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
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#define CAMIO_SUBDEV_SCAN_MAX 64

static int g_fd = -1;
static int g_probed_absent;

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
			LOG_INFO("camio: control subdev found at %s%s", path,
				 g_ptp_pulse ? " (PTP time base)" : "");
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

int camio_sync_apply(const struct camio_cfg *c)
{
	struct camio_cfg cc;
	int r = 0;

	if (!c || !c->present)
		return 0;
	if (g_fd < 0 && camio_sync_open() != 0)
		return -1;

	if (c->sync_role == CHC5CAMIO_ROLE_MASTER &&
	    (c->in_function == CHC5CAMIO_IN_SYNCIN ||
	     c->sync_trig_route == CHC5CAMIO_TRIGROUTE_XVS_SLAVE)) {
		cc = *c;
		if (cc.in_function == CHC5CAMIO_IN_SYNCIN)
			cc.in_function = CHC5CAMIO_IN_OFF;
		if (cc.sync_trig_route == CHC5CAMIO_TRIGROUTE_XVS_SLAVE)
			cc.sync_trig_route = CHC5CAMIO_TRIGROUTE_XTRIG;
		LOG_WARN("camio: role=master conflicts with XVS-drive (sync-in/XVS-route) -- masked (bus contention)");
		c = &cc;
	}

	r |= camio_set(g_fd, CHC5CAMIO_CID_ENABLE, 0);

	r |= camio_set(g_fd, CHC5CAMIO_CID_IN_FUNCTION,   c->in_function);
	r |= camio_set(g_fd, CHC5CAMIO_CID_IN_ACTIVATION, c->in_activation);
	r |= camio_set(g_fd, CHC5CAMIO_CID_IN_INVERT,     c->in_invert);
	r |= camio_set(g_fd, CHC5CAMIO_CID_IN_DELAY,
		       chc5camio_us_to_units(c->trigger_delay_us));
	r |= camio_set(g_fd, CHC5CAMIO_CID_IN_DIVIDER,    c->trigger_divider);

	r |= camio_set(g_fd, CHC5CAMIO_CID_STROBE_INVERT,    c->strobe_invert);
	r |= camio_set(g_fd, CHC5CAMIO_CID_STROBE_SRC,       c->strobe_src);
	r |= camio_set(g_fd, CHC5CAMIO_CID_STROBE_DELAY,
		       chc5camio_us_to_units(c->strobe_delay_us));
	r |= camio_set(g_fd, CHC5CAMIO_CID_STROBE_DURATION,
		       chc5camio_us_to_units(c->strobe_duration_us));
	r |= camio_set(g_fd, CHC5CAMIO_CID_STROBE_MINON,
		       chc5camio_us_to_units(c->strobe_minon_us));
	r |= camio_set(g_fd, CHC5CAMIO_CID_STROBE_ENABLE,    c->strobe_enable);

	r |= camio_set(g_fd, CHC5CAMIO_CID_OUT_SOURCE,     c->out_source);
	r |= camio_set(g_fd, CHC5CAMIO_CID_OUT_INVERT,     c->out_invert);
	r |= camio_set(g_fd, CHC5CAMIO_CID_OUT_USER_VALUE, c->out_user_value);

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

int camio_sync_read_status(uint32_t *status, uint32_t *in_status,
			   uint32_t *sync_status)
{
	int32_t v;

	if (g_fd < 0 && camio_sync_open() != 0)
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
