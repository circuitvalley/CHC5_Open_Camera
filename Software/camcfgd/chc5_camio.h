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
       CHC5CAMIO_OUT_EXPACTIVE = 3, CHC5CAMIO_OUT_STATUS = 4,
       CHC5CAMIO_OUT_SYNCOUT = 5, CHC5CAMIO_OUT_PASSTHROUGH = 6,
       CHC5CAMIO_OUT_PTP_PULSE = 7  };
enum { CHC5CAMIO_ROLE_OFF = 0, CHC5CAMIO_ROLE_MASTER = 1, CHC5CAMIO_ROLE_SLAVE = 2 };
enum { CHC5CAMIO_ROLEBY_PIN = 0, CHC5CAMIO_ROLEBY_I2C = 1 };
enum { CHC5CAMIO_TRIGROUTE_XTRIG = 0, CHC5CAMIO_TRIGROUTE_XVS_SLAVE = 1,
       CHC5CAMIO_TRIGROUTE_I2C = 2 };

#define CHC5CAMIO_ST_SYNC_LOCKED     (1u << 0)
#define CHC5CAMIO_ST_SLAVE_DETECTED  (1u << 1)
#define CHC5CAMIO_ST_BUSY            (1u << 2)
#define CHC5CAMIO_ST_TRIG_OVERRUN    (1u << 3)
#define CHC5CAMIO_ST_LINE_LEVELS_SHIFT 4

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

struct camio_cfg {
	uint8_t  present;

	uint8_t  enable;

	uint8_t  in_function;
	uint8_t  in_activation;
	uint8_t  in_invert;
	uint32_t trigger_delay_us;
	uint16_t trigger_divider;

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

#endif
