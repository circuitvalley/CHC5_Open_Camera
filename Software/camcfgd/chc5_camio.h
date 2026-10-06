/* SPDX-License-Identifier: CC-BY-NC-ND-4.0 */
/*
 * chc5_camio.h - chc5_camio_sync aux I/O and sync interface
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#ifndef CHC5_CAMIO_H
#define CHC5_CAMIO_H

#include <stdint.h>
#include "chc5-v4l2-controls.h"

enum { CHC5CAMIO_IN_OFF = 0, CHC5CAMIO_IN_TRIGGER = 1, CHC5CAMIO_IN_SYNCIN = 2 };
enum { CHC5CAMIO_ACT_RISING = 0, CHC5CAMIO_ACT_FALLING = 1,
       CHC5CAMIO_ACT_ANY = 2, CHC5CAMIO_ACT_LEVEL = 3 };
enum { CHC5CAMIO_STRB_SRC_PL = 0, CHC5CAMIO_STRB_SRC_NATIVE = 1 };
enum { CHC5CAMIO_OUT_OFF = 0, CHC5CAMIO_OUT_USER = 1, CHC5CAMIO_OUT_STROBE = 2,
       CHC5CAMIO_OUT_EXPACTIVE = 3, CHC5CAMIO_OUT_STATUS = 4 ,
       CHC5CAMIO_OUT_SYNCOUT = 5, CHC5CAMIO_OUT_PASSTHROUGH = 6,
       CHC5CAMIO_OUT_PTP_PULSE = 7  };
enum { CHC5CAMIO_ROLE_OFF = 0, CHC5CAMIO_ROLE_MASTER = 1, CHC5CAMIO_ROLE_SLAVE = 2,
       CHC5CAMIO_ROLE_PTP = 3 };
enum { CHC5CAMIO_ROLEBY_PIN = 0, CHC5CAMIO_ROLEBY_I2C = 1 };
enum { CHC5CAMIO_TRIG_SRC_LINE0 = 0, CHC5CAMIO_TRIG_SRC_SOFTWARE = 1,
       CHC5CAMIO_TRIG_SRC_PTP = 2, CHC5CAMIO_TRIG_SRC_ACTION = 3 };

enum { CHC5_SYNC_FREE_RUN = 0, CHC5_SYNC_LINE0 = 1, CHC5_SYNC_SOFTWARE = 2,
       CHC5_SYNC_ACTION = 3, CHC5_SYNC_PTP = 4, CHC5_SYNC_GENLOCK = 5 };
enum { CHC5CAMIO_TRIGROUTE_XTRIG = 0, CHC5CAMIO_TRIGROUTE_XVS_SLAVE = 1,
       CHC5CAMIO_TRIGROUTE_I2C = 2 };

#define CHC5CAMIO_ST_SYNC_LOCKED     (1u << 0)
#define CHC5CAMIO_ST_SLAVE_DETECTED  (1u << 1)
#define CHC5CAMIO_ST_BUSY            (1u << 2)
#define CHC5CAMIO_ST_TRIG_OVERRUN    (1u << 3)
#define CHC5CAMIO_ST_LINE_LEVELS_SHIFT 4
#define CHC5CAMIO_SYNC_STATUS_BITS   0x0Fu

#define CHC5CAMIO_TICK_US            10u
#define CHC5CAMIO_TIME_MAX_UNITS     0xFFFFu

static inline uint32_t chc5camio_us_to_units(uint32_t us)
{
	uint32_t u = (us + CHC5CAMIO_TICK_US / 2) / CHC5CAMIO_TICK_US;
	return u > CHC5CAMIO_TIME_MAX_UNITS ? CHC5CAMIO_TIME_MAX_UNITS : u;
}
static inline uint32_t chc5camio_units_to_us(uint32_t units)
{
	return units * CHC5CAMIO_TICK_US;
}
static inline uint64_t chc5camio_mhz_to_ps(uint32_t mhz)
{
	return mhz ? (1000000000000000ull + mhz / 2) / mhz : 0;
}
static inline uint32_t chc5camio_ps_to_mhz(uint64_t ps)
{
	return ps ? (uint32_t)((1000000000000000ull + ps / 2) / ps) : 0;
}

static inline uint32_t chc5camio_us_to_ns(uint32_t us)
{
	return us > 0x7FFFFFFFu / 1000u ? 0x7FFFFFFFu : us * 1000u;
}

#define GIGE_REG_LINE_SELECTOR      0xE000
#define GIGE_REG_LINE_MODE          0xE004
#define GIGE_REG_LINE_INVERTER      0xE008
#define GIGE_REG_LINE_STATUS        0xE00C
#define GIGE_REG_LINE_STATUS_ALL    0xE010
#define GIGE_REG_LINE_SOURCE        0xE014
#define GIGE_REG_USER_OUTPUT_VALUE  0xE018
#define GIGE_REG_TRIGGER_MODE       0xE020
#define GIGE_REG_TRIGGER_SOURCE     0xE024
#define GIGE_REG_TRIGGER_ACTIVATION 0xE028
#define GIGE_REG_TRIGGER_DIVIDER    0xE02C
#define GIGE_REG_TRIGGER_DELAY      0xE030
#define GIGE_REG_STROBE_ENABLE      0xE040
#define GIGE_REG_STROBE_SOURCE      0xE044
#define GIGE_REG_STROBE_DELAY       0xE050
#define GIGE_REG_STROBE_DURATION    0xE060
#define GIGE_REG_STROBE_MINON       0xE068
#define GIGE_REG_SYNC_ROLE          0xE070
#define GIGE_REG_SYNC_STATUS        0xE074
#define GIGE_REG_SFR_ENABLE         0xE080
#define GIGE_REG_SFR_START_LOW      0xE084
#define GIGE_REG_SFR_START_HIGH     0xE088
#define GIGE_REG_SFR_STATUS         0xE08C
#define GIGE_REG_SFR_RATE           0xE090
#define GIGE_REG_SFR_UPDATE         0xE098
#define GIGE_REG_LINE_DEBOUNCER     0xE0A0
#define GIGE_REG_IO_CAPS            0xE0FC
#define GIGE_REG_EXPOSURE_MODE      0xC030
#define GIGE_REG_TRIGGER_SOFTWARE   0xC058
enum { GENICAM_EXPMODE_TIMED = 1, GENICAM_EXPMODE_TRIGGER_WIDTH = 2 };
enum { GENICAM_TRIGSRC_SOFTWARE = 0, GENICAM_TRIGSRC_LINE0 = 1,
       GENICAM_TRIGSRC_ACTION1 = 2 };

struct camio_cfg {
	uint8_t  present;

	uint8_t  in_function;
	uint8_t  in_activation;
	uint8_t  in_invert;
	uint32_t trigger_delay_ns;
	uint16_t trigger_divider;
	uint8_t  trig_src;
	uint8_t  width_mode;
	uint32_t trig_minper_ns;
	uint32_t in_filter_ns;
	uint8_t  sched_enable;
	uint64_t sched_period_ps;
	uint64_t sched_start_ns;

	uint8_t  strobe_enable;
	uint8_t  strobe_invert;
	uint8_t  strobe_src;
	uint32_t strobe_delay_us;
	uint32_t strobe_duration_us;
	uint32_t strobe_minon_us;

	uint8_t  out_source;
	uint8_t  out_invert;
	uint8_t  out_user_value;

	uint8_t  sync_role;
	uint8_t  sync_role_by;
	uint8_t  sync_trig_route;
};

#define CAMIO_CFG_FIELDS(X) \
	X(IN_FUNCTION,   in_function)      X(IN_ACTIVATION,  in_activation) \
	X(IN_INVERT,     in_invert)        X(TRIG_DELAY,     trigger_delay_ns) \
	X(TRIG_DIVIDER,  trigger_divider)  X(TRIG_SRC,       trig_src) \
	X(WIDTH_MODE,    width_mode)       X(TRIG_MINPER,    trig_minper_ns) \
	X(IN_FILTER,     in_filter_ns)     X(SCHED_ENABLE,   sched_enable) \
	X(SCHED_PERIOD,  sched_period_ps)  X(SCHED_START,    sched_start_ns) \
	X(STROBE_ENABLE, strobe_enable)    X(STROBE_INVERT,  strobe_invert) \
	X(STROBE_SRC,    strobe_src)       X(STROBE_DELAY,   strobe_delay_us) \
	X(STROBE_DUR,    strobe_duration_us) X(STROBE_MINON, strobe_minon_us) \
	X(OUT_SOURCE,    out_source)       X(OUT_INVERT,     out_invert) \
	X(OUT_USER,      out_user_value)   X(SYNC_ROLE,      sync_role) \
	X(SYNC_ROLE_BY,  sync_role_by)     X(SYNC_TRIG_ROUTE, sync_trig_route)

enum {
#define CAMIO_FB_ENUM(n, f) CAMIO_FB_##n,
	CAMIO_CFG_FIELDS(CAMIO_FB_ENUM)
#undef CAMIO_FB_ENUM
	CAMIO_FB_COUNT
};
#define CAMIO_F(n)	(1u << CAMIO_FB_##n)
#define CAMIO_F_ALL	((1u << CAMIO_FB_COUNT) - 1u)
_Static_assert(CAMIO_FB_COUNT <= 32, "camio field mask is 32 bits");

static inline uint32_t camio_cfg_diff(const struct camio_cfg *a, const struct camio_cfg *b)
{
	uint32_t m = 0;
#define CAMIO_DIFF(n, f) if (a->f != b->f) m |= CAMIO_F(n);
	CAMIO_CFG_FIELDS(CAMIO_DIFF)
#undef CAMIO_DIFF
	return m;
}

static inline void camio_cfg_merge(struct camio_cfg *dst, const struct camio_cfg *src, uint32_t mask)
{
#define CAMIO_MERGE(n, f) if (mask & CAMIO_F(n)) dst->f = src->f;
	CAMIO_CFG_FIELDS(CAMIO_MERGE)
#undef CAMIO_MERGE
}

#endif
