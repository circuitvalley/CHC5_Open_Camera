// SPDX-License-Identifier: CC-BY-NC-ND-4.0
/*
 * genconv.c - GenICam <-> sensor unit conversions
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#include <math.h>
#include <string.h>
#include <arpa/inet.h>

#include "genconv.h"
#include "chc5_camio.h"
#include "camio_sync.h"
#include "config.h"
#include "media_pipeline.h"

uint8_t genconv_pfnc_to_bits(uint32_t pfnc)
{
    uint8_t bpp = (pfnc >> 16) & 0xFF;
    switch (bpp) {
    case 0x08: return 8;
    case 0x0A: return 10;
    case 0x0C: return 12;
    case 0x0E: return 14;
    case 0x10: return 16;
    case 0x18: return 24;
    case 0x20: return 32;
    default:   return 0;
    }
}

#define MEDIA_BUS_FMT_SRGGB8_1X8       0x3014
#define MEDIA_BUS_FMT_SRGGB10_1X10     0x300F
#define MEDIA_BUS_FMT_SRGGB12_1X12     0x3012
#define MEDIA_BUS_FMT_SRGGB14_1X14     0x301C
#define MEDIA_BUS_FMT_RBG888_1X24      0x100E

#define MEDIA_BUS_FMT_SGBRG10_1X10     0x300e
#define MEDIA_BUS_FMT_SGBRG12_1X12     0x3010
#define MEDIA_BUS_FMT_SGBRG14_1X14     0x301a
#define MEDIA_BUS_FMT_SGBRG16_1X16     0x301e
#define MEDIA_BUS_FMT_RGB565_1X16      0x1017
#define MEDIA_BUS_FMT_YUYV8_1X16      0x2011
#define MEDIA_BUS_FMT_Y8_1X8          0x2001
#define MEDIA_BUS_FMT_Y12_1X12        0x2013
#define MEDIA_BUS_FMT_Y16_1X16        0x202e
#define MEDIA_BUS_FMT_RGB888_1X32_PADHI 0x100f
#define MEDIA_BUS_FMT_BGR888_1X24     0x1013

uint32_t genconv_pfnc_to_pixpack_mbus(uint32_t pfnc)
{
    switch (pfnc) {
    case GVSP_PIX_BAYERRG8:
    case GVSP_PIX_BAYERBG8:
    case GVSP_PIX_BAYERGR8:
    case GVSP_PIX_BAYERGB8:      return MEDIA_BUS_FMT_SRGGB8_1X8;
    case GVSP_PIX_BAYERRG10P:
    case GVSP_PIX_BAYERBG10P:
    case GVSP_PIX_BAYERGR10P:
    case GVSP_PIX_BAYERGB10P:    return MEDIA_BUS_FMT_SRGGB10_1X10;
    case GVSP_PIX_BAYERRG10:
    case GVSP_PIX_BAYERBG10:
    case GVSP_PIX_BAYERGR10:
    case GVSP_PIX_BAYERGB10:     return MEDIA_BUS_FMT_SGBRG10_1X10;
    case GVSP_PIX_BAYERRG12P:
    case GVSP_PIX_BAYERBG12P:
    case GVSP_PIX_BAYERGR12P:
    case GVSP_PIX_BAYERGB12P:    return MEDIA_BUS_FMT_SRGGB12_1X12;
    case GVSP_PIX_BAYERRG12:
    case GVSP_PIX_BAYERBG12:
    case GVSP_PIX_BAYERGR12:
    case GVSP_PIX_BAYERGB12:     return MEDIA_BUS_FMT_SGBRG12_1X12;
    case GVSP_PIX_BAYERRG14P:
    case GVSP_PIX_BAYERBG14P:
    case GVSP_PIX_BAYERGR14P:
    case GVSP_PIX_BAYERGB14P:    return MEDIA_BUS_FMT_SRGGB14_1X14;
    case GVSP_PIX_BAYERRG14:
    case GVSP_PIX_BAYERBG14:
    case GVSP_PIX_BAYERGR14:
    case GVSP_PIX_BAYERGB14:     return MEDIA_BUS_FMT_SGBRG14_1X14;
    case GVSP_PIX_BAYERRG16:
    case GVSP_PIX_BAYERBG16:
    case GVSP_PIX_BAYERGR16:
    case GVSP_PIX_BAYERGB16:     return MEDIA_BUS_FMT_SGBRG16_1X16;
    case GVSP_PIX_RGB565P:       return MEDIA_BUS_FMT_RGB565_1X16;
    case GVSP_PIX_YUV422_8:      return MEDIA_BUS_FMT_YUYV8_1X16;
    case GVSP_PIX_YCBCR422_8:    return MEDIA_BUS_FMT_YUYV8_1X16;
    case GVSP_PIX_MONO8:         return MEDIA_BUS_FMT_Y8_1X8;
    case GVSP_PIX_MONO12P:       return MEDIA_BUS_FMT_Y12_1X12;
    case GVSP_PIX_MONO12:        return MEDIA_BUS_FMT_Y16_1X16;
    case GVSP_PIX_BGRA8:         return MEDIA_BUS_FMT_RGB888_1X32_PADHI;
    case GVSP_PIX_BGR8:          return MEDIA_BUS_FMT_BGR888_1X24;
    default:                     return 0;
    }
}

static double read_be_f64(const uint8_t *p)
{
    union { double d; uint64_t u; } conv;
    conv.u = ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
             ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
             ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
             ((uint64_t)p[6] <<  8) | ((uint64_t)p[7]);
    return conv.d;
}

static uint32_t read_be_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) | ((uint32_t)p[3]);
}

static uint32_t g_wb_ratio_sel = 0;

static struct camio_cfg g_camio_acc;
static uint32_t         g_camio_line_sel;

static void camio_push(void)
{
    g_camio_acc.present = 1;
    config_set_camio(&g_camio_acc);
}

#define CAMIO_DIVIDER_MAX  0xFFu
#define CAMIO_US_CEIL      (CHC5CAMIO_TIME_MAX_UNITS * CHC5CAMIO_TICK_US)

static int camio_enum_ok(const char *name, uint32_t v, uint32_t max)
{
    if (v <= max) return 1;
    LOG_WARN("camio: %s=%u out of range (max %u) -- write ignored", name, v, max);
    return 0;
}

static uint32_t camio_us_clamp(double us)
{
    if (us > (double)CAMIO_US_CEIL) return CAMIO_US_CEIL;
    return (uint32_t)(us + 0.5);
}

static int genconv_camio(uint32_t addr, const uint8_t *value, int len)
{
    uint32_t v;

    switch (addr) {
    case GIGE_REG_LINE_SELECTOR:
        if (len >= 4) g_camio_line_sel = read_be_u32(value);
        return 0;
    case GIGE_REG_LINE_INVERTER:
        if (len >= 4) {
            if (g_camio_line_sel == 1) g_camio_acc.out_invert = read_be_u32(value) ? 1 : 0;
            else                       g_camio_acc.in_invert  = read_be_u32(value) ? 1 : 0;
            camio_push();
        }
        return 0;
    case GIGE_REG_LINE_SOURCE:
        if (len >= 4 && camio_enum_ok("LineSource", v = read_be_u32(value),
                                      camio_sync_has_ptp_pulse() == 1
                                          ? CHC5CAMIO_OUT_PTP_PULSE
                                          : CHC5CAMIO_OUT_PASSTHROUGH)) {
            g_camio_acc.out_source = (uint8_t)v; camio_push();
        }
        return 0;
    case GIGE_REG_USER_OUTPUT_VALUE:
        if (len >= 4) { g_camio_acc.out_user_value = read_be_u32(value) ? 1 : 0; camio_push(); }
        return 0;
    case GIGE_REG_TRIGGER_MODE:
        if (len >= 4) {
            g_camio_acc.in_function = read_be_u32(value) ? CHC5CAMIO_IN_TRIGGER
                                                         : CHC5CAMIO_IN_OFF;
            camio_push();
        }
        return 0;
    case GIGE_REG_TRIGGER_SOURCE:
        if (len >= 4 && read_be_u32(value) == 1) {
            g_camio_acc.in_function = CHC5CAMIO_IN_TRIGGER;
            camio_push();
        }
        return 0;
    case GIGE_REG_TRIGGER_ACTIVATION:
        if (len >= 4 && camio_enum_ok("TriggerActivation", v = read_be_u32(value),
                                      CHC5CAMIO_ACT_LEVEL)) {
            g_camio_acc.in_activation = (uint8_t)v; camio_push();
        }
        return 0;
    case GIGE_REG_TRIGGER_DELAY:
        if (len >= 8) {
            double us = read_be_f64(value);
            if (isfinite(us) && us >= 0.0) { g_camio_acc.trigger_delay_us = camio_us_clamp(us); camio_push(); }
        }
        return 0;
    case GIGE_REG_TRIGGER_DIVIDER:
        if (len >= 4 && camio_enum_ok("TriggerDivider", v = read_be_u32(value),
                                      CAMIO_DIVIDER_MAX)) {
            g_camio_acc.trigger_divider = (uint16_t)v; camio_push();
        }
        return 0;
    case GIGE_REG_STROBE_ENABLE:
        if (len >= 4) { g_camio_acc.strobe_enable = read_be_u32(value) ? 1 : 0; camio_push(); }
        return 0;
    case GIGE_REG_STROBE_SOURCE:
        if (len >= 4 && camio_enum_ok("StrobeSource", v = read_be_u32(value),
                                      CHC5CAMIO_STRB_SRC_NATIVE)) {
            g_camio_acc.strobe_src = (uint8_t)v; camio_push();
        }
        return 0;
    case GIGE_REG_STROBE_DELAY:
        if (len >= 8) {
            double us = read_be_f64(value);
            if (isfinite(us) && us >= 0.0) { g_camio_acc.strobe_delay_us = camio_us_clamp(us); camio_push(); }
        }
        return 0;
    case GIGE_REG_STROBE_DURATION:
        if (len >= 8) {
            double us = read_be_f64(value);
            if (isfinite(us) && us >= 0.0) { g_camio_acc.strobe_duration_us = camio_us_clamp(us); camio_push(); }
        }
        return 0;
    case GIGE_REG_STROBE_MINON:
        if (len >= 8) {
            double us = read_be_f64(value);
            if (isfinite(us) && us >= 0.0) { g_camio_acc.strobe_minon_us = camio_us_clamp(us); camio_push(); }
        }
        return 0;
    case GIGE_REG_SYNC_ROLE:
        if (len >= 4 && camio_enum_ok("SyncRole", v = read_be_u32(value),
                                      CHC5CAMIO_ROLE_SLAVE)) {
            g_camio_acc.sync_role = (uint8_t)v; camio_push();
        }
        return 0;
    default:
        return 0;
    }
}

static uint8_t  g_gige_gamma_en   = 1;
static uint8_t  g_gige_gamma_sel  = 2;
static uint16_t g_gige_gamma_x100 = 100;

static void gige_gamma_apply(void)
{
    config_set_gamma_x100(!g_gige_gamma_en       ? CHC5_GAMMA_OFF  :
                          g_gige_gamma_sel == 2  ? CHC5_GAMMA_SRGB :
                                                   g_gige_gamma_x100);
}

int genconv_apply_register(uint32_t addr, const uint8_t *value, int len)
{
    imgsensor_cfg_t cur;

    if (addr >= 0xE000u && addr <= 0xE0FFu)
        return genconv_camio(addr, value, len);

    config_get_sensor(&cur);

    switch (addr) {

    case GIGE_REG_WIDTH:
        if (len >= 4) {
            uint16_t w = (uint16_t)read_be_u32(value);
            config_set_resolution(w, cur.height);
            LOG_INFO("genconv: Width -> %u", w);
        }
        return 0;

    case GIGE_REG_HEIGHT:
        if (len >= 4) {
            uint16_t h = (uint16_t)read_be_u32(value);
            config_set_resolution(cur.width, h);
            LOG_INFO("genconv: Height -> %u", h);
        }
        return 0;

    case GIGE_REG_OFFSET_X:
        if (len >= 4) {
            uint16_t ox = (uint16_t)read_be_u32(value);
            config_set_offset(ox, cur.offset_y);
            LOG_INFO("genconv: OffsetX -> %u", ox);
        }
        return 0;

    case GIGE_REG_OFFSET_Y:
        if (len >= 4) {
            uint16_t oy = (uint16_t)read_be_u32(value);
            config_set_offset(cur.offset_x, oy);
            LOG_INFO("genconv: OffsetY -> %u", oy);
        }
        return 0;

    case GIGE_REG_EXPOSURE_TIME:
        if (len >= 8) {
            double exp_us = read_be_f64(value);
            if (!isfinite(exp_us)) { LOG_WARN("genconv: ExposureTime not finite, ignored"); return 0; }
            if (exp_us < 1.0)       exp_us = 1.0;
            if (exp_us > (double)EXPOSURE_US_MAX) exp_us = (double)EXPOSURE_US_MAX;
            config_set_exposure_us((uint32_t)(exp_us + 0.5));
            LOG_INFO("genconv: ExposureTime %.1f us", exp_us);
        }
        return 0;

    case GIGE_REG_AE_EXP_LOWER:
        if (len >= 8) {
            double us = read_be_f64(value);
            if (!isfinite(us)) { LOG_WARN("genconv: AE lower limit not finite, ignored"); return 0; }
            if (us < 0.0)       us = 0.0;
            if (us > 1000000.0) us = 1000000.0;
            config_set_ae_exp_lower_us((uint32_t)(us + 0.5));
            LOG_INFO("genconv: AutoExposureTimeLowerLimit %.1f us", us);
        }
        return 0;

    case GIGE_REG_AE_EXP_UPPER:
        if (len >= 8) {
            double us = read_be_f64(value);
            if (!isfinite(us)) { LOG_WARN("genconv: AE upper limit not finite, ignored"); return 0; }
            if (us < 0.0)       us = 0.0;
            if (us > 1000000.0) us = 1000000.0;
            config_set_ae_exp_upper_us((uint32_t)(us + 0.5));
            LOG_INFO("genconv: AutoExposureTimeUpperLimit %.1f us", us);
        }
        return 0;

    case GIGE_REG_GAIN:
        if (len >= 8) {
            double gain_db = read_be_f64(value);
            double gmax    = media_pipeline_get_gain_db_max();
            if (gain_db < 0.0)  gain_db = 0.0;
            if (gain_db > gmax) gain_db = gmax;
            uint16_t gain_db10 = (uint16_t)(gain_db * GAIN_DB10_PER_DB + 0.5);
            config_set_gain_raw(gain_db10);
            LOG_INFO("genconv: Gain %.2f dB -> %u (0.1 dB)", gain_db, gain_db10);
        } else if (len >= 4) {
            uint16_t gain_db10 = (uint16_t)(read_be_u32(value) & 0xFFFF);
            config_set_gain_raw(gain_db10);
            LOG_INFO("genconv: Gain %u (0.1 dB)", gain_db10);
        }
        return 0;

    case GIGE_REG_PIXFMT:
        if (len >= 4) {
            uint32_t pfnc = read_be_u32(value);
            config_set_pixfmt_eth(pfnc);
            if (genconv_pfnc_to_bits(pfnc) > 0) {
                imgsensor_cfg_t s;
                config_get_sensor(&s);
                s.pixel_format = pfnc;
                config_set_sensor(&s, CFG_SRC_GIGE);
            }
            LOG_INFO("genconv: PixelFormat 0x%08X", pfnc);
        }
        return 0;

    case GIGE_REG_REVERSE_X:
        if (len >= 4) {
            bool on = (read_be_u32(value) != 0);
            bool was = config_get_reverse_x();
            config_set_reverse_x(on);
            LOG_INFO("genconv: ReverseX -> %d", on);
            if (was != config_get_reverse_x() && !config_get_stream_active())
                media_pipeline_refresh_cfa_order();
        }
        return 0;

    case GIGE_REG_REVERSE_Y:
        if (len >= 4) {
            bool on = (read_be_u32(value) != 0);
            bool was = config_get_reverse_y();
            config_set_reverse_y(on);
            LOG_INFO("genconv: ReverseY -> %d", on);
            if (was != config_get_reverse_y() && !config_get_stream_active())
                media_pipeline_refresh_cfa_order();
        }
        return 0;

    case GIGE_REG_TEST_PATTERN:
        if (len >= 4) {
            uint32_t pat = read_be_u32(value);
            config_set_test_pattern((uint8_t)pat);
            LOG_INFO("genconv: TestPattern -> %u", pat);
        }
        return 0;

    case GIGE_REG_ACQ_START:
        if (len >= 4 && read_be_u32(value) == 1) {
            config_set_stream(true);
            LOG_INFO("genconv: AcquisitionStart");
        }
        return 0;

    case GIGE_REG_ACQ_STOP:
        if (len >= 4 && read_be_u32(value) == 1) {
            config_set_stream(false);
            LOG_INFO("genconv: AcquisitionStop");
        }
        return 0;

    case GIGE_REG_EXPOSURE_AUTO:
        if (len >= 4) {
            uint32_t v = read_be_u32(value);
            if (v == 0) {
                config_set_auto_exposure(false);
                LOG_INFO("genconv: ExposureAuto -> Off");
            } else {
                config_set_auto_exposure(true);
                if (v == 1) config_request_ae_once_exposure();
                LOG_INFO("genconv: ExposureAuto -> %s", v == 1 ? "Once" : "Continuous");
            }
        }
        return 0;

    case GIGE_REG_GAIN_AUTO:
        if (len >= 4) {
            uint32_t v = read_be_u32(value);
            if (v == 0) {
                config_set_auto_gain(false);
                LOG_INFO("genconv: GainAuto -> Off");
            } else {
                config_set_auto_gain(true);
                if (v == 1) config_request_ae_once_gain();
                LOG_INFO("genconv: GainAuto -> %s", v == 1 ? "Once" : "Continuous");
            }
        }
        return 0;

    case GIGE_REG_BALANCE_WHITE_AUTO:
        if (len >= 4) {
            uint32_t v = read_be_u32(value);
            if (v == 0) {
                config_set_auto_wb(false);
                LOG_INFO("genconv: BalanceWhiteAuto -> Off");
            } else {
                config_set_auto_wb(true);
                if (v == 1) config_request_awb_once();
                LOG_INFO("genconv: BalanceWhiteAuto -> %s", v == 1 ? "Once" : "Continuous");
            }
        }
        return 0;

    case GIGE_REG_BALANCE_RATIO_SEL:
        if (len >= 4) {
            uint32_t s = read_be_u32(value);
            if (s <= 2) g_wb_ratio_sel = s;
        }
        return 0;

    case GIGE_REG_BALANCE_RATIO:
        if (len >= 4) {
            uint32_t u = read_be_u32(value);
            config_set_wb_ratio((int)g_wb_ratio_sel, (float)u / 256.0f);
        }
        return 0;

    case GIGE_REG_AWB_LOWER_LIMIT:
        if (len >= 4)
            config_set_awb_gain_min((float)read_be_u32(value) / 256.0f);
        return 0;

    case GIGE_REG_AWB_UPPER_LIMIT:
        if (len >= 4)
            config_set_awb_gain_max((float)read_be_u32(value) / 256.0f);
        return 0;

    case GIGE_REG_AWB_RATE:
        if (len >= 4)
            config_set_awb_damp((float)read_be_u32(value) / 1000.0f);
        return 0;

    case GIGE_REG_LINK_BW_LIMIT:
        if (len >= 4) {
            uint32_t bps = read_be_u32(value);
            if (bps < 10000000u) bps = 10000000u;
            if (bps > BW_ETH_MAX) bps = BW_ETH_MAX;
            config_set_link_bw_limit(CFG_LINK_ETH, bps);
        }
        return 0;

    case GIGE_REG_LINK_BW_MODE:
        if (len >= 4)
            config_set_link_bw_mode(CFG_LINK_ETH, read_be_u32(value) != 0);
        return 0;

    case GIGE_REG_COLOR_TEMP:
        if (len >= 4)
            config_set_color_temp((uint16_t)read_be_u32(value));
        return 0;

    case GIGE_REG_SATURATION:
        if (len >= 4) {
            uint32_t u = read_be_u32(value);
            if (u > 200) u = 200;
            config_set_saturation((float)u / 100.0f);
            LOG_INFO("genconv: Saturation -> %.2f", (float)u / 100.0f);
        }
        return 0;

    case GIGE_REG_GAMMA_ENABLE:
        if (len >= 4) {
            uint32_t v = read_be_u32(value);
            if (v > 1) { LOG_WARN("genconv: GammaEnable %u ignored", v); return 0; }
            g_gige_gamma_en = (uint8_t)v;
            gige_gamma_apply();
        }
        return 0;

    case GIGE_REG_GAMMA_SELECTOR:
        if (len >= 4) {
            uint32_t v = read_be_u32(value);
            if (v != 1 && v != 2) { LOG_WARN("genconv: GammaSelector %u ignored", v); return 0; }
            g_gige_gamma_sel = (uint8_t)v;
            gige_gamma_apply();
        }
        return 0;

    case GIGE_REG_GAMMA:
        if (len >= 8) {
            double g = read_be_f64(value);
            if (!isfinite(g) || g < CHC5_GAMMA_USER_MIN / 100.0 ||
                g > CHC5_GAMMA_USER_MAX / 100.0) {
                LOG_WARN("genconv: Gamma %g outside %.2f..%.2f, ignored", g,
                         CHC5_GAMMA_USER_MIN / 100.0, CHC5_GAMMA_USER_MAX / 100.0);
                return 0;
            }
            g_gige_gamma_x100 = (uint16_t)(g * 100.0 + 0.5);
            gige_gamma_apply();
        }
        return 0;

    case GIGE_REG_BLACK_LEVEL:
        if (len >= 8) {
            double bl = read_be_f64(value);
            if (!isfinite(bl)) { LOG_WARN("genconv: BlackLevel not finite, ignored"); return 0; }
            if (bl < 0.0)      bl = 0.0;
            if (bl > 4095.0)   bl = 4095.0;
            config_set_black_level((uint16_t)(bl + 0.5));
            LOG_INFO("genconv: BlackLevel %.1f -> %u", bl, (uint16_t)(bl + 0.5));
        } else if (len >= 4) {
            uint16_t bl = (uint16_t)(read_be_u32(value) & 0xFFFF);
            if (bl > 4095) bl = 4095;
            config_set_black_level(bl);
            LOG_INFO("genconv: BlackLevel raw %u", bl);
        }
        return 0;

    case GIGE_REG_FPS_ENABLE:
        if (len >= 4) {
            uint32_t v = read_be_u32(value);
            LOG_INFO("genconv: AcqFrameRateEnable -> %u", v);
        }
        return 0;

    case GIGE_REG_FPS:
        if (len >= 8) {
            double fps_hz = read_be_f64(value);
            if (!isfinite(fps_hz)) { LOG_WARN("genconv: AcquisitionFrameRate not finite, ignored"); return 0; }
            if (fps_hz < 0.1) fps_hz = 0.1;
            if (fps_hz > 10000.0) fps_hz = 10000.0;

            config_set_fps_f((float)fps_hz);
            LOG_INFO("genconv: AcquisitionFrameRate %.1f Hz -> %.1f", fps_hz, fps_hz);
        }
        return 0;

    case GIGE_REG_AE_METERING:
        if (len >= 4) {
            uint32_t m = read_be_u32(value);
            if (m > 5) { LOG_WARN("genconv: AEMeteringMode %u out of range, ignored", m); return 0; }
            config_set_ae_metering_mode((uint8_t)m);
            LOG_INFO("genconv: AEMeteringMode -> %u", m);
        }
        return 0;

    case GIGE_REG_AE_TARGET:
        if (len >= 8) {
            double grey = read_be_f64(value);
            if (!isfinite(grey)) { LOG_WARN("genconv: AutoExposureTargetGreyValue not finite, ignored"); return 0; }
            if (grey < 0.0)   grey = 0.0;
            if (grey > 255.0) grey = 255.0;
            config_set_ae_target((float)(grey / 255.0));
            LOG_INFO("genconv: AutoExposureTargetGreyValue %.1f -> target %.3f", grey, grey / 255.0);
        }
        return 0;

    case GIGE_REG_AEROI_OFFX:
    case GIGE_REG_AEROI_OFFY:
    case GIGE_REG_AEROI_W:
    case GIGE_REG_AEROI_H:
        if (len >= 4) {
            uint16_t rx, ry, rw, rh;
            config_get_ae_roi(&rx, &ry, &rw, &rh);
            uint16_t v = (uint16_t)read_be_u32(value);
            switch (addr) {
            case GIGE_REG_AEROI_OFFX: rx = v; break;
            case GIGE_REG_AEROI_OFFY: ry = v; break;
            case GIGE_REG_AEROI_W:    rw = v; break;
            case GIGE_REG_AEROI_H:    rh = v; break;
            }
            config_set_ae_roi(rx, ry, rw, rh);
            LOG_INFO("genconv: AutoFunctionAOI %s -> %u",
                     addr == GIGE_REG_AEROI_OFFX ? "OffsetX" :
                     addr == GIGE_REG_AEROI_OFFY ? "OffsetY" :
                     addr == GIGE_REG_AEROI_W    ? "Width"   : "Height", v);
        }
        return 0;

    default:
        return 0;
    }
}
