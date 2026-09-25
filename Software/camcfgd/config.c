// SPDX-License-Identifier: CC-BY-NC-ND-4.0
/*
 * config.c - Unified config state with mutex-protected access
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define SENSOR_ACTIVE_GAMMA_PATH  "/var/lib/chc5_platformd/sensors/active/gamma.bin"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "config.h"
#include "chc5_camio.h"
#include "camio_sync.h"

static struct camcfg_state_s g_state;
static pthread_mutex_t       g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t        g_dirty_cond;
static uint32_t              g_dirty_flags;
static bool                  g_shutdown;

static uint8_t g_hdmi_standalone;
static uint8_t g_hdmi_enable = 0;
static uint8_t g_prev_ae_force_enable;
static uint8_t g_prev_awb_force_enable;
static void config_force_awb_locked(bool enable);
static void ct_set_preset_gain(unsigned kelvin, int is_blue, float val);
static void ct_reset_presets(void);
static bool g_awb_auto;
static bool g_host_ae_exposure;
static bool g_host_ae_gain;
static bool g_ae_converged;
static uint32_t g_ae_exp_lower_us;
static uint32_t g_ae_exp_upper_us;

static uint32_t g_link_bw_limit[2];
static bool     g_link_bw_on[2] = { true, true };

static float    g_actual_fps;

enum stream_owner_e { SOWN_NONE = 0, SOWN_STANDALONE, SOWN_HOST };
static enum stream_owner_e g_stream_owner_kind;
static bool g_host_stream_on;

static bool fmt_locked_now(void)
{
    return g_state.stream_active && g_host_stream_on;
}
static bool g_host_seen;
static int  g_sa_tries;
static long g_sa_next_try_ms;

#define HDMI_SA_RETRY_MS  5000
#define HDMI_SA_MAX_TRIES 5

static long config_mono_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static void publish_stream_active(bool active)
{
    FILE *f = fopen("/run/camcfgd/stream_active", "w");
    if (f) {
        fputc(active ? '1' : '0', f);
        fclose(f);
    }
}

static void publish_pipeline_ready(bool ready)
{
    FILE *f = fopen("/run/camcfgd/pipeline_ready", "w");
    if (f) {
        fputc(ready ? '1' : '0', f);
        fclose(f);
    }
}

void config_set_pipeline_ready(bool ready)
{
    static int last = -1;
    int now = ready ? 1 : 0;
    if (now == last)
        return;
    last = now;
    publish_pipeline_ready(ready);
}

int    g_sensor_mono  = 0;
int    g_orient_hflip = 0;
int    g_bayer_order = 0;
int    g_orient_vflip = 0;
bool     g_sensor_caps_looked = false;
uint32_t g_sensor_width = 0, g_sensor_height = 0;
uint32_t g_width_min = 0,    g_height_min = 0;
uint32_t g_width_inc = 0,    g_height_inc = 0;
uint32_t g_binning_max_h = 0, g_binning_max_v = 0;
uint32_t g_binning_width = 0, g_binning_height = 0;
uint32_t g_binning_width_min = 0, g_binning_height_min = 0;
uint32_t g_offset_x_inc = 0, g_offset_y_inc = 0;
uint32_t g_binning_width_inc = 0, g_binning_height_inc = 0;
uint32_t g_binning_offset_x_inc = 0, g_binning_offset_y_inc = 0;
uint32_t g_sensor_bit_depth = 0;
uint32_t g_exposure_min_us = 0;

#define CFG_BLACK_LEVEL_DEFAULT 0u

static uint16_t config_black_level_for_depth(unsigned depth)
{
    (void)depth;
    return (uint16_t)CFG_BLACK_LEVEL_DEFAULT;
}

uint8_t g_camio_cap_has_xvs    = 2;
uint8_t g_camio_cap_xvs_out    = 2;
uint8_t g_camio_cap_xvs_in     = 2;
uint8_t g_camio_cap_has_xtrig  = 2;
uint8_t g_camio_cap_has_strobe = 2;

float    g_awb_gain_min = 0.25f;
float    g_awb_gain_max = 4.0f;
float    g_awb_damp     = 0.15f;
static float g_awb_baked_gain_min = 0.25f;
static float g_awb_baked_gain_max = 4.0f;
static float g_awb_baked_damp     = 0.15f;
static bool  g_awb_min_ovr = false, g_awb_max_ovr = false, g_awb_rate_ovr = false;
uint8_t  g_awb_gray_thr = 16;
uint8_t  g_gamma_x10   = 0;

static uint8_t g_gamma_lut[3][256];
static bool    g_gamma_valid = false;

static void gamma_fill_power(uint8_t lut[3][256], double g)
{
    for (unsigned i = 0; i < 256; i++) {
        long v = lround(255.0 * pow((double)i / 255.0, g));
        if (v < 0)   v = 0;
        if (v > 255) v = 255;
        lut[0][i] = lut[1][i] = lut[2][i] = (uint8_t)v;
    }
}

static bool gamma_load_blob(uint8_t lut[3][256])
{
    FILE *f = fopen(SENSOR_ACTIVE_GAMMA_PATH, "rb");
    if (!f)
        return false;
    size_t n = fread(lut, 1, 3 * 256, f);
    bool  extra = (fgetc(f) != EOF);
    fclose(f);
    if (n != 3 * 256 || extra) {
        LOG_WARN("gamma: %s is %zu bytes (want exactly 768) -- ignored",
                 SENSOR_ACTIVE_GAMMA_PATH, n);
        return false;
    }
    return true;
}

void config_reload_gamma(void)
{
    g_gamma_valid = false;

    if (gamma_load_blob(g_gamma_lut)) {
        g_gamma_valid = true;
        LOG_INFO("gamma: per-sensor curve from %s (768 B)", SENSOR_ACTIVE_GAMMA_PATH);
    } else if (g_gamma_x10 != 0) {
        gamma_fill_power(g_gamma_lut, (double)g_gamma_x10 / 10.0);
        g_gamma_valid = true;
        LOG_INFO("gamma: power curve %u.%u from sensor_caps",
                 g_gamma_x10 / 10, g_gamma_x10 % 10);
    } else {
        LOG_WARN("gamma: sensor archive carries no gamma.bin and no gamma_x10 "
                 "-> chc5_gamma left DISABLED (linear output)");
    }
}

const uint8_t (*config_get_gamma_lut(void))[256]
{
    static bool tried = false;

    if (!tried) { tried = true; config_reload_gamma(); }
    return g_gamma_valid ? g_gamma_lut : NULL;
}

static uint16_t g_gamma_host = CHC5_GAMMA_NO_CHANGE;

static void mark_dirty(uint32_t flags);

void config_set_gamma_x100(uint16_t v)
{
    uint16_t mag = (uint16_t)(v & ~CHC5_GAMMA_CURVE_ADJ);

    if (v == CHC5_GAMMA_NO_CHANGE)
        return;
    if ((v & CHC5_GAMMA_CURVE_ADJ)
            ? (mag < CHC5_GAMMA_USER_MIN || mag > CHC5_GAMMA_USER_MAX)
            : (v != CHC5_GAMMA_OFF && v != CHC5_GAMMA_SRGB &&
               (v < CHC5_GAMMA_USER_MIN || v > CHC5_GAMMA_USER_MAX))) {
        LOG_WARN("config: gamma 0x%04x is not a CHC5_GAMMA_* value -- ignored", v);
        return;
    }
    pthread_mutex_lock(&g_lock);
    bool changed = (g_gamma_host != v);
    g_gamma_host = v;
    if (changed)
        mark_dirty(DIRTY_GAMMA);
    pthread_mutex_unlock(&g_lock);

    if (!changed)
        return;
    if (v == CHC5_GAMMA_OFF)
        LOG_INFO("config: gamma OFF (linear, curve bypassed)");
    else if (v == CHC5_GAMMA_SRGB)
        LOG_INFO("config: gamma sRGB (the sensor archive's curve)");
    else if (v & CHC5_GAMMA_CURVE_ADJ)
        LOG_INFO("config: gamma %u.%02u on the sensor's curve (UVC)",
                 mag / 100u, mag % 100u);
    else
        LOG_INFO("config: gamma User %u.%02u", v / 100u, v % 100u);
}

static void gamma_fill_srgb(uint8_t lut[3][256])
{
    for (unsigned i = 0; i < 256; i++) {
        double x = (double)i / 255.0;
        double y = (x <= 0.0031308) ? 12.92 * x
                                    : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
        long v = lround(255.0 * y);
        if (v < 0)   v = 0;
        if (v > 255) v = 255;
        lut[0][i] = lut[1][i] = lut[2][i] = (uint8_t)v;
    }
}

bool config_get_output_gamma(uint8_t out[3][256], bool *host_off)
{
    pthread_mutex_lock(&g_lock);
    uint16_t host = g_gamma_host;
    pthread_mutex_unlock(&g_lock);

    const uint8_t (*cal)[256] = config_get_gamma_lut();

    *host_off = false;
    switch (host) {
    case CHC5_GAMMA_OFF:
        *host_off = true;
        return false;
    case CHC5_GAMMA_NO_CHANGE:
        if (!cal)
            return false;
        memcpy(out, cal, 3u * 256u);
        return true;
    case CHC5_GAMMA_SRGB:
        if (cal)
            memcpy(out, cal, 3u * 256u);
        else
            gamma_fill_srgb(out);
        return true;
    default:
        if (host & CHC5_GAMMA_CURVE_ADJ) {
            double e = 100.0 / (double)(host & ~CHC5_GAMMA_CURVE_ADJ);
            if (cal)
                memcpy(out, cal, 3u * 256u);
            else
                gamma_fill_srgb(out);
            for (unsigned c = 0; c < 3; c++)
                for (unsigned i = 0; i < 256; i++) {
                    long y = lround(255.0 * pow((double)out[c][i] / 255.0, e));
                    out[c][i] = (uint8_t)(y < 0 ? 0 : y > 255 ? 255 : y);
                }
            return true;
        }
        gamma_fill_power(out, (double)host / 100.0);
        return true;
    }
}

uint8_t  g_awb_gray_en  = 1;
uint8_t  g_awb_y_lo     = 16;
uint8_t  g_awb_y_hi     = 240;

static int32_t g_ccm_baked[12] = { 4096, 0, 0, 0,  0, 4096, 0, 0,  0, 0, 4096, 0 };
static bool    g_ccm_present   = false;
static bool    g_ccm_dirty     = false;

static ccm_ct_entry g_ccm_ct[CCM_CT_MAX];
static int          g_ccm_ct_n = 0;

static float g_ccm_last_ct  = 0.0f;
static float g_ccm_last_sat = 1.0f;
#define CCM_CT_REAPPLY_K 25.0f

static float g_saturation = 1.0f;
static float g_awb_ct_est = 0.0f;

static uint32_t g_lux_ref_exp_us    = 0;
static uint16_t g_lux_ref_gain_code = 0;
static float    g_lux_ref_y         = 0.0f;
static float    g_lux_ref_lux       = 0.0f;

static float g_awb_transverse   = 0.15f;
static float g_awb_prior_weight = 0.0f;
static float g_awb_wp_r = 1.0f, g_awb_wp_b = 1.0f;
#define AWB_PRIOR_MAX 8
static float g_awb_prior_lux[AWB_PRIOR_MAX];
static float g_awb_prior_ct[AWB_PRIOR_MAX];
static int   g_awb_prior_n = 0;
#define AE_TCURVE_MAX 8
static float g_ae_tc_lux[AE_TCURVE_MAX];
static float g_ae_tc_fac[AE_TCURVE_MAX];
static int   g_ae_tc_n = 0;

static int parse_pair_curve(const char *s, float *xs, float *ys, int max,
                            float y_scale, float y_min, float y_max)
{
    int n = 0;
    while (n < max) {
        float x, y;
        int consumed = 0;
        if (sscanf(s, " %f:%f%n", &x, &y, &consumed) != 2)
            break;
        y /= y_scale;
        if (x < 0.0f || y < y_min || y > y_max)
            break;
        if (n > 0 && x <= xs[n - 1])
            break;
        xs[n] = x;  ys[n] = y;  n++;
        s += consumed;
    }
    return n;
}

static uint32_t respell_bayer(uint32_t pfnc)
{
    int order = g_state.live_cfa_order ? g_state.live_cfa_order : g_bayer_order;
    static const uint32_t fam[][4] = {
        { GVSP_PIX_BAYERRG8,  GVSP_PIX_BAYERGB8,  GVSP_PIX_BAYERGR8,  GVSP_PIX_BAYERBG8  },
        { GVSP_PIX_BAYERRG10, GVSP_PIX_BAYERGB10, GVSP_PIX_BAYERGR10, GVSP_PIX_BAYERBG10 },
        { GVSP_PIX_BAYERRG10P,GVSP_PIX_BAYERGB10P,GVSP_PIX_BAYERGR10P,GVSP_PIX_BAYERBG10P},
        { GVSP_PIX_BAYERRG12, GVSP_PIX_BAYERGB12, GVSP_PIX_BAYERGR12, GVSP_PIX_BAYERBG12 },
        { GVSP_PIX_BAYERRG12P,GVSP_PIX_BAYERGB12P,GVSP_PIX_BAYERGR12P,GVSP_PIX_BAYERBG12P},
        { GVSP_PIX_BAYERRG14, GVSP_PIX_BAYERGB14, GVSP_PIX_BAYERGR14, GVSP_PIX_BAYERBG14 },
        { GVSP_PIX_BAYERRG14P,GVSP_PIX_BAYERGB14P,GVSP_PIX_BAYERGR14P,GVSP_PIX_BAYERBG14P},
    };
    size_t f, c;

    if (order < 1 || order > 4)
        return pfnc;

    for (f = 0; f < sizeof fam / sizeof fam[0]; f++)
        for (c = 0; c < 4; c++)
            if (fam[f][c] == pfnc)
                return fam[f][order - 1];

    return pfnc;
}

static uint32_t ingest_pixfmt(uint32_t pfnc)
{
    return g_sensor_mono ? pfnc : respell_bayer(pfnc);
}

static uint32_t respell_mono(uint32_t pfnc)
{
    switch (pfnc) {
    case GVSP_PIX_BAYERRG8:   case GVSP_PIX_BAYERGB8:
    case GVSP_PIX_BAYERGR8:   case GVSP_PIX_BAYERBG8:
        return GVSP_PIX_MONO8;
    case GVSP_PIX_BAYERRG12P: case GVSP_PIX_BAYERGB12P:
    case GVSP_PIX_BAYERGR12P: case GVSP_PIX_BAYERBG12P:
        return GVSP_PIX_MONO12P;
    case GVSP_PIX_BAYERRG10:  case GVSP_PIX_BAYERGB10:
    case GVSP_PIX_BAYERGR10:  case GVSP_PIX_BAYERBG10:
    case GVSP_PIX_BAYERRG10P: case GVSP_PIX_BAYERGB10P:
    case GVSP_PIX_BAYERGR10P: case GVSP_PIX_BAYERBG10P:
    case GVSP_PIX_BAYERRG12:  case GVSP_PIX_BAYERGB12:
    case GVSP_PIX_BAYERGR12:  case GVSP_PIX_BAYERBG12:
    case GVSP_PIX_BAYERRG14:  case GVSP_PIX_BAYERGB14:
    case GVSP_PIX_BAYERGR14:  case GVSP_PIX_BAYERBG14:
    case GVSP_PIX_BAYERRG14P: case GVSP_PIX_BAYERGB14P:
    case GVSP_PIX_BAYERGR14P: case GVSP_PIX_BAYERBG14P:
        return GVSP_PIX_MONO12;
    default:
        return pfnc;
    }
}

void config_apply_bayer_order(void)
{
    if (g_sensor_mono) {
        uint32_t before = g_state.sensor_cfg.pixel_format;

        g_state.sensor_cfg.pixel_format = respell_mono(g_state.sensor_cfg.pixel_format);
        g_state.pixfmt_usb              = respell_mono(g_state.pixfmt_usb);
        g_state.pixfmt_eth              = respell_mono(g_state.pixfmt_eth);

        if (before != g_state.sensor_cfg.pixel_format)
            LOG_INFO("config: mono sensor -- PixelFormat 0x%08X -> 0x%08X "
                     "(a Bayer default is a format this camera cannot produce)",
                     before, g_state.sensor_cfg.pixel_format);
        return;
    }

    g_state.sensor_cfg.pixel_format = respell_bayer(g_state.sensor_cfg.pixel_format);
    g_state.pixfmt_usb              = respell_bayer(g_state.pixfmt_usb);
    g_state.pixfmt_eth              = respell_bayer(g_state.pixfmt_eth);
}

void config_set_live_cfa_order(int order)
{
    static const char *nm[] = { "unknown", "RG", "GB", "GR", "BG" };
    uint32_t before, after;
    uint8_t flips;
    bool changed, echoed;

    if (order < 0 || order > 4)
        order = 0;

    pthread_mutex_lock(&g_lock);
    changed = order != 0 && g_state.live_cfa_order != (uint8_t)order;
    before  = g_state.pixfmt_eth;
    if (changed)
        g_state.live_cfa_order = (uint8_t)order;
    if (changed && !g_sensor_mono) {
        g_state.sensor_cfg.pixel_format = respell_bayer(g_state.sensor_cfg.pixel_format);
        g_state.pixfmt_usb              = respell_bayer(g_state.pixfmt_usb);
        g_state.pixfmt_eth              = respell_bayer(g_state.pixfmt_eth);
    }
    after = g_state.pixfmt_eth;
    flips  = (uint8_t)((config_get_reverse_x() ? 1u : 0u) |
                       (config_get_reverse_y() ? 2u : 0u));
    echoed = g_state.live_cfa_flips != flips;
    g_state.live_cfa_flips = flips;
    pthread_mutex_unlock(&g_lock);

    if (changed)
        LOG_INFO("config: sensor now delivers Bayer%s (flip/binning) -- "
                 "PixelFormat 0x%08X -> 0x%08X", nm[order], before, after);
    if (changed || echoed)
        config_publish_exposure();
}

uint8_t config_get_live_cfa_order(void)
{
    uint8_t v;

    pthread_mutex_lock(&g_lock);
    v = g_state.live_cfa_order;
    pthread_mutex_unlock(&g_lock);
    return v;
}

void config_load_sensor_caps(void)
{
    static time_t last_mtime = 0;
    const char *path = "/var/lib/chc5_platformd/sensors/sensor_caps";
    struct stat stt;
    g_sensor_caps_looked = true;
    if (stat(path, &stt) != 0) { last_mtime = 0; return; }
    if (stt.st_mtime == last_mtime) return;
    last_mtime = stt.st_mtime;

    FILE *f = fopen(path, "r");
    if (!f)
        return;
    g_ccm_present = false;
    g_ccm_ct_n    = 0;
    g_camio_cap_has_xvs = g_camio_cap_xvs_out = g_camio_cap_xvs_in = 2;
    g_camio_cap_has_xtrig = g_camio_cap_has_strobe = 2;
    g_sensor_mono = 0;
    g_orient_hflip = 0;
    g_orient_vflip = 0;
    g_bayer_order = 0;
    g_sensor_width = g_sensor_height = 0;
    g_width_min = g_height_min = 0;
    g_width_inc = g_height_inc = 0;
    g_binning_max_h = g_binning_max_v = 0;
    g_binning_width = g_binning_height = 0;
    g_binning_width_min = g_binning_height_min = 0;
    g_offset_x_inc = g_offset_y_inc = 0;
    g_binning_width_inc = g_binning_height_inc = 0;
    g_binning_offset_x_inc = g_binning_offset_y_inc = 0;
    g_sensor_bit_depth = 0;
    g_exposure_min_us = 0;
    ct_reset_presets();
    g_lux_ref_exp_us = 0; g_lux_ref_gain_code = 0;
    g_lux_ref_y = 0.0f;   g_lux_ref_lux = 0.0f;
    g_awb_transverse   = 0.15f;
    g_awb_prior_weight = 0.0f;
    g_awb_wp_r = g_awb_wp_b = 1.0f;
    g_awb_prior_n = 0;
    g_ae_tc_n     = 0;
    char line[192];
    while (fgets(line, sizeof line, f)) {
        unsigned v;
        if (sscanf(line, "chroma_mono=%u", &v) == 1 && v <= 1u)
            g_sensor_mono = (int)v;
        else if (sscanf(line, "bayer_order=%u", &v) == 1 && v <= 4u)
            g_bayer_order = (int)v;
        else if (sscanf(line, "orient_hflip=%u", &v) == 1 && v <= 1u)
            g_orient_hflip = (int)v;
        else if (sscanf(line, "orient_vflip=%u", &v) == 1 && v <= 1u)
            g_orient_vflip = (int)v;
        else if (sscanf(line, "sensor_width=%u", &v) == 1 && v >= 1u && v <= 65535u)
            g_sensor_width = v;
        else if (sscanf(line, "sensor_height=%u", &v) == 1 && v >= 1u && v <= 65535u)
            g_sensor_height = v;
        else if (sscanf(line, "width_min=%u", &v) == 1 && v >= 1u && v <= 65535u)
            g_width_min = v;
        else if (sscanf(line, "height_min=%u", &v) == 1 && v >= 1u && v <= 65535u)
            g_height_min = v;
        else if (sscanf(line, "width_inc=%u", &v) == 1 && v >= 1u && v <= 256u)
            g_width_inc = v;
        else if (sscanf(line, "height_inc=%u", &v) == 1 && v >= 1u && v <= 256u)
            g_height_inc = v;
        else if (sscanf(line, "bit_depth=%u", &v) == 1 &&
                 v >= 8u && v <= 16u && (v % 2u) == 0u)
            g_sensor_bit_depth = v;
        else if (sscanf(line, "exposure_min_us=%u", &v) == 1 &&
                 v >= 1u && v <= 30000000u)
            g_exposure_min_us = v;
        else if (sscanf(line, "binning_max_h=%u", &v) == 1 && v >= 1u && v <= 16u)
            g_binning_max_h = v;
        else if (sscanf(line, "binning_max_v=%u", &v) == 1 && v >= 1u && v <= 16u)
            g_binning_max_v = v;
        else if (sscanf(line, "binning_width=%u", &v) == 1 && v <= 65535u)
            g_binning_width = v;
        else if (sscanf(line, "binning_height=%u", &v) == 1 && v <= 65535u)
            g_binning_height = v;
        else if (sscanf(line, "binning_width_min=%u", &v) == 1 && v <= 65535u)
            g_binning_width_min = v;
        else if (sscanf(line, "binning_height_min=%u", &v) == 1 && v <= 65535u)
            g_binning_height_min = v;
        else if (sscanf(line, "offset_x_inc=%u", &v) == 1 && v >= 1u && v <= 256u)
            g_offset_x_inc = v;
        else if (sscanf(line, "offset_y_inc=%u", &v) == 1 && v >= 1u && v <= 256u)
            g_offset_y_inc = v;
        else if (sscanf(line, "binning_width_inc=%u", &v) == 1 && v >= 1u && v <= 256u)
            g_binning_width_inc = v;
        else if (sscanf(line, "binning_height_inc=%u", &v) == 1 && v >= 1u && v <= 256u)
            g_binning_height_inc = v;
        else if (sscanf(line, "binning_offset_x_inc=%u", &v) == 1 && v >= 1u && v <= 256u)
            g_binning_offset_x_inc = v;
        else if (sscanf(line, "binning_offset_y_inc=%u", &v) == 1 && v >= 1u && v <= 256u)
            g_binning_offset_y_inc = v;
        else if (sscanf(line, "awb_gain_min_x100=%u", &v) == 1 && v >= 1u && v <= 100u) {
            g_awb_baked_gain_min = (float)v / 100.0f;
            if (!g_awb_min_ovr) g_awb_gain_min = g_awb_baked_gain_min;
        }
        else if (sscanf(line, "awb_gain_max_x100=%u", &v) == 1 && v >= 100u && v <= 1600u) {
            g_awb_baked_gain_max = (float)v / 100.0f;
            if (!g_awb_max_ovr) g_awb_gain_max = g_awb_baked_gain_max;
        }
        else if (sscanf(line, "awb_damp_x100=%u", &v) == 1 && v >= 1u && v <= 100u) {
            g_awb_baked_damp = (float)v / 100.0f;
            if (!g_awb_rate_ovr) g_awb_damp = g_awb_baked_damp;
        }
        else if (sscanf(line, "gamma_x10=%u", &v) == 1 && v >= 1u && v <= 40u)
            g_gamma_x10 = (uint8_t)v;
        else if (sscanf(line, "awb_gray_thr=%u", &v) == 1 && v <= 255u)
            g_awb_gray_thr = (uint8_t)v;
        else if (sscanf(line, "awb_gray_en=%u", &v) == 1 && v <= 1u)
            g_awb_gray_en = (uint8_t)v;
        else if (sscanf(line, "awb_y_lo=%u", &v) == 1 && v <= 255u)
            g_awb_y_lo = (uint8_t)v;
        else if (sscanf(line, "awb_y_hi=%u", &v) == 1 && v <= 255u)
            g_awb_y_hi = (uint8_t)v;
        else if (sscanf(line, "camio_has_xvs=%u", &v) == 1 && v <= 2u)
            g_camio_cap_has_xvs = (uint8_t)v;
        else if (sscanf(line, "camio_xvs_out=%u", &v) == 1 && v <= 2u)
            g_camio_cap_xvs_out = (uint8_t)v;
        else if (sscanf(line, "camio_xvs_in=%u", &v) == 1 && v <= 2u)
            g_camio_cap_xvs_in = (uint8_t)v;
        else if (sscanf(line, "camio_has_xtrig=%u", &v) == 1 && v <= 2u)
            g_camio_cap_has_xtrig = (uint8_t)v;
        else if (sscanf(line, "camio_has_strobe=%u", &v) == 1 && v <= 2u)
            g_camio_cap_has_strobe = (uint8_t)v;
        else if (sscanf(line, "lux_ref_exp_us=%u", &v) == 1 && v <= 30000000u)
            g_lux_ref_exp_us = v;
        else if (sscanf(line, "lux_ref_gain_code=%u", &v) == 1 && v <= 65535u)
            g_lux_ref_gain_code = (uint16_t)v;
        else if (sscanf(line, "lux_ref_y_x1000=%u", &v) == 1 && v <= 1000u)
            g_lux_ref_y = (float)v / 1000.0f;
        else if (sscanf(line, "lux_ref_lux_x10=%u", &v) == 1 && v <= 100000000u)
            g_lux_ref_lux = (float)v / 10.0f;
        else if (sscanf(line, "awb_transverse_x100=%u", &v) == 1 && v <= 200u)
            g_awb_transverse = (float)v / 100.0f;
        else if (sscanf(line, "awb_prior_weight_x100=%u", &v) == 1 && v <= 10000u)
            g_awb_prior_weight = (float)v / 100.0f;
        else if (sscanf(line, "awb_wp_r_x100=%u", &v) == 1 && v >= 50u && v <= 200u)
            g_awb_wp_r = (float)v / 100.0f;
        else if (sscanf(line, "awb_wp_b_x100=%u", &v) == 1 && v >= 50u && v <= 200u)
            g_awb_wp_b = (float)v / 100.0f;
        else if (strncmp(line, "awb_prior=", 10) == 0)
            g_awb_prior_n = parse_pair_curve(line + 10, g_awb_prior_lux,
                                             g_awb_prior_ct, AWB_PRIOR_MAX,
                                             1.0f, 1000.0f, 20000.0f);
        else if (strncmp(line, "ae_target_curve=", 16) == 0)
            g_ae_tc_n = parse_pair_curve(line + 16, g_ae_tc_lux, g_ae_tc_fac,
                                         AE_TCURVE_MAX, 100.0f, 0.2f, 3.0f);
        else if (strncmp(line, "ccm=", 4) == 0) {
            int m[12];
            int nc = sscanf(line + 4,
                            "%d %d %d %d %d %d %d %d %d %d %d %d",
                            &m[0], &m[1], &m[2], &m[3], &m[4], &m[5],
                            &m[6], &m[7], &m[8], &m[9], &m[10], &m[11]);
            if (nc == 12) {
                for (int i = 0; i < 12; i++) {
                    if (m[i] < -32768) m[i] = -32768;
                    else if (m[i] > 32767) m[i] = 32767;
                    g_ccm_baked[i] = (int32_t)m[i];
                }
                g_ccm_present = true;
            } else {
                LOG_WARN("config: sensor_caps ccm= has %d ints (need 12) -- ignored", nc);
            }
        }
        else {
            unsigned k;
            int off = 0, m[12];
            if (sscanf(line, "ccm_%u=%n", &k, &off) == 1 && off > 0 &&
                k >= 1000u && k <= 20000u) {
                int nc = sscanf(line + off,
                                "%d %d %d %d %d %d %d %d %d %d %d %d",
                                &m[0], &m[1], &m[2], &m[3], &m[4], &m[5],
                                &m[6], &m[7], &m[8], &m[9], &m[10], &m[11]);
                if (nc == 12 && g_ccm_ct_n < CCM_CT_MAX) {
                    int pos = g_ccm_ct_n;
                    while (pos > 0 && g_ccm_ct[pos - 1].kelvin > (float)k) {
                        g_ccm_ct[pos] = g_ccm_ct[pos - 1];
                        pos--;
                    }
                    g_ccm_ct[pos].kelvin = (float)k;
                    for (int i = 0; i < 12; i++) {
                        if (m[i] < -32768) m[i] = -32768;
                        else if (m[i] > 32767) m[i] = 32767;
                        g_ccm_ct[pos].m[i] = (int32_t)m[i];
                    }
                    g_ccm_ct_n++;
                } else if (nc != 12) {
                    LOG_WARN("config: sensor_caps ccm_%u= has %d ints (need 12) -- ignored",
                             k, nc);
                }
                continue;
            }
            unsigned cv;
            if (sscanf(line, "ct_%u_r_x100=%u", &k, &cv) == 2 && cv >= 5u && cv <= 1600u)
                ct_set_preset_gain(k, 0, (float)cv / 100.0f);
            else if (sscanf(line, "ct_%u_b_x100=%u", &k, &cv) == 2 && cv >= 5u && cv <= 1600u)
                ct_set_preset_gain(k, 1, (float)cv / 100.0f);
        }
    }
    fclose(f);
    if (g_awb_y_hi <= g_awb_y_lo)
        g_awb_y_hi = (g_awb_y_lo < 255) ? 255 : g_awb_y_lo;
    if (g_awb_gain_max < g_awb_gain_min)
        g_awb_gain_max = g_awb_gain_min;
    if (g_sensor_width && g_sensor_height &&
        g_state.sensor_cfg_source == CFG_SRC_NONE) {
        imgsensor_cfg_t s;
        config_get_sensor(&s);
        if (s.width != (uint16_t)g_sensor_width ||
            s.height != (uint16_t)g_sensor_height) {
            LOG_INFO("config: boot geometry %ux%u -> %ux%u "
                     "(the default is a size from another sensor; nobody has "
                     "asked for one yet)",
                     s.width, s.height, g_sensor_width, g_sensor_height);
            s.width  = (uint16_t)g_sensor_width;
            s.height = (uint16_t)g_sensor_height;
            config_set_sensor(&s, CFG_SRC_NONE);
        }
    }

    if (g_width_min || g_height_min) {
        imgsensor_cfg_t s;
        config_get_sensor(&s);
        uint16_t w0 = s.width, h0 = s.height;
        if (g_width_min  && s.width  < g_width_min)
            s.width  = (uint16_t)g_width_min;
        if (g_height_min && s.height < g_height_min)
            s.height = (uint16_t)g_height_min;
        if (s.width != w0 || s.height != h0) {
            LOG_INFO("config: geometry %ux%u -> %ux%u "
                     "(carried over from the previous sensor; this one starts "
                     "at %ux%u)", w0, h0, s.width, s.height,
                     g_width_min, g_height_min);
            config_set_sensor(&s, CFG_SRC_NONE);
        }
    }
    {
        uint16_t bl  = config_black_level_for_depth(g_sensor_bit_depth);
        uint16_t cur = config_get_black_level();
        if (bl != cur) {
            LOG_INFO("config: black level %u -> %u (sensor adds no pedestal)",
                     cur, bl);
            config_set_black_level(bl);
        }
    }
    g_ccm_dirty = true;
    config_reload_gamma();

    config_apply_bayer_order();
}

bool config_take_ccm_update(int32_t out[12], bool *enable)
{
    pthread_mutex_lock(&g_lock);
    float ct  = g_awb_ct_est;
    float sat = g_saturation;
    pthread_mutex_unlock(&g_lock);

    bool ct_moved = (g_ccm_ct_n >= 2 && ct > 0.0f &&
                     fabsf(ct - g_ccm_last_ct) >= CCM_CT_REAPPLY_K);
    bool sat_moved = (fabsf(sat - g_ccm_last_sat) > 0.004f);
    if (!g_ccm_dirty && !ct_moved && !sat_moved)
        return false;
    g_ccm_dirty    = false;
    g_ccm_last_ct  = ct;
    g_ccm_last_sat = sat;

    static const int32_t ident[12] =
        { 4096, 0, 0, 0,  0, 4096, 0, 0,  0, 0, 4096, 0 };
    bool base_present = true;
    if (g_ccm_ct_n >= 1 && ct > 0.0f)
        ccm_interp_ct(g_ccm_ct, g_ccm_ct_n, ct, out);
    else if (g_ccm_present)
        memcpy(out, g_ccm_baked, sizeof g_ccm_baked);
    else if (g_ccm_ct_n >= 1)
        ccm_interp_ct(g_ccm_ct, g_ccm_ct_n, 5600.0f, out);
    else {
        memcpy(out, ident, sizeof ident);
        base_present = false;
    }

    bool sat_active = (fabsf(sat - 1.0f) > 0.004f);
    if (sat_active)
        ccm_apply_saturation(out, sat, out);

    if (enable)
        *enable = base_present || sat_active;
    return true;
}
void config_set_awb_gain_min(float v)
{
    if (v < 0.05f) v = 0.05f;
    if (v > 1.0f)  v = 1.0f;
    if (v > g_awb_gain_max) v = g_awb_gain_max;
    g_awb_gain_min = v;
    LOG_INFO("config: AWB gain min -> %.2f", v);
}
void config_set_awb_gain_max(float v)
{
    if (v < 1.0f)  v = 1.0f;
    if (v > 16.0f) v = 16.0f;
    if (v < g_awb_gain_min) v = g_awb_gain_min;
    g_awb_gain_max = v;
    LOG_INFO("config: AWB gain max -> %.2f", v);
}
void config_set_awb_damp(float v)
{
    if (v < 0.01f) v = 0.01f;
    if (v > 1.0f)  v = 1.0f;
    g_awb_damp = v;
    LOG_INFO("config: AWB rate (damp) -> %.3f", v);
}
uint8_t config_get_awb_gray_thr(void) { return g_awb_gray_thr; }
uint8_t config_get_gamma_x10(void)  { return g_gamma_x10; }
uint8_t config_get_awb_gray_en(void)  { return g_awb_gray_en; }
uint8_t config_get_awb_y_lo(void)     { return g_awb_y_lo; }
uint8_t config_get_awb_y_hi(void)     { return g_awb_y_hi; }

void config_set_awb_ct_estimate(float kelvin)
{
    pthread_mutex_lock(&g_lock);
    g_awb_ct_est = kelvin;
    pthread_mutex_unlock(&g_lock);
}

float config_get_awb_ct_estimate(void)
{
    pthread_mutex_lock(&g_lock);
    float v = g_awb_ct_est;
    pthread_mutex_unlock(&g_lock);
    return v;
}

void config_set_saturation(float s)
{
    if (!(s >= 0.0f)) s = 1.0f;
    if (s > 2.0f)     s = 2.0f;
    pthread_mutex_lock(&g_lock);
    bool changed = (fabsf(g_saturation - s) > 0.004f);
    g_saturation = s;
    pthread_mutex_unlock(&g_lock);
    if (changed)
        LOG_INFO("config: saturation -> %.2f", s);
}

float config_get_saturation(void)
{
    pthread_mutex_lock(&g_lock);
    float v = g_saturation;
    pthread_mutex_unlock(&g_lock);
    return v;
}

static int16_t readback_soc_temp_x10(void)
{
    FILE *f = fopen("/run/chc5_platformd/temperature", "r");
    if (!f)
        return 0;
    long mdegc = 0;
    int ok = (fscanf(f, "%ld", &mdegc) == 1);
    fclose(f);
    if (!ok)
        return 0;
    long dc = (mdegc + (mdegc >= 0 ? 50 : -50)) / 100;
    if (dc >  32767) dc =  32767;
    if (dc < -32768) dc = -32768;
    return (int16_t)dc;
}

static void publish_readback(uint32_t exp_us, uint16_t gain,
                             bool ae_exp_active, bool ae_gain_active,
                             bool converged, float res_fps,
                             uint32_t pixfmt_usb, uint8_t cfa_order,
                             uint8_t flip_echo)
{
    static int     rb_fd  = -1;
    static bool    tried  = false;
    static uint8_t rb_seq = 0;
    imgsensor_readback_t rb;

    if (rb_fd < 0) {
        if (tried)
            return;
        tried = true;
        rb_fd = open(USB_CHARDEV_PATH, O_WRONLY);
        if (rb_fd < 0)
            return;
    }

    if (res_fps < 0.0f)   res_fps = 0.0f;
    if (res_fps > 655.0f) res_fps = 655.0f;

    memset(&rb, 0, sizeof(rb));
    rb.ae_status   = (uint8_t)((ae_exp_active  ? RB_AE_STATUS_EXP_ACTIVE  : 0) |
                               (ae_gain_active ? RB_AE_STATUS_GAIN_ACTIVE : 0) |
                               (converged      ? RB_AE_STATUS_CONVERGED   : 0));
    rb.exposure_us = exp_us;
    rb.analog_gain = gain;
    rb.fps_x100    = (uint16_t)(res_fps * 100.0f + 0.5f);
    rb.temp_c_x10  = readback_soc_temp_x10();
    rb.pixfmt_usb  = pixfmt_usb;
    rb.cfa_order   = cfa_order;
    rb.flip_echo   = flip_echo;
    rb.rb_seq      = ++rb_seq;
    rb.rb_seq_echo = rb_seq;

    if (pwrite(rb_fd, &rb, sizeof(rb), CHC5_READBACK_OFFSET) != (ssize_t)sizeof(rb)) {
        close(rb_fd);
        rb_fd = -1;
        tried = false;
    }
}

void config_publish_exposure(void)
{
    pthread_mutex_lock(&g_lock);
    uint32_t exp_us = g_state.ae_exposure_auto ? g_state.ae_current_exposure_us
                                               : g_state.sensor_cfg.exposure_us;
    uint16_t gain   = g_state.ae_gain_auto ? g_state.ae_current_gain
                                           : g_state.sensor_cfg.analog_gain;
    bool ae_exp     = g_state.ae_exposure_auto;
    bool ae_gain    = g_state.ae_gain_auto;
    bool streaming  = g_state.stream_active;
    uint32_t pixfmt_usb = g_state.pixfmt_usb;
    uint8_t  cfa_order  = g_state.live_cfa_order;
    uint8_t  flip_echo  = g_state.live_cfa_flips;
    bool ae_once    = g_state.ae_once_exposure || g_state.ae_once_gain;
    int  owner      = (int)g_stream_owner_kind;
    float res_fps   = (g_actual_fps > 0.0f) ? g_actual_fps : g_state.fps_precise;
    bool converged  = g_ae_converged;
    pthread_mutex_unlock(&g_lock);
    bool ae_active = (ae_exp || ae_gain) && streaming;
    bool awb_auto   = config_get_auto_wb();
    bool awb_active = awb_auto && streaming;
    float awb_r = config_get_wb_ratio(0);
    float awb_b = config_get_wb_ratio(2);
    float lux = -1.0f;
    float ct  = config_get_awb_ct_estimate();
    float sat = config_get_saturation();

    FILE *f = fopen("/run/camcfgd/exposure", "w");
    if (f) {
        fprintf(f, "exposure_us=%u\ngain_db=%.1f\ngain_code=%u\n"
                   "ae_exposure_auto=%d\nae_gain_auto=%d\nae_active=%d\nae_once_running=%d\n"
                   "stream_active=%d\nstream_owner=%d\nresulting_fps=%.3f\nae_converged=%d\n"
                   "awb_active=%d\nawb_auto=%d\nawb_ratio_r_x1000=%u\nawb_ratio_b_x1000=%u\n"
                   "lux=%.1f\nawb_ct_est_k=%u\nsaturation_x100=%u\n",
                exp_us, gain_code_to_db(gain), gain,
                ae_exp ? 1 : 0, ae_gain ? 1 : 0, ae_active ? 1 : 0, ae_once ? 1 : 0,
                streaming ? 1 : 0, owner, res_fps,
                (ae_active && converged) ? 1 : 0,
                awb_active ? 1 : 0, awb_auto ? 1 : 0,
                (unsigned)(awb_r * 1000.0f + 0.5f), (unsigned)(awb_b * 1000.0f + 0.5f),
                lux, (unsigned)(ct > 0.0f ? ct + 0.5f : 0.0f),
                (unsigned)(sat * 100.0f + 0.5f));
        fclose(f);
    }

    publish_readback(exp_us, gain,
                     ae_exp && streaming, ae_gain && streaming,
                     ae_active && converged, res_fps,
                     pixfmt_usb, cfa_order, flip_echo);
}

static void set_defaults(void)
{
    memset(&g_state, 0, sizeof(g_state));

    g_state.sensor_cfg.width        = 2032;
    g_state.sensor_cfg.height       = 1080;
    g_state.sensor_cfg.pixel_format = GVSP_PIX_BAYERRG12;
    g_state.sensor_cfg.fps          = 30;
    g_state.fps_precise             = 30.0f;
    g_state.sensor_cfg.exposure_us  = 10000;
    g_state.sensor_cfg.analog_gain  = 0;
    g_state.sensor_cfg.black_level  = CFG_BLACK_LEVEL_DEFAULT;
    g_state.sensor_cfg.stream_enable = 0;

    g_state.sensor_cfg_source = CFG_SRC_NONE;
    g_state.stream_active     = false;

    g_state.pixfmt_usb = GVSP_PIX_BAYERRG12P;
    g_state.pixfmt_eth = GVSP_PIX_BAYERRG12P;

    g_state.imaging.hdmi_max_fps         = 25;
    g_state.imaging.hdmi_crop_auto       = 1;
    g_state.imaging.hdmi_crop_x          = 0;
    g_state.imaging.hdmi_crop_y          = 0;
    g_state.imaging.ae_target_brightness = 0.5f;
    g_state.imaging.ae_metering_mode     = 1;
    g_state.imaging.ae_speed             = 40;
    g_state.imaging.ae_priority          = 0;
    g_state.imaging.ae_highlight         = 1;
    g_state.imaging.ae_flicker           = 0;
    g_state.imaging.ae_once              = 0;

    g_state.config_version = 0;
}

void config_init(void)
{
    pthread_mutex_init(&g_lock, NULL);

    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC);
    pthread_cond_init(&g_dirty_cond, &cattr);
    pthread_condattr_destroy(&cattr);

    g_dirty_flags = 0;
    g_shutdown = false;
    set_defaults();
    publish_stream_active(false);
}

void config_destroy(void)
{
    pthread_cond_destroy(&g_dirty_cond);
    pthread_mutex_destroy(&g_lock);
}

static void mark_dirty(uint32_t flags)
{
    g_dirty_flags |= flags;
    pthread_cond_signal(&g_dirty_cond);
}

void config_set_sensor(const imgsensor_cfg_t *cfg, uint8_t source)
{
    pthread_mutex_lock(&g_lock);

    if (source == CFG_SRC_USB || source == CFG_SRC_GIGE) {
        g_stream_owner_kind = SOWN_HOST;
        g_host_seen         = true;
    } else if (source == CFG_SRC_WEB && g_stream_owner_kind == SOWN_HOST) {
        static time_t last_warn;
        time_t now = time(NULL);
        if (now - last_warn >= 10) {
            last_warn = now;
            LOG_WARN("config: web/standalone config ignored -- a USB/GigE host "
                     "owns the pipeline (host always wins)");
        }
        pthread_mutex_unlock(&g_lock);
        return;
    }

    bool res_changed = (cfg->width    != g_state.sensor_cfg.width    ||
                        cfg->height   != g_state.sensor_cfg.height   ||
                        cfg->offset_x != g_state.sensor_cfg.offset_x ||
                        cfg->offset_y != g_state.sensor_cfg.offset_y ||
                        cfg->binning  != g_state.sensor_cfg.binning);

    bool pixfmt_changed = false;
    uint32_t prev_pixfmt_usb = g_state.pixfmt_usb;
    uint32_t prev_pixfmt_eth = g_state.pixfmt_eth;
    bool     prev_usb_active = g_state.usb_active;
    bool     prev_eth_active = g_state.eth_active;
    if (pixfmt_is_valid(cfg->pixel_format)) {
        uint32_t pf = ingest_pixfmt(cfg->pixel_format);

        if (source == CFG_SRC_USB) {
            g_state.usb_active = true;
            g_state.eth_active = false;
        } else if (source == CFG_SRC_GIGE) {
            g_state.eth_active = true;
            g_state.usb_active = false;
        }

        if (g_state.pixfmt_usb != pf || g_state.pixfmt_eth != pf) {
            g_state.pixfmt_usb = pf;
            g_state.pixfmt_eth = pf;
            pixfmt_changed = true;
        }
    }

    imgsensor_cfg_t locked_cfg;
    if (fmt_locked_now() && (res_changed || pixfmt_changed) &&
        (source == CFG_SRC_USB || source == CFG_SRC_GIGE)) {
        LOG_WARN("config: image format is locked while streaming -- refused "
                 "%ux%u bin=0x%02X pfnc=0x%08X, keeping %ux%u bin=0x%02X "
                 "pfnc=0x%08X (stop acquisition to change it)",
                 cfg->width, cfg->height, cfg->binning, cfg->pixel_format,
                 g_state.sensor_cfg.width, g_state.sensor_cfg.height,
                 g_state.sensor_cfg.binning, g_state.sensor_cfg.pixel_format);
        locked_cfg              = *cfg;
        locked_cfg.width        = g_state.sensor_cfg.width;
        locked_cfg.height       = g_state.sensor_cfg.height;
        locked_cfg.offset_x     = g_state.sensor_cfg.offset_x;
        locked_cfg.offset_y     = g_state.sensor_cfg.offset_y;
        locked_cfg.binning      = g_state.sensor_cfg.binning;
        locked_cfg.pixel_format = g_state.sensor_cfg.pixel_format;
        g_state.pixfmt_usb = prev_pixfmt_usb;
        g_state.pixfmt_eth = prev_pixfmt_eth;
        g_state.usb_active = prev_usb_active;
        g_state.eth_active = prev_eth_active;
        cfg            = &locked_cfg;
        res_changed    = false;
        pixfmt_changed = false;
    }

    uint8_t saved_stream = g_state.sensor_cfg.stream_enable;
    memcpy(&g_state.sensor_cfg, cfg, sizeof(*cfg));
    g_state.sensor_cfg.stream_enable = saved_stream;
    g_state.sensor_cfg.pixel_format  = ingest_pixfmt(cfg->pixel_format);
    g_state.sensor_cfg_source = source;
    g_state.fps_precise = (float)cfg->fps;

    uint32_t flags = DIRTY_SENSOR_CTRL;

    if (res_changed) {
        if (g_state.stream_active) {
            flags |= DIRTY_PIPELINE_FMT;
        } else {
            g_state.fmt_pending = true;
        }
    }

    if (pixfmt_changed) {
        if (g_state.stream_active) {
            flags |= DIRTY_PIXPACK;
        } else {
            g_state.pixpack_pending = true;
        }
    }

    mark_dirty(flags);
    pthread_mutex_unlock(&g_lock);
}

void config_set_exposure_us(uint32_t exposure_us)
{
    pthread_mutex_lock(&g_lock);
    g_state.sensor_cfg.exposure_us = exposure_us;
    mark_dirty(DIRTY_SENSOR_CTRL);
    pthread_mutex_unlock(&g_lock);
}

void config_set_gain_raw(uint16_t gain)
{
    pthread_mutex_lock(&g_lock);
    g_state.sensor_cfg.analog_gain = gain;
    mark_dirty(DIRTY_SENSOR_CTRL);
    pthread_mutex_unlock(&g_lock);
}

uint16_t config_get_black_level(void)
{
    return g_state.sensor_cfg.black_level;
}

void config_set_black_level(uint16_t level)
{
    pthread_mutex_lock(&g_lock);
    g_state.sensor_cfg.black_level = level;
    mark_dirty(DIRTY_SENSOR_CTRL);
    pthread_mutex_unlock(&g_lock);
}

void config_set_test_pattern(uint8_t pat)
{
    pthread_mutex_lock(&g_lock);
    g_state.sensor_cfg.test_pattern = pat;
    mark_dirty(DIRTY_SENSOR_CTRL);
    pthread_mutex_unlock(&g_lock);
}

void config_set_resolution(uint16_t w, uint16_t h)
{
    pthread_mutex_lock(&g_lock);
    if (w != g_state.sensor_cfg.width || h != g_state.sensor_cfg.height) {
        if (fmt_locked_now()) {
            LOG_WARN("config: resolution locked while host is acquiring -- "
                     "refused %ux%u, keeping %ux%u", w, h,
                     g_state.sensor_cfg.width, g_state.sensor_cfg.height);
            pthread_mutex_unlock(&g_lock);
            return;
        }
        g_state.sensor_cfg.width  = w;
        g_state.sensor_cfg.height = h;
        if (g_state.stream_active) {
            mark_dirty(DIRTY_PIPELINE_FMT | DIRTY_SENSOR_CTRL);
        } else {
            g_state.fmt_pending = true;
            mark_dirty(DIRTY_SENSOR_CTRL);
        }
    }
    pthread_mutex_unlock(&g_lock);
}

void config_set_offset(uint16_t x, uint16_t y)
{
    pthread_mutex_lock(&g_lock);
    if (x != g_state.sensor_cfg.offset_x || y != g_state.sensor_cfg.offset_y) {
        if (fmt_locked_now()) {
            LOG_WARN("config: offset locked while host is acquiring -- "
                     "refused %u,%u", x, y);
            pthread_mutex_unlock(&g_lock);
            return;
        }
        g_state.sensor_cfg.offset_x = x;
        g_state.sensor_cfg.offset_y = y;
        if (g_state.stream_active) {
            mark_dirty(DIRTY_PIPELINE_FMT);
        } else {
            g_state.fmt_pending = true;
        }
    }
    pthread_mutex_unlock(&g_lock);
}

void config_set_fps(uint16_t fps)
{
    pthread_mutex_lock(&g_lock);
    g_state.sensor_cfg.fps = fps;
    g_state.fps_precise = (float)fps;
    mark_dirty(DIRTY_SENSOR_CTRL);
    pthread_mutex_unlock(&g_lock);
}

void config_set_fps_f(float fps)
{
    pthread_mutex_lock(&g_lock);
    g_state.sensor_cfg.fps = (uint16_t)(fps + 0.5f);
    g_state.fps_precise = fps;
    mark_dirty(DIRTY_SENSOR_CTRL);
    pthread_mutex_unlock(&g_lock);
}

float config_get_fps_f(void)
{
    pthread_mutex_lock(&g_lock);
    float f = g_state.fps_precise;
    pthread_mutex_unlock(&g_lock);
    return f;
}

void config_set_max_fps(float fps)
{
    pthread_mutex_lock(&g_lock);
    g_state.max_fps = fps;
    pthread_mutex_unlock(&g_lock);
}
void config_set_actual_fps(float fps)
{
    pthread_mutex_lock(&g_lock);
    g_actual_fps = fps;
    pthread_mutex_unlock(&g_lock);
}

void config_set_pixfmt_usb(uint32_t pfnc)
{
    pthread_mutex_lock(&g_lock);
    pfnc = ingest_pixfmt(pfnc);
    g_state.usb_active = true;
    if ((g_state.pixfmt_usb != pfnc || g_state.pixfmt_eth != pfnc)
        && fmt_locked_now()) {
        LOG_WARN("config: pixel format locked while host is acquiring -- "
                 "refused 0x%08X (from USB)", pfnc);
        pthread_mutex_unlock(&g_lock);
        return;
    }
    if (g_state.pixfmt_usb != pfnc || g_state.pixfmt_eth != pfnc) {
        g_state.pixfmt_usb = pfnc;
        g_state.pixfmt_eth = pfnc;
        if (g_state.stream_active) {
            mark_dirty(DIRTY_PIXPACK);
        } else {
            g_state.pixpack_pending = true;
        }
        LOG_INFO("config: pixfmt = 0x%08X (from USB)%s", pfnc,
                 g_state.stream_active ? "" : " (pending)");
    }
    pthread_mutex_unlock(&g_lock);
}

void config_set_pixfmt_eth(uint32_t pfnc)
{
    pthread_mutex_lock(&g_lock);
    pfnc = ingest_pixfmt(pfnc);
    g_state.eth_active = true;
    if ((g_state.pixfmt_usb != pfnc || g_state.pixfmt_eth != pfnc)
        && fmt_locked_now()) {
        LOG_WARN("config: pixel format locked while host is acquiring -- "
                 "refused 0x%08X (from ETH)", pfnc);
        pthread_mutex_unlock(&g_lock);
        return;
    }
    if (g_state.pixfmt_usb != pfnc || g_state.pixfmt_eth != pfnc) {
        g_state.pixfmt_usb = pfnc;
        g_state.pixfmt_eth = pfnc;
        if (g_state.stream_active) {
            mark_dirty(DIRTY_PIXPACK);
        } else {
            g_state.pixpack_pending = true;
        }
        LOG_INFO("config: pixfmt = 0x%08X (from ETH)%s", pfnc,
                 g_state.stream_active ? "" : " (pending)");
    }
    pthread_mutex_unlock(&g_lock);
}

void config_set_stream(bool enable)
{
    pthread_mutex_lock(&g_lock);
    g_state.sensor_cfg.stream_enable = enable ? 1 : 0;
    g_stream_owner_kind = enable ? SOWN_HOST : SOWN_NONE;
    g_host_stream_on    = enable;
    if (enable)
        g_host_seen = true;
    mark_dirty(DIRTY_STREAM);
    pthread_mutex_unlock(&g_lock);
}

void config_set_stream_active(bool active)
{
    pthread_mutex_lock(&g_lock);
    bool changed = (g_state.stream_active != active);
    g_state.stream_active = active;
    pthread_mutex_unlock(&g_lock);
    if (changed)
        publish_stream_active(active);
}
static uint16_t g_ae_roi_x, g_ae_roi_y, g_ae_roi_w, g_ae_roi_h;

void config_set_ae_roi(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    pthread_mutex_lock(&g_lock);
    g_ae_roi_x = x; g_ae_roi_y = y; g_ae_roi_w = w; g_ae_roi_h = h;
    pthread_mutex_unlock(&g_lock);
}

void config_get_ae_roi(uint16_t *x, uint16_t *y, uint16_t *w, uint16_t *h)
{
    pthread_mutex_lock(&g_lock);
    if (x) *x = g_ae_roi_x;
    if (y) *y = g_ae_roi_y;
    if (w) *w = g_ae_roi_w;
    if (h) *h = g_ae_roi_h;
    pthread_mutex_unlock(&g_lock);
}

void config_set_ae_target(float target01)
{
    if (target01 < 0.0f) target01 = 0.0f;
    if (target01 > 1.0f) target01 = 1.0f;
    pthread_mutex_lock(&g_lock);
    g_state.imaging.ae_target_brightness = target01;
    pthread_mutex_unlock(&g_lock);
}

void config_set_ae_metering_mode(uint8_t mode)
{
    if (mode > 5) return;
    pthread_mutex_lock(&g_lock);
    g_state.imaging.ae_metering_mode = mode;
    pthread_mutex_unlock(&g_lock);
}

void config_mark_stream_dirty(void)
{
    pthread_mutex_lock(&g_lock);
    mark_dirty(DIRTY_STREAM);
    pthread_mutex_unlock(&g_lock);
}

static struct camio_cfg g_camio;

void config_set_camio(const struct camio_cfg *c)
{
    pthread_mutex_lock(&g_lock);
    g_camio = *c;
    mark_dirty(DIRTY_CAMIO);
    pthread_mutex_unlock(&g_lock);
}

void config_get_camio(struct camio_cfg *out)
{
    pthread_mutex_lock(&g_lock);
    *out = g_camio;
    pthread_mutex_unlock(&g_lock);
}

void config_publish_camio(void)
{
    struct camio_cfg c;
    config_get_camio(&c);

    FILE *f = fopen("/run/camcfgd/camio", "w");
    if (!f)
        return;
    unsigned enable_eff = c.present && (c.in_function || c.strobe_enable ||
                                        c.out_source  || c.sync_role);

    int st_locked = -1, st_optoin = -1;
    if (camio_sync_hw_state() == 1) {
        uint32_t status = 0, in_status = 0, sync_status = 0;
        if (camio_sync_read_status(&status, &in_status, &sync_status) == 0) {
            st_locked = (status & (1u << 0)) ? 1 : 0;
            st_optoin = (status & (1u << 4)) ? 1 : 0;
        }
    }

    fprintf(f,
            "present=%u\nenable=%u\nblock_present=%d\n"
            "st_locked=%d\nst_optoin=%d\n"
            "in_function=%u\nin_activation=%u\nin_invert=%u\n"
            "trigger_delay_us=%u\ntrigger_divider=%u\n"
            "strobe_enable=%u\nstrobe_invert=%u\nstrobe_src=%u\n"
            "strobe_delay_us=%u\n"
            "strobe_duration_us=%u\nstrobe_minon_us=%u\n"
            "out_source=%u\nout_invert=%u\nout_user_value=%u\n"
            "sync_role=%u\nsync_role_by=%u\n"
            "sync_trig_route=%u\n"
            "cap_has_xvs=%u\ncap_xvs_out=%u\ncap_xvs_in=%u\n"
            "cap_has_xtrig=%u\ncap_has_strobe=%u\ncap_ptp_pulse=%d\n",
            c.present, enable_eff, camio_sync_hw_state(),
            st_locked, st_optoin,
            c.in_function, c.in_activation, c.in_invert,
            c.trigger_delay_us, c.trigger_divider,
            c.strobe_enable, c.strobe_invert, c.strobe_src,
            c.strobe_delay_us,
            c.strobe_duration_us, c.strobe_minon_us,
            c.out_source, c.out_invert, c.out_user_value,
            c.sync_role, c.sync_role_by,
            c.sync_trig_route,
            g_camio_cap_has_xvs, g_camio_cap_xvs_out, g_camio_cap_xvs_in,
            g_camio_cap_has_xtrig, g_camio_cap_has_strobe,
            camio_sync_has_ptp_pulse());
    fclose(f);
}
void config_clear_pending(void)
{
    pthread_mutex_lock(&g_lock);
    g_state.fmt_pending = false;
    g_state.pixpack_pending = false;
    pthread_mutex_unlock(&g_lock);
}

static void ae_latch_exposure_locked(bool was_auto, bool now_auto)
{
    if (was_auto && !now_auto && g_state.stream_active)
        g_state.sensor_cfg.exposure_us = g_state.ae_current_exposure_us;
}

static void ae_latch_gain_locked(bool was_auto, bool now_auto)
{
    if (was_auto && !now_auto && g_state.stream_active)
        g_state.sensor_cfg.analog_gain = g_state.ae_current_gain;
}

void config_apply_imaging(uint16_t hdmi_max_fps, uint16_t hdmi_crop_x,
                          uint16_t hdmi_crop_y, float ae_target,
                          uint8_t ae_metering_mode, uint8_t hdmi_standalone,
                          uint8_t ae_force_enable, uint8_t hdmi_enable,
                          uint8_t ae_speed, uint8_t ae_priority,
                          uint8_t ae_highlight, uint8_t ae_flicker,
                          uint8_t ae_once, uint32_t exposure_us,
                          uint8_t awb_force_enable, uint8_t awb_gain_min_x100,
                          uint8_t awb_gain_max_x10, uint8_t awb_rate_x100,
                          uint8_t awb_color_temp_d100, uint8_t hdmi_crop_auto)
{
    if (hdmi_max_fps > HDMI_FPS_MAX) hdmi_max_fps = HDMI_FPS_MAX;
    if (hdmi_max_fps == 0)           hdmi_max_fps = HDMI_FPS_MAX;
    if (ae_target < 0.0f) ae_target = 0.0f;
    if (ae_target > 1.0f) ae_target = 1.0f;
    if (ae_metering_mode > 5) ae_metering_mode = 1;
    if (ae_speed == 0)   ae_speed = 40;
    if (ae_speed > 100)  ae_speed = 100;
    if (ae_priority > 1) ae_priority = 0;
    if (ae_highlight > 2) ae_highlight = 1;
    if (ae_flicker > 2) ae_flicker = 0;
    if (hdmi_standalone > 1) hdmi_standalone = 0;
    if (ae_force_enable > 1) ae_force_enable = 0;
    if (awb_force_enable > 1) awb_force_enable = 0;
    if (hdmi_enable > 1) hdmi_enable = 1;

    if (!hdmi_enable) hdmi_standalone = 0;

    pthread_mutex_lock(&g_lock);

    bool hdmi_en_changed = (hdmi_enable != g_hdmi_enable);
    g_hdmi_enable     = hdmi_enable;
    g_hdmi_standalone = hdmi_standalone;

    bool ae_force_edge     = (ae_force_enable && !g_prev_ae_force_enable);
    bool ae_unforce_edge   = (!ae_force_enable && g_prev_ae_force_enable);
    g_prev_ae_force_enable = ae_force_enable;

    bool awb_force_edge     = (awb_force_enable && !g_prev_awb_force_enable);
    bool awb_unforce_edge   = (!awb_force_enable && g_prev_awb_force_enable);
    g_prev_awb_force_enable = awb_force_enable;

    static uint8_t prev_ae_once = 0;
    static bool    ae_once_init = false;
    bool ae_once_edge = false;
    if (!ae_once_init) {
        ae_once_init = true;
        prev_ae_once = ae_once;
    } else if (ae_once != prev_ae_once) {
        prev_ae_once  = ae_once;
        ae_once_edge  = true;
    }

    bool fps_changed  = (hdmi_max_fps != g_state.imaging.hdmi_max_fps);
    static float prev_web_target = -1.0f;
    bool ae_changed   = (ae_target != prev_web_target);
    prev_web_target   = ae_target;
    static uint16_t prev_web_metering = 0xFFFF;
    bool meter_web_moved = (ae_metering_mode != prev_web_metering);
    prev_web_metering    = ae_metering_mode;
    bool crop_changed = (hdmi_crop_x    != g_state.imaging.hdmi_crop_x ||
                         hdmi_crop_y    != g_state.imaging.hdmi_crop_y ||
                         hdmi_crop_auto != g_state.imaging.hdmi_crop_auto);

    g_state.imaging.hdmi_max_fps         = hdmi_max_fps;
    g_state.imaging.hdmi_crop_auto       = hdmi_crop_auto;
    g_state.imaging.hdmi_crop_x          = hdmi_crop_x;
    g_state.imaging.hdmi_crop_y          = hdmi_crop_y;
    if (ae_changed) g_state.imaging.ae_target_brightness = ae_target;
    if (meter_web_moved) g_state.imaging.ae_metering_mode = ae_metering_mode;
    g_state.imaging.ae_speed             = ae_speed;
    g_state.imaging.ae_priority          = ae_priority;
    g_state.imaging.ae_highlight         = ae_highlight;
    g_state.imaging.ae_flicker           = ae_flicker;
    g_state.imaging.ae_once              = ae_once;

    uint32_t flags = 0;
    if (fps_changed) {
        flags |= DIRTY_SENSOR_CTRL;
        if (hdmi_standalone && !g_state.usb_active && !g_state.eth_active) {
            g_state.sensor_cfg.fps = hdmi_max_fps;
            g_state.fps_precise    = (float)hdmi_max_fps;
        }
    }
    if (hdmi_en_changed) flags |= DIRTY_SENSOR_CTRL;
    static uint32_t prev_web_exp_us = 0;
    static uint8_t  prev_standalone = 0;
    bool exp_changed = (exposure_us != prev_web_exp_us);
    bool standalone_edge = (hdmi_standalone && !prev_standalone);
    prev_web_exp_us  = exposure_us;
    prev_standalone  = hdmi_standalone;
    if (exposure_us > 0 && (exp_changed || standalone_edge) && hdmi_standalone &&
        !g_state.usb_active && !g_state.eth_active &&
        !g_state.ae_exposure_auto) {
        g_state.sensor_cfg.exposure_us = exposure_us;
        flags |= DIRTY_SENSOR_CTRL;
    }
    if (ae_changed)  flags |= DIRTY_AE_CFG;
    if (ae_force_edge) {
        if (!g_state.ae_exposure_auto)
            g_state.ae_current_exposure_us = g_state.sensor_cfg.exposure_us;
        if (!g_state.ae_gain_auto)
            g_state.ae_current_gain = g_state.sensor_cfg.analog_gain;
        g_state.ae_exposure_auto = false;
        g_state.ae_gain_auto     = false;
        flags |= DIRTY_AE_CFG;
    }
    if (awb_force_edge)   config_force_awb_locked(true);
    if (awb_unforce_edge) config_force_awb_locked(false);

    {
        static bool    awb_tune_init = false;
        static uint8_t p_min = 0, p_max = 0, p_rate = 0;
        if (!awb_tune_init || awb_gain_max_x10 != p_max) {
            if (awb_gain_max_x10) { g_awb_max_ovr = true;  config_set_awb_gain_max((float)awb_gain_max_x10 / 10.0f); }
            else                  { g_awb_max_ovr = false; config_set_awb_gain_max(g_awb_baked_gain_max); }
        }
        if (!awb_tune_init || awb_gain_min_x100 != p_min) {
            if (awb_gain_min_x100) { g_awb_min_ovr = true;  config_set_awb_gain_min((float)awb_gain_min_x100 / 100.0f); }
            else                   { g_awb_min_ovr = false; config_set_awb_gain_min(g_awb_baked_gain_min); }
        }
        if (!awb_tune_init || awb_rate_x100 != p_rate) {
            if (awb_rate_x100) { g_awb_rate_ovr = true;  config_set_awb_damp((float)awb_rate_x100 / 100.0f); }
            else               { g_awb_rate_ovr = false; config_set_awb_damp(g_awb_baked_damp); }
        }
        awb_tune_init = true;
        p_min = awb_gain_min_x100; p_max = awb_gain_max_x10; p_rate = awb_rate_x100;
    }
    static bool    ct_init = false;
    static uint8_t prev_ct = 0;
    bool     ct_do     = ct_init ? (awb_color_temp_d100 != prev_ct)
                                 : (awb_color_temp_d100 != 0);
    uint16_t ct_kelvin = (uint16_t)awb_color_temp_d100 * 100u;
    ct_init = true;
    prev_ct = awb_color_temp_d100;

    if (ae_unforce_edge) {
        bool was_fexp  = g_state.ae_exposure_auto;
        bool was_fgain = g_state.ae_gain_auto;
        g_state.ae_exposure_auto = false;
        g_state.ae_gain_auto     = false;
        ae_latch_exposure_locked(was_fexp,  g_state.ae_exposure_auto);
        ae_latch_gain_locked(was_fgain,     g_state.ae_gain_auto);
        g_state.ae_once_exposure = false;
        g_state.ae_once_gain     = false;
        flags |= DIRTY_AE_CFG;
    }
    if (ae_once_edge) {
        g_state.ae_exposure_auto = false;
        g_state.ae_gain_auto     = false;
        g_state.ae_once_exposure = false;
        g_state.ae_once_gain     = false;
        flags |= DIRTY_AE_CFG;
    }
    if (crop_changed) {
        if (g_state.stream_active) flags |= DIRTY_PIPELINE_FMT;
        else                       g_state.fmt_pending = true;
    }
    if (flags)
        mark_dirty(flags);

    pthread_mutex_unlock(&g_lock);

    if (ct_do)
        config_set_color_temp(ct_kelvin);
}
void config_set_auto_exposure(bool enable)
{
    pthread_mutex_lock(&g_lock);
    bool was_exp = g_state.ae_exposure_auto;
    bool changed = (g_state.ae_exposure_auto != enable) || g_state.ae_once_exposure;
    g_state.ae_exposure_auto = false;
    g_host_ae_exposure       = enable;
    g_state.ae_once_exposure = false;
    if (!was_exp && enable)
        g_state.ae_current_exposure_us = g_state.sensor_cfg.exposure_us;
    ae_latch_exposure_locked(was_exp, enable);
    if (changed) {
        mark_dirty(DIRTY_AE_CFG);
        LOG_INFO("config: auto exposure %s", enable ? "ENABLED" : "DISABLED");
    }
    pthread_mutex_unlock(&g_lock);
}

void config_set_auto_gain(bool enable)
{
    pthread_mutex_lock(&g_lock);
    bool was_gain = g_state.ae_gain_auto;
    bool changed = (g_state.ae_gain_auto != enable) || g_state.ae_once_gain;
    g_state.ae_gain_auto = false;
    g_host_ae_gain       = enable;
    g_state.ae_once_gain = false;
    if (!was_gain && enable)
        g_state.ae_current_gain = g_state.sensor_cfg.analog_gain;
    ae_latch_gain_locked(was_gain, enable);
    if (changed) {
        mark_dirty(DIRTY_AE_CFG);
        LOG_INFO("config: auto gain %s", enable ? "ENABLED" : "DISABLED");
    }
    pthread_mutex_unlock(&g_lock);
}

void config_get_full(struct camcfg_state_s *out)
{
    pthread_mutex_lock(&g_lock);
    memcpy(out, &g_state, sizeof(*out));
    pthread_mutex_unlock(&g_lock);
}

void config_get_sensor(imgsensor_cfg_t *out)
{
    pthread_mutex_lock(&g_lock);
    memcpy(out, &g_state.sensor_cfg, sizeof(*out));
    pthread_mutex_unlock(&g_lock);
}

bool config_get_stream_active(void)
{
    pthread_mutex_lock(&g_lock);
    bool active = g_state.stream_active;
    pthread_mutex_unlock(&g_lock);
    return active;
}

bool config_get_host_active(void)
{
    pthread_mutex_lock(&g_lock);
    bool active = g_state.usb_active || g_state.eth_active;
    pthread_mutex_unlock(&g_lock);
    return active;
}

bool config_get_usb_active(void)
{
    pthread_mutex_lock(&g_lock);
    bool v = g_state.usb_active;
    pthread_mutex_unlock(&g_lock);
    return v;
}

void config_set_ae_exp_lower_us(uint32_t us)
{
    pthread_mutex_lock(&g_lock);
    g_ae_exp_lower_us = us;
    pthread_mutex_unlock(&g_lock);
}

void config_set_ae_exp_upper_us(uint32_t us)
{
    pthread_mutex_lock(&g_lock);
    g_ae_exp_upper_us = us;
    pthread_mutex_unlock(&g_lock);
}
void config_set_link_bw_limit(int link, uint32_t bps)
{
    if (link < 0 || link > 1) return;
    pthread_mutex_lock(&g_lock);
    bool changed = (g_link_bw_limit[link] != bps);
    g_link_bw_limit[link] = bps;
    if (changed) mark_dirty(DIRTY_SENSOR_CTRL);
    pthread_mutex_unlock(&g_lock);
    if (changed)
        LOG_INFO("config: DeviceLinkThroughputLimit[%s] = %u B/s",
                 link == CFG_LINK_USB ? "usb" : "eth", bps);
}

void config_set_link_bw_mode(int link, bool on)
{
    if (link < 0 || link > 1) return;
    pthread_mutex_lock(&g_lock);
    bool changed = (g_link_bw_on[link] != on);
    g_link_bw_on[link] = on;
    if (changed) mark_dirty(DIRTY_SENSOR_CTRL);
    pthread_mutex_unlock(&g_lock);
    if (changed)
        LOG_INFO("config: DeviceLinkThroughputLimitMode[%s] = %s",
                 link == CFG_LINK_USB ? "usb" : "eth", on ? "On" : "Off");
}

uint32_t config_get_link_bw_bps(int link)
{
    uint32_t hw = (link == CFG_LINK_ETH) ? BW_ETH_MAX : BW_USB3_MAX;
    if (link < 0 || link > 1) return hw;
    pthread_mutex_lock(&g_lock);
    uint32_t lim = g_link_bw_limit[link];
    bool     on  = g_link_bw_on[link];
    pthread_mutex_unlock(&g_lock);
    return (on && lim != 0 && lim < hw) ? lim : hw;
}

bool config_get_ae_enabled(void)
{
    pthread_mutex_lock(&g_lock);
    bool enabled = g_state.ae_exposure_auto;
    pthread_mutex_unlock(&g_lock);
    return enabled;
}

bool config_get_auto_gain(void)
{
    pthread_mutex_lock(&g_lock);
    bool enabled = g_state.ae_gain_auto;
    pthread_mutex_unlock(&g_lock);
    return enabled;
}

static bool g_awb_auto = false;
static bool g_awb_once;

static void config_force_awb_locked(bool enable)
{
    if (g_awb_auto != enable) {
        g_awb_auto = false;
        LOG_INFO("config: auto white-balance %s (force toggle)",
                 enable ? "ENABLED" : "DISABLED");
    }
    g_awb_once = false;
}

static bool g_reverse_x = false;
static bool g_reverse_y = false;

void config_set_reverse_x(bool enable)
{
    pthread_mutex_lock(&g_lock);
    if (g_reverse_x != enable && fmt_locked_now()) {
        LOG_WARN("config: ReverseX locked while host is acquiring -- refused");
        pthread_mutex_unlock(&g_lock);
        return;
    }
    if (g_reverse_x != enable) {
        g_reverse_x = enable;
        LOG_INFO("config: ReverseX %s", enable ? "ON" : "OFF");
        if (g_state.stream_active)
            mark_dirty(DIRTY_PIPELINE_FMT);
        else
            g_state.fmt_pending = true;
    }
    pthread_mutex_unlock(&g_lock);
}

void config_set_reverse_y(bool enable)
{
    pthread_mutex_lock(&g_lock);
    if (g_reverse_y != enable && fmt_locked_now()) {
        LOG_WARN("config: ReverseY locked while host is acquiring -- refused");
        pthread_mutex_unlock(&g_lock);
        return;
    }
    if (g_reverse_y != enable) {
        g_reverse_y = enable;
        LOG_INFO("config: ReverseY %s", enable ? "ON" : "OFF");
        if (g_state.stream_active)
            mark_dirty(DIRTY_PIPELINE_FMT);
        else
            g_state.fmt_pending = true;
    }
    pthread_mutex_unlock(&g_lock);
}

bool config_get_reverse_x(void) { return g_reverse_x; }
bool config_get_reverse_y(void) { return g_reverse_y; }

int config_get_effective_hflip(void)
{
    return (g_orient_hflip ? 1 : 0) ^ (g_reverse_x ? 1 : 0);
}

int config_get_effective_vflip(void)
{
    return (g_orient_vflip ? 1 : 0) ^ (g_reverse_y ? 1 : 0);
}

void config_set_auto_wb(bool enable)
{
    pthread_mutex_lock(&g_lock);
    if (g_awb_auto != enable) {
        g_awb_auto = false;
        LOG_INFO("config: auto white-balance %s", enable ? "ENABLED" : "DISABLED");
    }
    g_awb_once = false;
    pthread_mutex_unlock(&g_lock);
}

bool config_get_auto_wb(void)
{
    pthread_mutex_lock(&g_lock);
    bool enabled = g_awb_auto;
    pthread_mutex_unlock(&g_lock);
    return enabled;
}

static bool g_awb_once = false;

void config_request_awb_once(void)
{
    pthread_mutex_lock(&g_lock);
    g_awb_once = false;
    pthread_mutex_unlock(&g_lock);
}
static float g_wb_ratio_r = 1.0f;
static float g_wb_ratio_b = 1.0f;

void config_set_wb_ratio(int channel, float ratio)
{
    if (ratio < 0.05f) ratio = 0.05f;
    if (ratio > 16.0f) ratio = 16.0f;
    pthread_mutex_lock(&g_lock);
    if (channel == 0)      g_wb_ratio_r = ratio;
    else if (channel == 2) g_wb_ratio_b = ratio;
    pthread_mutex_unlock(&g_lock);
    LOG_INFO("config: BalanceRatio[%s] = %.3f",
             channel == 0 ? "Red" : channel == 2 ? "Blue" : "Green", ratio);
}
float config_get_wb_ratio(int channel)
{
    pthread_mutex_lock(&g_lock);
    float v = (channel == 0) ? g_wb_ratio_r : (channel == 2) ? g_wb_ratio_b : 1.0f;
    pthread_mutex_unlock(&g_lock);
    return v;
}
#define CT_NPRESET 4
static const uint16_t g_ct_kelvin[CT_NPRESET] = { 3200, 4800, 5600, 6500 };
static float g_ct_r[CT_NPRESET] = { 1.0f, 1.0f, 1.0f, 1.0f };
static float g_ct_b[CT_NPRESET] = { 1.0f, 1.0f, 1.0f, 1.0f };
static bool  g_ct_have_r[CT_NPRESET];
static bool  g_ct_have_b[CT_NPRESET];

static void ct_set_preset_gain(unsigned kelvin, int is_blue, float val)
{
    for (int i = 0; i < CT_NPRESET; i++)
        if (g_ct_kelvin[i] == kelvin) {
            if (is_blue) { g_ct_b[i] = val; g_ct_have_b[i] = true; }
            else         { g_ct_r[i] = val; g_ct_have_r[i] = true; }
            return;
        }
}

static void ct_reset_presets(void)
{
    for (int i = 0; i < CT_NPRESET; i++) {
        g_ct_r[i] = g_ct_b[i] = 1.0f;
        g_ct_have_r[i] = g_ct_have_b[i] = false;
    }
}
void config_set_color_temp(uint16_t kelvin)
{
    if (kelvin == 0) {
        config_set_auto_wb(true);
        LOG_INFO("config: colour temperature -> Auto (AWB continuous)");
        return;
    }
    for (int i = 0; i < CT_NPRESET; i++) {
        if (g_ct_kelvin[i] == kelvin) {
            config_set_auto_wb(false);
            config_set_wb_ratio(0, g_ct_r[i]);
            config_set_wb_ratio(2, g_ct_b[i]);
            config_set_awb_ct_estimate((float)kelvin);
            LOG_INFO("config: colour temperature -> %uK (R=%.2f B=%.2f)",
                     kelvin, g_ct_r[i], g_ct_b[i]);
            return;
        }
    }
    LOG_WARN("config: colour temperature %uK not a preset -- ignored", kelvin);
}
uint8_t config_get_ae_once(void)
{
    pthread_mutex_lock(&g_lock);
    uint8_t v = g_state.imaging.ae_once;
    pthread_mutex_unlock(&g_lock);
    return v;
}
void config_request_ae_once_exposure(void)
{
    pthread_mutex_lock(&g_lock);
    g_state.ae_once_exposure = false;
    g_host_ae_exposure       = false;
    mark_dirty(DIRTY_AE_CFG);
    pthread_mutex_unlock(&g_lock);
    LOG_INFO("config: ExposureAuto=Once -> one-shot (exposure off on converge)");
}

void config_request_ae_once_gain(void)
{
    pthread_mutex_lock(&g_lock);
    g_state.ae_once_gain = false;
    g_host_ae_gain       = false;
    mark_dirty(DIRTY_AE_CFG);
    pthread_mutex_unlock(&g_lock);
    LOG_INFO("config: GainAuto=Once -> one-shot (gain off on converge)");
}
uint8_t config_get_hdmi_standalone(void)
{
    pthread_mutex_lock(&g_lock);
    uint8_t v = g_hdmi_standalone;
    pthread_mutex_unlock(&g_lock);
    return v;
}

uint8_t config_get_hdmi_enable(void)
{
    pthread_mutex_lock(&g_lock);
    uint8_t v = g_hdmi_enable;
    pthread_mutex_unlock(&g_lock);
    return v;
}

void config_hdmi_standalone_tick(void)
{
    long now = config_mono_ms();
    int  act = 0;

    pthread_mutex_lock(&g_lock);
    bool want   = (g_hdmi_standalone != 0);
    bool active = g_state.stream_active;
    if (!want)
        g_host_seen = false;
    if (want && g_stream_owner_kind == SOWN_NONE && !g_host_seen) {
        g_stream_owner_kind = SOWN_STANDALONE;
        g_sa_tries          = 1;
        g_sa_next_try_ms    = now + HDMI_SA_RETRY_MS;
        act = 1;
    } else if (g_stream_owner_kind == SOWN_STANDALONE) {
        if (!want) {
            g_stream_owner_kind              = SOWN_NONE;
            g_state.sensor_cfg.stream_enable = 0;
            mark_dirty(DIRTY_STREAM);
            act = 3;
        } else if (!active && g_sa_tries < HDMI_SA_MAX_TRIES &&
                   now >= g_sa_next_try_ms) {
            g_sa_tries++;
            g_sa_next_try_ms                 = now + HDMI_SA_RETRY_MS;
            g_state.sensor_cfg.stream_enable = 1;
            mark_dirty(DIRTY_STREAM);
            act = 2;
        }
    }
    pthread_mutex_unlock(&g_lock);

    if (act == 1) {
        uint16_t hdmi_fps = config_get_hdmi_max_fps();
        imgsensor_cfg_t cfg;
        config_get_sensor(&cfg);
        cfg.width   = 1920;
        cfg.height  = 1080;
        cfg.fps     = hdmi_fps;

        cfg.binning = (g_binning_max_h >= 2 && g_binning_max_v >= 2)
                    ? 0x11
                    : 0;

        pthread_mutex_lock(&g_lock);
        bool still_ours = (g_stream_owner_kind == SOWN_STANDALONE);
        pthread_mutex_unlock(&g_lock);
        if (!still_ours) {
            LOG_INFO("hdmi-standalone: host claimed during start -- yielding");
            return;
        }

        config_set_sensor(&cfg, CFG_SRC_WEB);
        bool sa_ae = false, sa_awb = false;
        pthread_mutex_lock(&g_lock);
        if (g_stream_owner_kind == SOWN_STANDALONE) {
            g_state.sensor_cfg.stream_enable = 1;
            bool want_ae  = (g_prev_ae_force_enable  != 0);
            bool want_awb = (g_prev_awb_force_enable != 0);
            if (want_ae) {
                if (!g_state.ae_exposure_auto)
                    g_state.ae_current_exposure_us = g_state.sensor_cfg.exposure_us;
                if (!g_state.ae_gain_auto)
                    g_state.ae_current_gain = g_state.sensor_cfg.analog_gain;
            }
            g_state.ae_exposure_auto = false;
            g_state.ae_gain_auto     = false;
            config_force_awb_locked(want_awb);
            mark_dirty(DIRTY_STREAM | DIRTY_AE_CFG);
            sa_ae = want_ae; sa_awb = want_awb;
        }
        pthread_mutex_unlock(&g_lock);
        LOG_INFO("hdmi-standalone: starting 1920x1080 binned preview @%ufps "
                 "(no host; AE=%s AWB=%s per stored web settings)",
                 hdmi_fps, sa_ae ? "auto" : "manual",
                 sa_awb ? "auto" : "off/preset");
    } else if (act == 2) {
        if (g_sa_tries >= HDMI_SA_MAX_TRIES)
            LOG_WARN("hdmi-standalone: stream not up after %d tries; giving up "
                     "(toggle off then on to retry)", HDMI_SA_MAX_TRIES);
        else
            LOG_INFO("hdmi-standalone: stream not up, retry %d/%d",
                     g_sa_tries, HDMI_SA_MAX_TRIES);
    } else if (act == 3) {
        LOG_INFO("hdmi-standalone: stopped (disabled)");
    }
}

uint16_t config_get_hdmi_max_fps(void)
{
    pthread_mutex_lock(&g_lock);
    uint16_t fps = g_state.imaging.hdmi_max_fps;
    pthread_mutex_unlock(&g_lock);
    return fps;
}

void config_get_hdmi_crop(uint16_t *x, uint16_t *y, uint8_t *automatic)
{
    pthread_mutex_lock(&g_lock);
    if (x) *x = g_state.imaging.hdmi_crop_x;
    if (y) *y = g_state.imaging.hdmi_crop_y;
    if (automatic) *automatic = g_state.imaging.hdmi_crop_auto;
    pthread_mutex_unlock(&g_lock);
}

uint32_t config_wait_dirty(void)
{
    struct timespec ts;
    pthread_mutex_lock(&g_lock);

    if (g_dirty_flags == 0 && !g_shutdown) {
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_nsec += 500000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec  += 1;
            ts.tv_nsec -= 1000000000;
        }
        pthread_cond_timedwait(&g_dirty_cond, &g_lock, &ts);
    }

    uint32_t flags = g_dirty_flags;
    g_dirty_flags = 0;
    pthread_mutex_unlock(&g_lock);
    return flags;
}

void config_signal_shutdown(void)
{
    pthread_mutex_lock(&g_lock);
    g_shutdown = true;
    pthread_cond_broadcast(&g_dirty_cond);
    pthread_mutex_unlock(&g_lock);
}

uint32_t config_peek_dirty(void)
{
    pthread_mutex_lock(&g_lock);
    uint32_t flags = g_dirty_flags;
    g_dirty_flags = 0;
    pthread_mutex_unlock(&g_lock);
    return flags;
}
