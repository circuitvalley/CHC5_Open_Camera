/*
 * led_status.h - Status LED interface and GPIO mapping
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */
#ifndef _INCLUDED_LED_STATUS_H_
#define _INCLUDED_LED_STATUS_H_

#include "cyu3types.h"
#include "cyu3gpio.h"

#define LED_STATUS_ENABLED       1

#define LED_GPIO_GREEN          20
#define LED_GPIO_RED            60
#define LED_ACTIVE_HIGH          1
#define LED_BICOLOR_EXCLUSIVE    0

typedef enum { LED_C_OFF = 0, LED_C_RED, LED_C_GREEN, LED_C_AMBER } led_color_t;

#define LED_BOOT_COLOR   LED_C_GREEN

#define LED_BLINK_SLOW_MS       800
#define LED_BLINK_FAST_MS       200
#define LED_STROBE_ON_MS        100
#define LED_STROBE_PERIOD_MS    500
#define LED_DBLINK_PERIOD_MS   1200
#define LED_DBLINK_PULSE_MS     200
#define LED_DBLINK_GAP_MS       200

#define LED_ALT_SLOT_MS         500

typedef enum { LED_PAT_SOLID = 0, LED_PAT_BLINK_SLOW, LED_PAT_BLINK_FAST,
               LED_PAT_DOUBLE_BLINK, LED_PAT_STROBE,
               LED_PAT_ALT_AG } led_pattern_t;

typedef enum {
    LED_ST_FATAL = 0,
    LED_ST_FAULT,
    LED_ST_NO_FRAMES,
    LED_ST_STREAMING,
    LED_ST_READY,
    LED_ST_NOHOST,
    LED_ST_BOOT,
    LED_ST__COUNT
} led_state_t;

#define LED_SYS_CLK_HZ          403200000u
#define LED_GPIO_FAST_DIV       2u
#define LED_GPIO_SLOW_DIV       2u
#define LED_PWM_HZ              (LED_SYS_CLK_HZ / LED_GPIO_FAST_DIV / LED_GPIO_SLOW_DIV)
#define LED_PWM_TICKS(ms)       ((uint32_t)(((uint64_t)(ms) * LED_PWM_HZ) / 1000u))

void led_status_init(void);

extern volatile CyBool_t glLedFatal;
extern volatile CyBool_t glLedConnected;

void led_status_fatal(void);
void led_status_set_connected(CyBool_t on);
void led_status_poke(void);

void led_status_thread_entry(uint32_t input);

led_state_t led_state_source(CyBool_t *link_degraded_out);

void led_state_appearance(led_state_t st, CyBool_t degraded,
                          led_color_t *c, led_pattern_t *p);

#endif
