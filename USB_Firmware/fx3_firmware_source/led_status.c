/*
 * led_status.c - Status LED thread (green GPIO20 PWM / red GPIO60)
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */
#include "led_status.h"

#if LED_STATUS_ENABLED

#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3error.h"

#define LED_EVT_POKE            (1u << 0)

static CyU3PEvent   glLedEvent;
static CyBool_t     glLedEventOk  = CyFalse;
static CyBool_t     glLedReady    = CyFalse;

volatile CyBool_t   glLedFatal     = CyFalse;
volatile CyBool_t   glLedConnected = CyFalse;

static led_color_t   s_cur_color = LED_C_OFF;
static led_pattern_t s_cur_pat   = LED_PAT_SOLID;
static CyBool_t      s_cur_valid = CyFalse;
static int           s_red_level = -1;
static int           s_grn_static = -1;

static uint32_t      s_sleep_ms  = 0;


void led_state_appearance(led_state_t st, CyBool_t degraded,
                          led_color_t *c, led_pattern_t *p)
{
    switch (st) {
    case LED_ST_FATAL:     *c = LED_C_RED;   *p = LED_PAT_BLINK_FAST;   break;
    case LED_ST_FAULT:     *c = LED_C_RED;   *p = LED_PAT_DOUBLE_BLINK; break;
    case LED_ST_NO_FRAMES: *c = LED_C_RED;   *p = LED_PAT_BLINK_SLOW;   break;
    case LED_ST_STREAMING: *c = LED_C_GREEN; *p = LED_PAT_SOLID;        break;
    case LED_ST_READY:     *c = LED_C_GREEN; *p = LED_PAT_SOLID;        break;
    case LED_ST_NOHOST:    *c = LED_C_AMBER; *p = LED_PAT_ALT_AG;      break;
    case LED_ST_BOOT:      *c = LED_BOOT_COLOR; *p = LED_PAT_BLINK_FAST; break;
    default:               *c = LED_C_OFF;  *p = LED_PAT_SOLID;         break;
    }

    if (degraded && *c == LED_C_GREEN)
        *c = LED_C_AMBER;
}


static int pat_on(led_pattern_t p, uint32_t t_ms)
{
    switch (p) {
    case LED_PAT_BLINK_SLOW:
        return ((t_ms / LED_BLINK_SLOW_MS) & 1u) ? 0 : 1;
    case LED_PAT_BLINK_FAST:
        return ((t_ms / LED_BLINK_FAST_MS) & 1u) ? 0 : 1;
    case LED_PAT_STROBE:
        return ((t_ms % LED_STROBE_PERIOD_MS) < LED_STROBE_ON_MS) ? 1 : 0;
    case LED_PAT_DOUBLE_BLINK: {
        uint32_t ph = t_ms % LED_DBLINK_PERIOD_MS;
        uint32_t p2 = LED_DBLINK_PULSE_MS + LED_DBLINK_GAP_MS;
        return ((ph < LED_DBLINK_PULSE_MS) ||
                (ph >= p2 && ph < p2 + LED_DBLINK_PULSE_MS)) ? 1 : 0;
    }
    case LED_PAT_SOLID:
    default:
        return 1;
    }
}

static uint32_t pat_sleep_ms(led_pattern_t p)
{
    switch (p) {
    case LED_PAT_BLINK_SLOW:   return LED_BLINK_SLOW_MS;
    case LED_PAT_BLINK_FAST:   return LED_BLINK_FAST_MS;
    case LED_PAT_DOUBLE_BLINK: return LED_DBLINK_PULSE_MS;
    case LED_PAT_STROBE:       return LED_STROBE_ON_MS;
    case LED_PAT_ALT_AG:       return LED_ALT_SLOT_MS;
    case LED_PAT_SOLID:
    default:                   return 0u;
    }
}


static void red_write(int on)
{
    CyBool_t lvl;

    if (!glLedReady || s_red_level == on)
        return;
    s_red_level = on;
#if LED_ACTIVE_HIGH
    lvl = on ? CyTrue : CyFalse;
#else
    lvl = on ? CyFalse : CyTrue;
#endif
    CyU3PGpioSimpleSetValue(LED_GPIO_RED, lvl);
}

static void green_static(int on)
{
    CyU3PGpioComplexConfig_t cfg;
    CyBool_t lvl;

    if (!glLedReady || s_grn_static == on)
        return;
    s_grn_static = on;
#if LED_ACTIVE_HIGH
    lvl = on ? CyTrue : CyFalse;
#else
    lvl = on ? CyFalse : CyTrue;
#endif

    CyU3PMemSet((uint8_t *)&cfg, 0, sizeof(cfg));
    cfg.outValue    = lvl;
    cfg.driveLowEn  = CyTrue;
    cfg.driveHighEn = CyTrue;
    cfg.inputEn     = CyFalse;
    cfg.pinMode     = CY_U3P_GPIO_MODE_STATIC;
    cfg.intrMode    = CY_U3P_GPIO_NO_INTR;
    cfg.timerMode   = CY_U3P_GPIO_TIMER_LOW_FREQ;
    cfg.timer       = 0;
    cfg.period      = 0;
    cfg.threshold   = 0;
    CyU3PGpioSetComplexConfig(LED_GPIO_GREEN, &cfg);
}

static void green_hw_pattern(led_pattern_t p)
{
    CyU3PGpioComplexConfig_t cfg;
    uint32_t period_ms, on_ms;

    if (!glLedReady)
        return;

    switch (p) {
    case LED_PAT_BLINK_SLOW: period_ms = LED_BLINK_SLOW_MS * 2u;
                             on_ms     = LED_BLINK_SLOW_MS;        break;
    case LED_PAT_BLINK_FAST: period_ms = LED_BLINK_FAST_MS * 2u;
                             on_ms     = LED_BLINK_FAST_MS;        break;
    case LED_PAT_STROBE:     period_ms = LED_STROBE_PERIOD_MS;
                             on_ms     = LED_STROBE_ON_MS;         break;
    case LED_PAT_DOUBLE_BLINK:
    case LED_PAT_SOLID:
    default:                 green_static(1); return;
    }

    s_grn_static = -1;

    CyU3PMemSet((uint8_t *)&cfg, 0, sizeof(cfg));
    cfg.outValue    = CyFalse;
    cfg.driveLowEn  = CyTrue;
    cfg.driveHighEn = CyTrue;
    cfg.inputEn     = CyFalse;
    cfg.pinMode     = CY_U3P_GPIO_MODE_PWM;
    cfg.intrMode    = CY_U3P_GPIO_NO_INTR;
    cfg.timerMode   = CY_U3P_GPIO_TIMER_LOW_FREQ;
    cfg.timer       = 0;
    cfg.period      = LED_PWM_TICKS(period_ms) - 1u;
#if LED_ACTIVE_HIGH
    cfg.threshold   = LED_PWM_TICKS(on_ms) - 1u;
#else
    cfg.threshold   = LED_PWM_TICKS(period_ms - on_ms) - 1u;
#endif
    CyU3PGpioSetComplexConfig(LED_GPIO_GREEN, &cfg);
}


void led_status_init(void)
{
    CyU3PGpioSimpleConfig_t scfg;
    CyU3PReturnStatus_t st;

    st = CyU3PDeviceGpioOverride(LED_GPIO_GREEN, CyFalse);
    if (st != CY_U3P_SUCCESS) {
        return;
    }

    CyU3PMemSet((uint8_t *)&scfg, 0, sizeof(scfg));
#if LED_ACTIVE_HIGH
    scfg.outValue = CyFalse;
#else
    scfg.outValue = CyTrue;
#endif
    scfg.driveLowEn  = CyTrue;
    scfg.driveHighEn = CyTrue;
    scfg.inputEn     = CyFalse;
    scfg.intrMode    = CY_U3P_GPIO_NO_INTR;
    st = CyU3PGpioSetSimpleConfig(LED_GPIO_RED, &scfg);
    if (st != CY_U3P_SUCCESS)
        return;

    glLedReady = CyTrue;

    s_red_level  = -1;
    s_grn_static = -1;
    green_static((LED_BOOT_COLOR == LED_C_GREEN ||
                  LED_BOOT_COLOR == LED_C_AMBER) ? 1 : 0);
    red_write((LED_BOOT_COLOR == LED_C_RED ||
               LED_BOOT_COLOR == LED_C_AMBER) ? 1 : 0);
    s_cur_color = LED_BOOT_COLOR;
    s_cur_pat   = LED_PAT_SOLID;
    s_cur_valid = CyTrue;
    s_sleep_ms  = 0;
}


void led_status_fatal(void)
{
    glLedFatal = CyTrue;
    led_status_poke();
}

void led_status_set_connected(CyBool_t on)
{
    glLedConnected = on;
    led_status_poke();
}

void led_status_poke(void)
{
    if (glLedEventOk)
        CyU3PEventSet(&glLedEvent, LED_EVT_POKE, CYU3P_EVENT_OR);
}


static void led_render(uint32_t t_ms)
{
    led_state_t   st;
    led_color_t   c;
    led_pattern_t p;
    CyBool_t      degraded = CyFalse;
    CyBool_t      changed;

    st = led_state_source(&degraded);
    led_state_appearance(st, degraded, &c, &p);

    changed = (!s_cur_valid || c != s_cur_color || p != s_cur_pat);
    s_cur_color = c;
    s_cur_pat   = p;
    s_cur_valid = CyTrue;

    if (c == LED_C_GREEN || c == LED_C_OFF) {
        if (changed) {
            red_write(0);
            if (c == LED_C_OFF)
                green_static(0);
            else
                green_hw_pattern(p);
        }
        s_sleep_ms = 0;
        return;
    }

    if (changed && c == LED_C_RED)
        green_static(0);

    if (p == LED_PAT_ALT_AG) {
        uint32_t slot = (t_ms / LED_ALT_SLOT_MS) & 3u;
        switch (slot) {
        case 1:  green_static(1); red_write(1); break;
        case 3:  green_static(1); red_write(0); break;
        default: green_static(0); red_write(0); break;
        }
    } else {
        int on = pat_on(p, t_ms);
        if (c == LED_C_AMBER) {
            green_static(on);
            red_write(on);
        } else {
            red_write(on);
        }
    }
    s_sleep_ms = pat_sleep_ms(p);
}

void led_status_thread_entry(uint32_t input)
{
    uint32_t t_ms = 0;

    (void)input;

    if (CyU3PEventCreate(&glLedEvent) == CY_U3P_SUCCESS)
        glLedEventOk = CyTrue;

    for (;;) {
        uint32_t flag = 0;
        uint32_t sleep_ms = s_sleep_ms;

        CyU3PEventGet(&glLedEvent, LED_EVT_POKE, CYU3P_EVENT_OR_CLEAR, &flag,
                      sleep_ms ? sleep_ms : CYU3P_WAIT_FOREVER);

        t_ms += sleep_ms;

        led_render(t_ms);
    }
}

#endif
