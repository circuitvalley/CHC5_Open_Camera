// SPDX-License-Identifier: GPL-2.0+
/*
 * TAI (time application interface) driver for Marvell PHYs and Marvell NETA.
 *
 * This file implements TAI support as a PTP clock. Timecounter/cyclecounter
 * representation taken from Marvell 88E6xxx DSA driver. We may need to share
 * the TAI between multiple PHYs in a multiport PHY.
 */
#include <linux/device.h>
#include <linux/export.h>
#include <linux/if_ether.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/marvell_ptp.h>

#define TAI_CONFIG_0				0
#define TAI_CONFIG_0_EVENTCAPOV			BIT(15)
#define TAI_CONFIG_0_EVENTCTRSTART		BIT(14)
#define TAI_CONFIG_0_EVENTPHASE			BIT(13)
#define TAI_CONFIG_0_TRIGGENINTEN		BIT(9)
#define TAI_CONFIG_0_EVENTCAPINTEN		BIT(8)

/* TAI Global status register
 * 15  EventInt (A38x, 88E151x) - Event capture interrupt
 * 14  Capture (88E6393) - Capture trigger (0=extts 1=PTP_TRIG internal event)
 * 9   EventCapErr - Event capture error (overflow)
 * 8   EventCapValid - Event capture valid
 * 7:0 EventCapCtr - Event capture counter
 */
#define TAI_CONFIG_9				9
#define TAI_CONFIG_9_EVENTCAPERR		BIT(9)
#define TAI_CONFIG_9_EVENTCAPVALID		BIT(8)

#define TAI_EVENT_POLL_INTERVAL msecs_to_jiffies(100)

struct marvell_tai {
	const struct marvell_tai_ops *ops;
	struct device *dev;

	struct ptp_clock_info caps;
	struct ptp_clock *ptp_clock;

	u32 cc_mult_num;
	u32 cc_mult_den;
	u32 cc_mult;

	struct mutex mutex;
	struct timecounter timecounter;
	struct cyclecounter cyclecounter;

	long refresh_period;
	struct delayed_work overflow_work;
	struct delayed_work event_work;

	/* Used while reading the TAI */
	struct ptp_system_timestamp *sts;

	bool cc_primed;
	bool read_held;
	bool jump_pending;
	u64 jump_cyc;
	u32 read_errors;
	u32 read_jumps;
};

static struct marvell_tai *cc_to_tai(const struct cyclecounter *cc)
{
	return container_of(cc, struct marvell_tai, cyclecounter);
}

/* Read the global time registers using the readplus command */
static u64 marvell_tai_clock_read(const struct cyclecounter *cc)
{
	struct marvell_tai *tai = cc_to_tai(cc);
	u64 last = tai->timecounter.cycle_last;
	u64 cyc, delta;

	cyc = tai->ops->tai_clock_read(tai->dev, tai->sts);
	if (cyc == MARVELL_TAI_READ_FAILED) {
		tai->read_held = true;
		tai->read_errors++;
		dev_err_ratelimited(tai->dev,
				    "TAI read failed, clock held (%u failed reads)\n",
				    tai->read_errors);
		return last;
	}

	delta = (cyc - last) & cc->mask;
	if (tai->cc_primed && delta > cc->mask / 2) {
		if (!tai->jump_pending ||
		    ((cyc - tai->jump_cyc) & cc->mask) > cc->mask / 2) {
			tai->jump_pending = true;
			tai->jump_cyc = cyc;
			tai->read_held = true;
			tai->read_jumps++;
			dev_warn_ratelimited(tai->dev,
					     "TAI count 0x%08llx is more than half a wrap from 0x%08llx, clock held (%u jumps)\n",
					     cyc, last, tai->read_jumps);
			return last;
		}
		dev_warn_ratelimited(tai->dev,
				     "TAI count jump to 0x%08llx confirmed, accepted\n",
				     cyc);
	}
	tai->jump_pending = false;

	return cyc;
}

u64 marvell_tai_cyc2time(struct marvell_tai *tai, u32 cyc)
{
	u64 ns;

	mutex_lock(&tai->mutex);
	ns = timecounter_cyc2time(&tai->timecounter, cyc);
	mutex_unlock(&tai->mutex);

	return ns;
}
EXPORT_SYMBOL_GPL(marvell_tai_cyc2time);

u64 marvell_tai_gettime_ns(struct marvell_tai *tai)
{
	u64 ns;

	mutex_lock(&tai->mutex);
	ns = timecounter_read(&tai->timecounter);
	mutex_unlock(&tai->mutex);

	return ns;
}
EXPORT_SYMBOL_GPL(marvell_tai_gettime_ns);

static struct marvell_tai *ptp_to_tai(struct ptp_clock_info *ptp)
{
	return container_of(ptp, struct marvell_tai, caps);
}

static int marvell_tai_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct marvell_tai *tai = ptp_to_tai(ptp);
	bool neg;
	u32 diff;
	u64 adj;

	neg = scaled_ppm < 0;
	if (neg)
		scaled_ppm = -scaled_ppm;

	adj = tai->cc_mult_num;
	adj *= scaled_ppm;
	diff = div_u64(adj, tai->cc_mult_den);

	mutex_lock(&tai->mutex);
	timecounter_read(&tai->timecounter);
	tai->cyclecounter.mult = neg ? tai->cc_mult - diff :
				       tai->cc_mult + diff;
	mutex_unlock(&tai->mutex);

	return 0;
}

static int marvell_tai_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct marvell_tai *tai = ptp_to_tai(ptp);

	mutex_lock(&tai->mutex);
	timecounter_adjtime(&tai->timecounter, delta);
	mutex_unlock(&tai->mutex);

	return 0;
}

static int marvell_tai_gettimex64(struct ptp_clock_info *ptp,
				  struct timespec64 *ts,
				  struct ptp_system_timestamp *sts)
{
	struct marvell_tai *tai = ptp_to_tai(ptp);
	bool held;
	u64 ns;

	mutex_lock(&tai->mutex);
	tai->sts = sts;
	tai->read_held = false;
	ns = timecounter_read(&tai->timecounter);
	held = tai->read_held;
	tai->sts = NULL;
	mutex_unlock(&tai->mutex);

	if (held)
		return -EIO;

	*ts = ns_to_timespec64(ns);

	return 0;
}

static int marvell_tai_settime64(struct ptp_clock_info *ptp,
				 const struct timespec64 *ts)
{
	struct marvell_tai *tai = ptp_to_tai(ptp);
	u64 ns = timespec64_to_ns(ts);
	bool held;

	mutex_lock(&tai->mutex);
	tai->read_held = false;
	timecounter_init(&tai->timecounter, &tai->cyclecounter, ns);
	held = tai->read_held;
	mutex_unlock(&tai->mutex);

	return held ? -EIO : 0;
}

static void marvell_tai_extts(struct marvell_tai *tai)
{
	struct marvell_extts extts;
	struct ptp_clock_event ev;
	int err;

	err = tai->ops->tai_extts_read(tai->dev, TAI_CONFIG_9, &extts);
	if (err < 0) {
		dev_err(tai->dev, "failed to read TAI event capture\n");
		return;
	}

	if (extts.status & TAI_CONFIG_9_EVENTCAPERR) {
		dev_warn(tai->dev, "extts timestamp overrun (%x)\n",
			 extts.status);
		return;
	}

	if (extts.status & TAI_CONFIG_9_EVENTCAPVALID) {
		ev.type = PTP_CLOCK_EXTTS;
		ev.index = 0;
		ev.timestamp = marvell_tai_cyc2time(tai, extts.time);

		ptp_clock_event(tai->ptp_clock, &ev);
	}
}

static int marvell_tai_enable_extts(struct marvell_tai *tai,
				    struct ptp_extts_request *req, int enable)
{
	int err, pin;
	u16 cfg0;

	/* Reject requests to enable timestamping on both edges if
	 * userspace requests strict mode.
	 */
	if (req->flags & PTP_ENABLE_FEATURE &&
	    req->flags & PTP_STRICT_FLAGS &&
	    (req->flags & PTP_EXTTS_EDGES) == PTP_EXTTS_EDGES)
		return -EINVAL;

	pin = ptp_find_pin(tai->ptp_clock, PTP_PF_EXTTS, req->index);
	if (pin < 0)
		return -EBUSY;

	/* Setup this pin */
	err = tai->ops->tai_pin_setup(tai->dev, pin, PTP_PF_EXTTS, enable);
	if (err < 0)
		return err;

	if (enable) {
		/* Clear the status */
		err = tai->ops->tai_write(tai->dev, TAI_CONFIG_9, 0);
		if (err < 0)
			return err;

		cfg0 = TAI_CONFIG_0_EVENTCAPINTEN |
		       TAI_CONFIG_0_EVENTCTRSTART;

		/*
		 * For compatibility with DSA, we test for !rising rather
		 * than for falling. Marvell PHYs (88E151x) doesn't have
		 * this.
		 */
		if (!(req->flags & PTP_RISING_EDGE))
			cfg0 |= TAI_CONFIG_0_EVENTPHASE;

		/* Enable the event interrupt and counter */
		err = tai->ops->tai_modify(tai->dev, TAI_CONFIG_0,
					   TAI_CONFIG_0_EVENTCAPOV |
					   TAI_CONFIG_0_EVENTCTRSTART |
					   TAI_CONFIG_0_EVENTCAPINTEN |
					   TAI_CONFIG_0_EVENTPHASE, cfg0);
		if (err < 0)
			return err;

		schedule_delayed_work(&tai->event_work,
				      TAI_EVENT_POLL_INTERVAL);
	} else {
		/* Disable the event interrupt and counter */
		err = tai->ops->tai_modify(tai->dev, TAI_CONFIG_0,
					   TAI_CONFIG_0_EVENTCTRSTART |
					   TAI_CONFIG_0_EVENTCAPINTEN, 0);
		if (err < 0)
			return err;

		cancel_delayed_work_sync(&tai->event_work);
	}

	return 0;
}

static int marvell_tai_enable(struct ptp_clock_info *ptp,
			      struct ptp_clock_request *req, int enable)
{
	if (req->type != PTP_CLK_REQ_EXTTS)
		return -EOPNOTSUPP;

	return marvell_tai_enable_extts(ptp_to_tai(ptp), &req->extts, enable);
}

static int marvell_tai_verify(struct ptp_clock_info *ptp, unsigned int pin,
			      enum ptp_pin_function func, unsigned int chan)
{
	struct marvell_tai *tai = ptp_to_tai(ptp);

	/* Always allow a pin to be set to no function */
	if (func == PTP_PF_NONE)
		return 0;

	/* This driver only supports PTP_PF_EXTTS */
	if (func != PTP_PF_EXTTS)
		return -EOPNOTSUPP;

	if (!tai->ops->tai_pin_verify)
		return -EOPNOTSUPP;

	return tai->ops->tai_pin_verify(tai->dev, pin, func, chan);
}

static long marvell_tai_aux_work(struct ptp_clock_info *ptp)
{
	struct marvell_tai *tai = ptp_to_tai(ptp);
	long ret = -1;

	if (tai->ops->tai_aux_work)
		ret = tai->ops->tai_aux_work(tai->dev);

	return ret;
}

#define event_work_to_tai(w) \
	container_of(to_delayed_work(w), struct marvell_tai, event_work)
static void marvell_tai_event_work(struct work_struct *w)
{
	struct marvell_tai *tai = event_work_to_tai(w);

	marvell_tai_extts(tai);

	schedule_delayed_work(&tai->event_work, TAI_EVENT_POLL_INTERVAL);
}

/* Periodically read the timecounter to keep the time refreshed. */
#define overflow_work_to_tai(w) \
	container_of(to_delayed_work(w), struct marvell_tai, overflow_work)
static void marvell_tai_overflow_work(struct work_struct *w)
{
	struct marvell_tai *tai = overflow_work_to_tai(w);

	/* Read the timecounter to update */
	mutex_lock(&tai->mutex);
	timecounter_read(&tai->timecounter);
	mutex_unlock(&tai->mutex);

	schedule_delayed_work(&tai->overflow_work, tai->refresh_period);
}

static int marvell_tai_hw_enable(struct marvell_tai *tai)
{
	return tai->ops->tai_hw_enable(tai->dev);
}

static void marvell_tai_hw_disable(struct marvell_tai *tai)
{
	tai->ops->tai_hw_disable(tai->dev);
}

int marvell_tai_ptp_clock_index(struct marvell_tai *tai)
{
	return ptp_clock_index(tai->ptp_clock);
}
EXPORT_SYMBOL_GPL(marvell_tai_ptp_clock_index);

int marvell_tai_schedule(struct marvell_tai *tai, unsigned long delay)
{
	return ptp_schedule_worker(tai->ptp_clock, delay);
}
EXPORT_SYMBOL_GPL(marvell_tai_schedule);

void marvell_tai_cancel_worker(struct marvell_tai *tai)
{
	ptp_cancel_worker_sync(tai->ptp_clock);
}
EXPORT_SYMBOL_GPL(marvell_tai_cancel_worker);

void marvell_tai_remove(struct marvell_tai *tai)
{
	ptp_clock_unregister(tai->ptp_clock);

	/* tai->event_work will be disabled by ptp_clock_unregister()
	 * disabling the pins, so there's no need call
	 * cancel_delayed_work_sync(&tai->event_work) here.
	 */

	cancel_delayed_work_sync(&tai->overflow_work);

	marvell_tai_hw_disable(tai);
}
EXPORT_SYMBOL_GPL(marvell_tai_remove);

int marvell_tai_probe(struct marvell_tai **taip,
		      const struct marvell_tai_ops *ops,
		      const struct marvell_tai_param *param,
		      const struct marvell_tai_pins *pins,
		      const char *name, struct device *dev)
{
	struct marvell_tai *tai;
	u64 overflow_ns;
	int err;

	tai = devm_kzalloc(dev, sizeof(*tai), GFP_KERNEL);
	if (!tai)
		return -ENOMEM;

	mutex_init(&tai->mutex);

	tai->dev = dev;
	tai->ops = ops;
	tai->cc_mult_num = param->cc_mult_num;
	tai->cc_mult_den = param->cc_mult_den;
	tai->cc_mult = param->cc_mult;

	err = marvell_tai_hw_enable(tai);
	if (err < 0)
		return err;

	tai->cyclecounter.read = marvell_tai_clock_read;
	tai->cyclecounter.mask = CYCLECOUNTER_MASK(32);
	tai->cyclecounter.mult = param->cc_mult;
	tai->cyclecounter.shift = param->cc_shift;

	overflow_ns = BIT_ULL(32) * param->cc_mult;
	overflow_ns >>= param->cc_shift;
	tai->refresh_period = nsecs_to_jiffies64(overflow_ns / 4);

	timecounter_init(&tai->timecounter, &tai->cyclecounter,
			 ktime_to_ns(ktime_get_real()));
	tai->cc_primed = true;

	tai->caps.owner = THIS_MODULE;
	strscpy(tai->caps.name, name, sizeof(tai->caps.name));
	/* max_adj of 1000000 is what MV88E6xxx DSA uses */
	tai->caps.max_adj = 1000000;
	tai->caps.adjfine = marvell_tai_adjfine;
	tai->caps.adjtime = marvell_tai_adjtime;
	tai->caps.gettimex64 = marvell_tai_gettimex64;
	tai->caps.settime64 = marvell_tai_settime64;
	tai->caps.do_aux_work = marvell_tai_aux_work;

	if (pins) {
		tai->caps.n_ext_ts = pins->n_ext_ts;
		tai->caps.n_pins = pins->n_pins;
		tai->caps.pin_config = pins->pins;
		tai->caps.enable = marvell_tai_enable;
		tai->caps.verify = marvell_tai_verify;
	}

	INIT_DELAYED_WORK(&tai->overflow_work, marvell_tai_overflow_work);
	INIT_DELAYED_WORK(&tai->event_work, marvell_tai_event_work);

	tai->ptp_clock = ptp_clock_register(&tai->caps, dev);
	if (IS_ERR(tai->ptp_clock)) {
		marvell_tai_hw_disable(tai);
		return PTR_ERR(tai->ptp_clock);
	}

	/*
	 * Kick off the auxiliary worker to run once every quarter-overflow
	 * period to keep the timecounter properly updated.
	 */
	schedule_delayed_work(&tai->overflow_work, tai->refresh_period);

	*taip = tai;

	return 0;
}
EXPORT_SYMBOL_GPL(marvell_tai_probe);
