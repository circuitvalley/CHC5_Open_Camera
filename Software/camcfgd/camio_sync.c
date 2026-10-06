// SPDX-License-Identifier: CC-BY-NC-ND-4.0
/*
 * camio_sync.c - camcfgd client for the chc5_camio_sync aux-I/O + sync block
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#include "camio_sync.h"
#include "common.h"

#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#define CAMIO_SUBDEV_SCAN_MAX 64

static pthread_mutex_t g_st_lock = PTHREAD_MUTEX_INITIALIZER;

static int g_fd = -1;
static int g_probed_absent;
static int g_trig = 2;

static pthread_mutex_t g_w_lock = PTHREAD_MUTEX_INITIALIZER;
static int      g_w_auto;
static uint32_t g_w_offset_ns;
static uint32_t g_w_exposure_us;
static uint32_t g_w_written;

static int camio_set(int fd, uint32_t id, int32_t val)
{
	struct v4l2_ext_control c;
	struct v4l2_ext_controls cs;

	memset(&c, 0, sizeof(c));
	memset(&cs, 0, sizeof(cs));
	c.id = id;
	c.value = val;
	cs.count = 1;
	cs.controls = &c;

	if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &cs) < 0) {
		LOG_WARN("camio: S_EXT_CTRLS id=0x%08x val=%d: %s",
			 id, val, strerror(errno));
		return -1;
	}
	return 0;
}

static int camio_set64(int fd, uint32_t id, int64_t val)
{
	struct v4l2_ext_control c;
	struct v4l2_ext_controls cs;

	memset(&c, 0, sizeof(c));
	memset(&cs, 0, sizeof(cs));
	c.id = id;
	c.value64 = val;
	cs.count = 1;
	cs.controls = &c;

	if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &cs) < 0) {
		LOG_WARN("camio: S_EXT_CTRLS id=0x%08x val=%lld: %s",
			 id, (long long)val, strerror(errno));
		return -1;
	}
	return 0;
}

static int camio_get(int fd, uint32_t id, int32_t *val)
{
	struct v4l2_control c;

	memset(&c, 0, sizeof(c));
	c.id = id;
	if (ioctl(fd, VIDIOC_G_CTRL, &c) < 0)
		return -1;
	*val = c.value;
	return 0;
}

static bool subdev_is_camio(int fd)
{
	struct v4l2_queryctrl q;

	memset(&q, 0, sizeof(q));
	q.id = CHC5CAMIO_CID_ENABLE;
	return ioctl(fd, VIDIOC_QUERYCTRL, &q) == 0;
}

static int g_ptp_pulse = 2;
static int g_native_gate;

int camio_sync_has_ptp_pulse(void)
{
	return g_fd >= 0 ? g_ptp_pulse : 2;
}

int camio_sync_open(void)
{
	int i;

	if (g_fd >= 0)
		return 0;
	if (g_probed_absent)
		return -1;

	for (i = 0; i < CAMIO_SUBDEV_SCAN_MAX; i++) {
		char path[32];
		int fd;

		snprintf(path, sizeof(path), "/dev/v4l-subdev%d", i);
		fd = open(path, O_RDWR);
		if (fd < 0)
			continue;

		if (subdev_is_camio(fd)) {
			struct v4l2_queryctrl q;

			g_fd = fd;
			memset(&q, 0, sizeof(q));
			q.id = CHC5CAMIO_CID_OUT_SOURCE;
			g_ptp_pulse = ioctl(fd, VIDIOC_QUERYCTRL, &q) == 0 &&
				      q.maximum >= CHC5CAMIO_OUT_PTP_PULSE;
			memset(&q, 0, sizeof(q));
			q.id = CHC5CAMIO_CID_TRIG_SOURCE;
			g_trig = ioctl(fd, VIDIOC_QUERYCTRL, &q) == 0;
			memset(&q, 0, sizeof(q));
			q.id = CHC5CAMIO_CID_NATIVE_LOW;
			g_native_gate = ioctl(fd, VIDIOC_QUERYCTRL, &q) == 0;
			LOG_INFO("camio: control subdev found at %s%s%s%s", path,
				 g_ptp_pulse ? " (PTP time base)" : "",
				 g_trig ? " (v3 trigger)" : "",
				 g_native_gate ? " (native gate)" : "");
			return 0;
		}
		close(fd);
	}
	g_probed_absent = 1;
	LOG_INFO("camio: no aux-I/O control subdev in this bitstream -- aux disabled");
	return -1;
}

bool camio_sync_present(void)
{
	return g_fd >= 0;
}

int camio_sync_hw_state(void)
{
	if (g_fd >= 0)
		return 1;
	if (g_probed_absent)
		return 0;
	return -1;
}

void camio_sync_close(void)
{
	if (g_fd >= 0) {
		close(g_fd);
		g_fd = -1;
	}
}

static int g_src = CHC5_SYNC_FREE_RUN;
static int g_src_xtrig;
static uint64_t g_sched_period_applied;
static int      g_ptp_follow;
static struct camio_facts g_facts = {
	.has_trig = 2, .role_by = CHC5_FACT_UNKNOWN, .trig_route = CHC5_FACT_UNKNOWN,
	.has_strobe = 2, .xvs_out = 2,
};
static uint32_t g_line_ns_written, g_lines_written;
static int g_sensor_on;

static int ptp_by_trigger(const struct camio_facts *f, const struct camio_cfg *c)
{
	return f->can_trigger && f->trig_route != CHC5_FACT_NONE &&
	       c->sync_trig_route == CHC5CAMIO_TRIGROUTE_XTRIG;
}

int camio_sync_resolve(const struct camio_cfg *in, const struct camio_facts *f,
		       struct camio_cfg *out, const char **why)
{
	uint64_t min_ps = (uint64_t)f->min_frame_ns * 1000u;
	int src;

	*out = *in;
	*why = NULL;

	if (f->role_by != CHC5_FACT_UNKNOWN)
		out->sync_role_by = (uint8_t)f->role_by;
	if (f->trig_route != CHC5_FACT_UNKNOWN && f->trig_route != CHC5_FACT_NONE)
		out->sync_trig_route = (uint8_t)f->trig_route;

	if (out->sync_role == CHC5CAMIO_ROLE_MASTER) {
		if (out->in_function == CHC5CAMIO_IN_SYNCIN) {
			out->in_function = CHC5CAMIO_IN_OFF;
			*why = "role=master conflicts with Sync in (XVS bus contention) -- masked";
		}
		if (out->sync_trig_route == CHC5CAMIO_TRIGROUTE_XVS_SLAVE) {
			out->sync_trig_route = CHC5CAMIO_TRIGROUTE_XTRIG;
			*why = "role=master conflicts with the XVS trigger route (bus contention) -- masked";
		}
	}

	if (out->in_function != CHC5CAMIO_IN_TRIGGER &&
	    (out->in_function == CHC5CAMIO_IN_SYNCIN ||
	     out->sync_role == CHC5CAMIO_ROLE_SLAVE)) {
		out->in_function = CHC5CAMIO_IN_SYNCIN;
		out->sync_role = CHC5CAMIO_ROLE_SLAVE;
	} else if (out->in_function == CHC5CAMIO_IN_TRIGGER &&
		   out->sync_role == CHC5CAMIO_ROLE_SLAVE) {
		out->sync_role = CHC5CAMIO_ROLE_OFF;
		*why = "SyncRole Slave is off while TriggerMode is On";
	}

	if (out->in_function == CHC5CAMIO_IN_TRIGGER &&
	    (f->trig_route == CHC5_FACT_NONE || !f->can_trigger)) {
		out->in_function = CHC5CAMIO_IN_OFF;
		src = CHC5_SYNC_FREE_RUN;
		*why = f->trig_route == CHC5_FACT_NONE ? "this sensor has no trigger input -- free run"
						       : "this sensor has no trigger mode -- free run";
	} else if (out->in_function == CHC5CAMIO_IN_TRIGGER) {
		switch (out->trig_src) {
		case CHC5CAMIO_TRIG_SRC_SOFTWARE: src = CHC5_SYNC_SOFTWARE; break;
		case CHC5CAMIO_TRIG_SRC_ACTION:   src = CHC5_SYNC_ACTION;   break;
		default:
			out->trig_src = CHC5CAMIO_TRIG_SRC_LINE0;
			src = CHC5_SYNC_LINE0;
			break;
		}
		if (src != CHC5_SYNC_LINE0 && f->has_trig != 1) {
			out->trig_src = CHC5CAMIO_TRIG_SRC_LINE0;
			src = CHC5_SYNC_LINE0;
			*why = "Software / Action1 trigger needs the camio v3 trigger block -- Line0";
		}
		if (in->sched_enable)
			*why = "synchronous free run is off while TriggerMode is On";
	} else if (out->in_function == CHC5CAMIO_IN_SYNCIN && (!f->can_follow || f->has_trig != 1)) {
		out->in_function = CHC5CAMIO_IN_OFF;
		out->sync_role = CHC5CAMIO_ROLE_OFF;
		src = CHC5_SYNC_FREE_RUN;
		*why = !f->can_follow ? "genlock needs the sensor's follower mode (XCLR under software control) -- free run"
				      : "genlock needs the camio v3 line generator -- free run";
	} else if (out->in_function == CHC5CAMIO_IN_SYNCIN) {
		src = CHC5_SYNC_GENLOCK;
		if (in->sched_enable)
			*why = "synchronous free run is off while genlocked (Sync in)";
	} else if (in->sched_enable && f->has_trig == 1 &&
		   (ptp_by_trigger(f, out) || f->can_follow)) {
		if (ptp_by_trigger(f, out)) {
			out->in_function = CHC5CAMIO_IN_TRIGGER;
			out->trig_src = CHC5CAMIO_TRIG_SRC_PTP;
		} else {
			if (out->sync_role == CHC5CAMIO_ROLE_MASTER)
				*why = "SyncRole Master is off: PTP sync runs this sensor as a follower";
			out->in_function = CHC5CAMIO_IN_OFF;
			out->sync_role = CHC5CAMIO_ROLE_PTP;
		}
		if (!out->sched_period_ps)
			out->sched_period_ps = chc5camio_mhz_to_ps(CHC5_SYNC_RATE_DEFAULT_MHZ);
		if (min_ps && out->sched_period_ps < min_ps) {
			out->sched_period_ps = min_ps;
			*why = "synchronous free run faster than the sensor can expose and read out -- slowed to its maximum";
		}
		out->trigger_divider = 1;
		out->trigger_delay_ns = 0;
		src = CHC5_SYNC_PTP;
	} else {
		if (in->sched_enable)
			*why = f->has_trig != 1 ? "synchronous free run needs the camio v3 block -- free run"
						: "synchronous free run needs a sensor trigger or follower mode -- free run";
		src = CHC5_SYNC_FREE_RUN;
	}
	out->sched_enable = src == CHC5_SYNC_PTP;
	if (src != CHC5_SYNC_LINE0)
		out->width_mode = 0;
	if (src != CHC5_SYNC_FREE_RUN && src != CHC5_SYNC_GENLOCK &&
	    out->trig_minper_ns < f->min_frame_ns)
		out->trig_minper_ns = f->min_frame_ns;
	return src;
}

const char *camio_sync_source_name(int src)
{
	switch (src) {
	case CHC5_SYNC_LINE0:    return "Line0 trigger";
	case CHC5_SYNC_SOFTWARE: return "software trigger";
	case CHC5_SYNC_ACTION:   return "action command trigger";
	case CHC5_SYNC_PTP:      return "PTP synchronous free run";
	case CHC5_SYNC_GENLOCK:  return "genlock (Sync in)";
	default:                 return "free run";
	}
}

static int write_line_timing(void)
{
	uint32_t ns = (g_facts.line_ps + 500u) / 1000u;
	uint32_t lines = g_ptp_follow && g_facts.line_ps
		       ? (uint32_t)(g_sched_period_applied / g_facts.line_ps)
		       : g_facts.frame_lines;
	int r = 0;

	if (g_fd < 0 || g_trig != 1 || !ns || !lines)
		return 0;
	if (ns > 0xFFFFFFu)
		ns = 0xFFFFFFu;
	if (ns != g_line_ns_written) {
		r |= camio_set(g_fd, CHC5CAMIO_CID_LINE_PERIOD_NS, (int32_t)ns);
		g_line_ns_written = ns;
	}
	if (lines != g_lines_written) {
		r |= camio_set(g_fd, CHC5CAMIO_CID_FRAME_LINES,
			       (int32_t)(lines > 0xFFFFFFu ? 0xFFFFFFu : lines));
		g_lines_written = lines;
	}
	return r;
}

int camio_sync_set_sensor(const struct camio_facts *f)
{
	int has_trig, changed, src;

	pthread_mutex_lock(&g_st_lock);
	has_trig = g_facts.has_trig;
	changed = f->can_trigger  != g_facts.can_trigger  ||
		      f->can_follow   != g_facts.can_follow   ||
		      f->follow_xhs   != g_facts.follow_xhs   ||
		      f->min_frame_ns != g_facts.min_frame_ns ||
		      f->role_by      != g_facts.role_by      ||
		      f->trig_route   != g_facts.trig_route   ||
		      f->has_strobe   != g_facts.has_strobe   ||
		      f->strobe_low   != g_facts.strobe_low   ||
		      f->xvs_out      != g_facts.xvs_out;

	g_facts = *f;
	g_facts.has_trig = has_trig;
	src = g_src;
	pthread_mutex_unlock(&g_st_lock);
	if (src == CHC5_SYNC_GENLOCK || src == CHC5_SYNC_PTP)
		(void)write_line_timing();
	return changed;
}

uint32_t camio_sync_io_caps(void)
{
	int v3 = g_fd >= 0 && g_trig == 1;
	uint32_t c = 0;
	struct camio_facts f;

	pthread_mutex_lock(&g_st_lock);
	f = g_facts;
	pthread_mutex_unlock(&g_st_lock);

	if (f.can_trigger && v3 && f.trig_route != CHC5_FACT_NONE)
		c |= CHC5_IOCAP_TRIGGER | CHC5_IOCAP_SOFTWARE | CHC5_IOCAP_PTP_SYNC;
	if (f.can_follow && v3)
		c |= CHC5_IOCAP_PTP_SYNC;
	if (v3)
		c |= CHC5_IOCAP_DEBOUNCE;
	if (f.can_follow && v3)
		c |= CHC5_IOCAP_GENLOCK;
	if (f.has_strobe != 0)
		c |= CHC5_IOCAP_NATIVE_STROBE;
	if (f.xvs_out != 0)
		c |= CHC5_IOCAP_SYNC_OUT;
	if (camio_sync_has_ptp_pulse() == 1)
		c |= CHC5_IOCAP_PTP_PULSE;
	return c;
}

#define CAMIO_STROBE_US_MAX  (CHC5CAMIO_TIME_MAX_UNITS * CHC5CAMIO_TICK_US)
#define CAMIO_SCHED_PS_MIN   (1000000000000000ull / CHC5_SYNC_RATE_MAX_MHZ)
#define CAMIO_SCHED_PS_MAX   (1000000000000000ull / CHC5_SYNC_RATE_MIN_MHZ)

uint32_t camio_sync_check(const struct camio_cfg *c, uint32_t mask)
{
	int trig = camio_sync_has_trigger();
	uint32_t bad = 0;
	struct camio_facts f;

	pthread_mutex_lock(&g_st_lock);
	f = g_facts;
	pthread_mutex_unlock(&g_st_lock);

#define BAD(fb, cond, ...) do { if ((mask & CAMIO_F(fb)) && (cond)) { \
		bad |= CAMIO_F(fb); LOG_WARN("camio: " __VA_ARGS__); } } while (0)
	BAD(IN_FUNCTION,   c->in_function > CHC5CAMIO_IN_SYNCIN, "input function %u -- ignored", c->in_function);
	BAD(IN_ACTIVATION, c->in_activation > CHC5CAMIO_ACT_LEVEL, "TriggerActivation %u -- ignored", c->in_activation);
	BAD(IN_INVERT,     c->in_invert > 1, "Line0 inverter %u -- ignored", c->in_invert);
	BAD(TRIG_DELAY,    c->trigger_delay_ns > CHC5_TRIG_DELAY_MAX_US * 1000u, "TriggerDelay %u ns -- ignored", c->trigger_delay_ns);
	BAD(TRIG_DIVIDER,  c->trigger_divider > 0xFFu, "TriggerDivider %u (8 bit) -- ignored", c->trigger_divider);
	BAD(TRIG_SRC,      c->trig_src != CHC5CAMIO_TRIG_SRC_LINE0 &&
			   c->trig_src != CHC5CAMIO_TRIG_SRC_SOFTWARE &&
			   c->trig_src != CHC5CAMIO_TRIG_SRC_ACTION, "TriggerSource %u -- ignored", c->trig_src);
	BAD(TRIG_SRC,      c->trig_src != CHC5CAMIO_TRIG_SRC_LINE0 && trig == 0,
	    "TriggerSource Software / Action1 needs the camio v3 trigger block -- ignored");
	BAD(WIDTH_MODE,    c->width_mode > 1, "ExposureMode %u -- ignored", c->width_mode);
	BAD(WIDTH_MODE,    c->width_mode == 1 && trig == 0,
	    "ExposureMode TriggerWidth needs the camio v3 trigger block -- ignored");
	BAD(IN_FILTER,     c->in_filter_ns > 0xFFFFFFu, "LineDebouncerTime %u ns (24 bit) -- ignored", c->in_filter_ns);
	BAD(SCHED_ENABLE,  c->sched_enable > 1, "SyncFreeRunTimerEnable %u -- ignored", c->sched_enable);
	BAD(SCHED_PERIOD,  c->sched_period_ps && (c->sched_period_ps < CAMIO_SCHED_PS_MIN ||
					      c->sched_period_ps > CAMIO_SCHED_PS_MAX),
	    "synchronous free run period %llu ps -- ignored", (unsigned long long)c->sched_period_ps);
	BAD(STROBE_ENABLE, c->strobe_enable > 1, "StrobeEnable %u -- ignored", c->strobe_enable);
	BAD(STROBE_INVERT, c->strobe_invert > 1, "strobe inverter %u -- ignored", c->strobe_invert);
	BAD(STROBE_SRC,    c->strobe_src > CHC5CAMIO_STRB_SRC_NATIVE, "StrobeSource %u -- ignored", c->strobe_src);
	BAD(STROBE_SRC,    c->strobe_src == CHC5CAMIO_STRB_SRC_NATIVE && f.has_strobe == 0,
	    "StrobeSource NativeStrobe: this sensor has no strobe output -- ignored");
	BAD(STROBE_DELAY,  c->strobe_delay_us > CAMIO_STROBE_US_MAX, "StrobeDelay %u us -- ignored", c->strobe_delay_us);
	BAD(STROBE_DUR,    c->strobe_duration_us > CAMIO_STROBE_US_MAX, "StrobeDuration %u us -- ignored", c->strobe_duration_us);
	BAD(STROBE_MINON,  c->strobe_minon_us > CAMIO_STROBE_US_MAX, "StrobeMinOn %u us -- ignored", c->strobe_minon_us);
	BAD(OUT_SOURCE,    c->out_source > CHC5CAMIO_OUT_PTP_PULSE, "LineSource %u -- ignored", c->out_source);
	BAD(OUT_SOURCE,    c->out_source == CHC5CAMIO_OUT_PTP_PULSE && camio_sync_has_ptp_pulse() == 0,
	    "LineSource PtpPulse needs the PTP time base -- ignored");
	BAD(OUT_SOURCE,    c->out_source == CHC5CAMIO_OUT_EXPACTIVE && f.has_strobe == 0,
	    "LineSource ExposureActive: this sensor has no exposure output -- ignored");
	BAD(OUT_SOURCE,    c->out_source == CHC5CAMIO_OUT_SYNCOUT && f.xvs_out == 0,
	    "LineSource SyncOut: this sensor drives no XVS -- ignored");
	BAD(OUT_INVERT,    c->out_invert > 1, "Line1 inverter %u -- ignored", c->out_invert);
	BAD(OUT_USER,      c->out_user_value > 1, "UserOutputValue %u -- ignored", c->out_user_value);
	BAD(SYNC_ROLE,     c->sync_role > CHC5CAMIO_ROLE_SLAVE, "SyncRole %u -- ignored", c->sync_role);
	BAD(SYNC_ROLE,     c->sync_role == CHC5CAMIO_ROLE_MASTER && f.xvs_out == 0,
	    "SyncRole Master: this sensor drives no XVS -- ignored");
	BAD(SYNC_ROLE_BY,  1, "SyncRoleBy is the sensor's wiring, not a setting -- ignored");
	BAD(SYNC_TRIG_ROUTE, 1, "trigger route is the sensor's wiring, not a setting -- ignored");
#undef BAD
	return mask & ~bad;
}

int camio_sync_plan_follower(const struct camio_cfg *c)
{
	struct camio_cfg out;
	struct camio_facts f;
	const char *why;
	int src;

	pthread_mutex_lock(&g_st_lock);
	f = g_facts;
	f.has_trig = g_trig;
	pthread_mutex_unlock(&g_st_lock);
	src = camio_sync_resolve(c, &f, &out, &why);
	return src == CHC5_SYNC_GENLOCK ||
	       (src == CHC5_SYNC_PTP && out.sync_role == CHC5CAMIO_ROLE_PTP);
}

int camio_sync_wants_follower(void)
{
	int r;

	pthread_mutex_lock(&g_st_lock);
	r = g_src == CHC5_SYNC_GENLOCK || (g_src == CHC5_SYNC_PTP && g_ptp_follow);
	pthread_mutex_unlock(&g_st_lock);
	return r;
}

uint32_t camio_sync_follow_rate_mhz(void)
{
	uint32_t r;

	pthread_mutex_lock(&g_st_lock);
	r = g_ptp_follow ? chc5camio_ps_to_mhz(g_sched_period_applied) : 0;
	pthread_mutex_unlock(&g_st_lock);
	return r;
}

#define CAMIO_TRIG_HOLD_MAX 16u
#define CAMIO_TRIG_HOLD_NS  (2000ull * 1000000ull)
static unsigned g_change_gen;
static unsigned g_applied_gen;
static unsigned g_held[2];
static uint64_t g_held_ns[2];

static uint64_t mono_ns(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

void camio_sync_note_change(void)
{
	__atomic_add_fetch(&g_change_gen, 1, __ATOMIC_SEQ_CST);
}

unsigned camio_sync_change_gen(void)
{
	return __atomic_load_n(&g_change_gen, __ATOMIC_SEQ_CST);
}

static const char *trig_ev_name(int k)
{
	return k ? "action command" : "TriggerSoftware";
}

void camio_sync_applied(unsigned gen)
{
	uint64_t held_ns[2], now;
	unsigned n[2];
	int src, k;

	pthread_mutex_lock(&g_st_lock);
	g_applied_gen = gen;
	if (gen != camio_sync_change_gen() || !g_sensor_on) {
		pthread_mutex_unlock(&g_st_lock);
		return;
	}
	n[0] = g_held[0];
	n[1] = g_held[1];
	held_ns[0] = g_held_ns[0];
	held_ns[1] = g_held_ns[1];
	g_held[0] = g_held[1] = 0;
	src = g_src;
	pthread_mutex_unlock(&g_st_lock);

	now = mono_ns();
	for (k = 0; k < 2; k++) {
		if (!n[k])
			continue;
		if (now - held_ns[k] > CAMIO_TRIG_HOLD_NS) {
			LOG_WARN("camio: %u %s held %llu ms without a running sensor -- dropped",
				 n[k], trig_ev_name(k),
				 (unsigned long long)((now - held_ns[k]) / 1000000ull));
			continue;
		}
		if (src != (k ? CHC5_SYNC_ACTION : CHC5_SYNC_SOFTWARE)) {
			LOG_WARN("camio: %u %s held over a settings change -- dropped, frame timing is %s",
				 n[k], trig_ev_name(k), camio_sync_source_name(src));
			continue;
		}
		LOG_INFO("camio: %u %s held over a settings change -- fired", n[k], trig_ev_name(k));
		while (n[k]--)
			(void)camio_sync_software_trigger();
	}
}

int camio_sync_fire(int ev)
{
	int want = ev == CHC5_TRIGEV_ACTION ? CHC5_SYNC_ACTION : CHC5_SYNC_SOFTWARE;
	int k = ev == CHC5_TRIGEV_ACTION;
	int src;

	pthread_mutex_lock(&g_st_lock);
	if (g_applied_gen != camio_sync_change_gen() || !g_sensor_on) {
		if (!g_held[k])
			g_held_ns[k] = mono_ns();
		if (g_held[k] < CAMIO_TRIG_HOLD_MAX)
			g_held[k]++;
		pthread_mutex_unlock(&g_st_lock);
		LOG_DBG("camio: %s held until the sensor runs with the pending change", trig_ev_name(k));
		return 0;
	}
	src = g_src;
	pthread_mutex_unlock(&g_st_lock);
	if (src != want) {
		LOG_DBG("camio: %s while the trigger source is %s -- ignored",
			ev == CHC5_TRIGEV_ACTION ? "action command" : "TriggerSoftware",
			camio_sync_source_name(src));
		return -1;
	}
	return camio_sync_software_trigger();
}

uint32_t camio_sync_sched_rate_mhz(void)
{
	uint32_t r;

	pthread_mutex_lock(&g_st_lock);
	r = g_src == CHC5_SYNC_PTP ? chc5camio_ps_to_mhz(g_sched_period_applied) : 0;
	pthread_mutex_unlock(&g_st_lock);
	return r;
}

int camio_sync_source(void)
{
	int r;

	pthread_mutex_lock(&g_st_lock);
	r = g_src;
	pthread_mutex_unlock(&g_st_lock);
	return r;
}

int camio_sync_uses_xtrig(void)
{
	int r;

	pthread_mutex_lock(&g_st_lock);
	r = g_src_xtrig;
	pthread_mutex_unlock(&g_st_lock);
	return r;
}

static struct camio_cfg g_applied;
static int g_applied_ok;
static unsigned g_native_low;

static int line1_strobe_enable(const struct camio_cfg *c)
{
	return c->strobe_enable &&
	       (c->strobe_src != CHC5CAMIO_STRB_SRC_NATIVE || g_sensor_on);
}

static int line1_out_write(const struct camio_cfg *c)
{
	int exp = c->out_source == CHC5CAMIO_OUT_EXPACTIVE;
	int r = 0;

	r |= camio_set(g_fd, CHC5CAMIO_CID_OUT_USER_VALUE,
		       exp ? (int32_t)g_native_low : c->out_user_value);
	r |= camio_set(g_fd, CHC5CAMIO_CID_OUT_INVERT,
		       c->out_invert ^ (exp && g_native_low));
	r |= camio_set(g_fd, CHC5CAMIO_CID_OUT_SOURCE,
		       exp && !g_sensor_on ? CHC5CAMIO_OUT_USER : c->out_source);
	return r;
}

void camio_sync_sensor_running(int on)
{
	on = !!on;
	if (on == g_sensor_on)
		return;
	pthread_mutex_lock(&g_st_lock);
	g_sensor_on = on;
	pthread_mutex_unlock(&g_st_lock);
	if (g_fd < 0 || !g_applied_ok)
		return;
	if (line1_out_write(&g_applied) |
	    camio_set(g_fd, CHC5CAMIO_CID_STROBE_ENABLE, line1_strobe_enable(&g_applied)))
		LOG_WARN("camio: Line1 %s the sensor: control write errors",
			 on ? "to" : "off");
}

int camio_sync_apply(const struct camio_cfg *c)
{
	struct camio_cfg cc;
	int r = 0;

	if (!c || !c->present)
		return 0;
	if (g_fd < 0 && camio_sync_open() != 0)
		return -1;

	int timing_change;
	struct camio_facts f;

	{
		const char *why;
		int src;

		pthread_mutex_lock(&g_st_lock);
		f = g_facts;
		pthread_mutex_unlock(&g_st_lock);
		f.has_trig = g_trig;
		src = camio_sync_resolve(c, &f, &cc, &why);

		if (why)
			LOG_WARN("camio: %s", why);
		pthread_mutex_lock(&g_st_lock);
		if (src != g_src)
			LOG_INFO("camio: frame timing %s", camio_sync_source_name(src));
		g_src = src;
		g_sched_period_applied = cc.sched_enable ? cc.sched_period_ps : 0;
		g_ptp_follow = src == CHC5_SYNC_PTP && cc.sync_role == CHC5CAMIO_ROLE_PTP;
		g_src_xtrig = cc.in_function == CHC5CAMIO_IN_TRIGGER &&
			      cc.sync_trig_route == CHC5CAMIO_TRIGROUTE_XTRIG;
		pthread_mutex_unlock(&g_st_lock);
		c = &cc;
	}

	timing_change = !g_applied_ok ||
			c->in_function     != g_applied.in_function     ||
			c->trig_src        != g_applied.trig_src        ||
			c->sync_role       != g_applied.sync_role       ||
			c->sync_role_by    != g_applied.sync_role_by    ||
			c->sync_trig_route != g_applied.sync_trig_route ||
			c->sched_enable    != g_applied.sched_enable;
	g_applied = *c;
	g_applied_ok = 1;
	if (timing_change)
		r |= camio_set(g_fd, CHC5CAMIO_CID_ENABLE, 0);

	r |= camio_set(g_fd, CHC5CAMIO_CID_IN_FUNCTION,   c->in_function);
	r |= camio_set(g_fd, CHC5CAMIO_CID_IN_ACTIVATION, c->in_activation);
	r |= camio_set(g_fd, CHC5CAMIO_CID_IN_INVERT,     c->in_invert);
	r |= camio_set(g_fd, CHC5CAMIO_CID_TRIG_DELAY_NS, (int32_t)c->trigger_delay_ns);
	r |= camio_set(g_fd, CHC5CAMIO_CID_IN_DIVIDER,    c->trigger_divider);
	if (g_trig == 1) {
		r |= camio_set(g_fd, CHC5CAMIO_CID_TRIG_SOURCE,
			       c->trig_src == CHC5CAMIO_TRIG_SRC_ACTION
			       ? CHC5CAMIO_TRIG_SRC_SOFTWARE : c->trig_src);
		r |= camio_set(g_fd, CHC5CAMIO_CID_TRIG_WIDTH_MODE, c->width_mode);
		r |= camio_set(g_fd, CHC5CAMIO_CID_TRIG_MINPER_NS,  (int32_t)c->trig_minper_ns);
		r |= camio_set(g_fd, CHC5CAMIO_CID_IN_FILTER_NS,
			       (int32_t)(c->in_filter_ns > 0xFFFFFFu ? 0xFFFFFFu : c->in_filter_ns));
	}

	g_native_low = f.strobe_low == 1;
	if (g_native_gate)
		r |= camio_set(g_fd, CHC5CAMIO_CID_NATIVE_LOW, (int32_t)g_native_low);

	r |= camio_set(g_fd, CHC5CAMIO_CID_STROBE_INVERT,    c->strobe_invert ^
		       (g_native_low && c->strobe_src == CHC5CAMIO_STRB_SRC_NATIVE));
	r |= camio_set(g_fd, CHC5CAMIO_CID_STROBE_SRC,       c->strobe_src);
	r |= camio_set(g_fd, CHC5CAMIO_CID_STROBE_DELAY,
		       chc5camio_us_to_units(c->strobe_delay_us));
	r |= camio_set(g_fd, CHC5CAMIO_CID_STROBE_DURATION,
		       chc5camio_us_to_units(c->strobe_duration_us));
	r |= camio_set(g_fd, CHC5CAMIO_CID_STROBE_MINON,
		       chc5camio_us_to_units(c->strobe_minon_us));
	r |= camio_set(g_fd, CHC5CAMIO_CID_STROBE_ENABLE,    line1_strobe_enable(c));

	r |= line1_out_write(c);

	if (g_trig == 1) {
		g_line_ns_written = g_lines_written = 0;
		r |= write_line_timing();
		r |= camio_set(g_fd, CHC5CAMIO_CID_LINE_XHS,
			       camio_sync_wants_follower() && g_facts.follow_xhs);
	}

	if (g_trig == 1) {
		if (!c->sched_enable)
			r |= camio_set(g_fd, CHC5CAMIO_CID_SCHED_ENABLE, 0);
		if (c->sched_period_ps)
			r |= camio_set64(g_fd, CHC5CAMIO_CID_SCHED_PERIOD_PS,
					 (int64_t)c->sched_period_ps);
		r |= camio_set64(g_fd, CHC5CAMIO_CID_SCHED_START_NS,
				 (int64_t)(c->sched_start_ns & 0x7FFFFFFFFFFFFFFFull));
		if (c->sched_enable)
			r |= camio_set(g_fd, CHC5CAMIO_CID_SCHED_ENABLE, 1);
	}

	r |= camio_set(g_fd, CHC5CAMIO_CID_SYNC_XVS_POL,    1);
	r |= camio_set(g_fd, CHC5CAMIO_CID_SYNC_XHS_POL,    1);
	r |= camio_set(g_fd, CHC5CAMIO_CID_SYNC_ROLE,       c->sync_role);
	r |= camio_set(g_fd, CHC5CAMIO_CID_SYNC_ROLE_BY,    c->sync_role_by);
	r |= camio_set(g_fd, CHC5CAMIO_CID_SYNC_TRIG_ROUTE, c->sync_trig_route);

	{
		int on = c->in_function || c->strobe_enable ||
			 c->out_source  || c->sync_role;
		r |= camio_set(g_fd, CHC5CAMIO_CID_ENABLE, on ? 1 : 0);
	}

	if (r)
		LOG_WARN("camio: apply had control write errors");
	return r;
}

int camio_sync_has_trigger(void)
{
	return g_fd >= 0 ? g_trig : 2;
}

int camio_sync_software_trigger(void)
{
	if (g_fd < 0)
		return -1;
	if (g_trig != 1) {
		LOG_WARN("camio: TriggerSoftware needs the v3 trigger block -- ignored");
		return -1;
	}
	return camio_set(g_fd, CHC5CAMIO_CID_TRIG_SOFTWARE, 0);
}

int camio_sync_trigger_count(uint64_t *count)
{
	struct v4l2_ext_control c;
	struct v4l2_ext_controls cs;

	if (g_fd < 0 || g_trig != 1)
		return -1;
	memset(&c, 0, sizeof(c));
	memset(&cs, 0, sizeof(cs));
	c.id = CHC5CAMIO_CID_TRIG_COUNT;
	cs.count = 1;
	cs.controls = &c;
	if (ioctl(g_fd, VIDIOC_G_EXT_CTRLS, &cs) < 0)
		return -1;
	*count = (uint64_t)c.value64;
	return 0;
}

#define CAMIO_TRIG_WIDTH_MIN_NS 100u

static void width_write_locked(void)
{
	uint64_t want = (uint64_t)g_w_exposure_us * 1000u;
	uint64_t w = want > (uint64_t)g_w_offset_ns + CAMIO_TRIG_WIDTH_MIN_NS
		   ? want - g_w_offset_ns : CAMIO_TRIG_WIDTH_MIN_NS;
	uint32_t ns = w > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)w;

	if (g_fd < 0 || g_trig != 1 || !g_w_exposure_us || ns == g_w_written)
		return;
	if (camio_set(g_fd, CHC5CAMIO_CID_TRIG_WIDTH_NS, (int32_t)ns) == 0)
		g_w_written = ns;
}

void camio_sync_set_width_auto(int on)
{
	pthread_mutex_lock(&g_w_lock);
	g_w_auto = on;
	g_w_written = 0;
	pthread_mutex_unlock(&g_w_lock);
}

void camio_sync_set_exposure(uint32_t exposure_us, uint32_t offset_ns)
{
	pthread_mutex_lock(&g_w_lock);
	if (exposure_us)
		g_w_exposure_us = exposure_us;
	g_w_offset_ns = offset_ns;
	if (g_w_auto)
		width_write_locked();
	pthread_mutex_unlock(&g_w_lock);
}

int camio_sync_sched_status(uint32_t *st)
{
	int32_t v;

	if (g_fd < 0 || g_trig != 1 ||
	    camio_get(g_fd, CHC5CAMIO_CID_SCHED_STATUS, &v) < 0)
		return -1;
	*st = (uint32_t)v;
	return 0;
}

int camio_sync_line_levels(uint32_t *levels)
{
	int32_t v;

	if (g_fd < 0 || camio_get(g_fd, CHC5CAMIO_CID_STATUS, &v) < 0)
		return -1;
	*levels = ((v & (1 << 4)) ? 1u : 0u) |
		  ((v & (1 << 5)) ? 2u : 0u);
	return 0;
}

int camio_sync_read_status(uint32_t *status, uint32_t *in_status,
			   uint32_t *sync_status)
{
	int32_t v;

	if (g_fd < 0)
		return -1;

	if (status) {
		if (camio_get(g_fd, CHC5CAMIO_CID_STATUS, &v) < 0)
			return -1;
		*status = (uint32_t)v;
	}
	if (in_status) {
		if (camio_get(g_fd, CHC5CAMIO_CID_IN_STATUS, &v) < 0)
			return -1;
		*in_status = (uint32_t)v;
	}
	if (sync_status) {
		if (camio_get(g_fd, CHC5CAMIO_CID_SYNC_STATUS, &v) < 0)
			return -1;
		*sync_status = (uint32_t)v;
	}
	return 0;
}
