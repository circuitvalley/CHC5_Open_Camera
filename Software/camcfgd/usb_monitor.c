// SPDX-License-Identifier: CC-BY-NC-ND-4.0
/*
 * usb_monitor.c - Poll I2C slave chardev for USB camera config
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <stddef.h>
#include <stdbool.h>

#include "common.h"
#include "config.h"
#include "camio_sync.h"
#include "chc5_camio.h"
#include "media_pipeline.h"
#include "usb_monitor.h"
#include <poll.h>

#define USB_NOTIFY_TIMEOUT_MS  1000
#define USB_COALESCE_MS        50

static void usb_wait_next(int fd, bool *first)
{
    if (*first) { *first = false; return; }

    usleep(USB_COALESCE_MS * 1000);

    struct pollfd pw = { .fd = fd, .events = POLLPRI | POLLERR };
    (void)poll(&pw, 1, USB_NOTIFY_TIMEOUT_MS);
}

static bool cfg_field_written(const uint16_t *cnt, const uint16_t *prev,
                              size_t off, size_t len)
{
    size_t i;
    for (i = off; i < off + len; i++)
        if (cnt[i] != prev[i])
            return true;
    return false;
}

static pthread_t g_thread;
static volatile int g_running;

void usb_cfg_from_wire(const chc5_i2c_cfg_t *w, imgsensor_cfg_t *c)
{
    uint8_t bx = (uint8_t)((w->tp_bin >> CHC5_TPBIN_BINX_SHIFT) & CHC5_TPBIN_BIN_MASK);
    uint8_t by = (uint8_t)((w->tp_bin >> CHC5_TPBIN_BINY_SHIFT) & CHC5_TPBIN_BIN_MASK);

    memset(c, 0, sizeof(*c));
    c->width             = w->width;
    c->height            = w->height;
    c->offset_x          = w->offset_x;
    c->offset_y          = w->offset_y;
    c->exposure_us       = w->exposure_us;
    c->analog_gain       = w->analog_gain;
    c->fps_mhz           = w->fps_mhz;
    c->test_pattern      = (uint8_t)(w->tp_bin & CHC5_TPBIN_TP_MASK);
    c->stream_enable     = w->stream_enable;
    c->binning           = (uint8_t)((bx << 4) | by);
    c->pixel_format      = w->pixel_format;
    c->black_level       = w->black_level;
    c->auto_flags        = w->auto_flags;
    c->ae_metering_mode  = w->ae_metering_mode;
    c->ae_target         = w->ae_target;
    c->ae_exp_lower_us   = w->ae_exp_lower_us;
    c->ae_exp_upper_us   = w->ae_exp_upper_us;
    c->ae_roi_x          = w->ae_roi_x;
    c->ae_roi_y          = w->ae_roi_y;
    c->ae_roi_w          = w->ae_roi_w;
    c->ae_roi_h          = w->ae_roi_h;
    c->balance_ratio_r   = w->balance_ratio_r;
    c->balance_ratio_b   = w->balance_ratio_b;
    c->awb_color_temp    = w->awb_color_temp;
    c->awb_gain_min_x100 = w->awb_gain_min_x100;
    c->awb_gain_max_x10  = w->awb_gain_max_x10;
    c->awb_rate_x100     = w->awb_rate_x100;
    c->flip_flags        = (uint8_t)(w->flip_flags & CHC5_FLIP_FLAGS_MASK);
    c->gamma_x100        = w->gamma_x100;
    c->aux_seq           = w->aux_seq;
    c->proto_ver         = w->proto_ver;
    c->write_seq         = w->write_seq;
}

static const uint8_t chc5_crc8_tab[256] = {
    0x00, 0x1D, 0x3A, 0x27, 0x74, 0x69, 0x4E, 0x53, 0xE8, 0xF5, 0xD2, 0xCF, 0x9C, 0x81, 0xA6, 0xBB,
    0xCD, 0xD0, 0xF7, 0xEA, 0xB9, 0xA4, 0x83, 0x9E, 0x25, 0x38, 0x1F, 0x02, 0x51, 0x4C, 0x6B, 0x76,
    0x87, 0x9A, 0xBD, 0xA0, 0xF3, 0xEE, 0xC9, 0xD4, 0x6F, 0x72, 0x55, 0x48, 0x1B, 0x06, 0x21, 0x3C,
    0x4A, 0x57, 0x70, 0x6D, 0x3E, 0x23, 0x04, 0x19, 0xA2, 0xBF, 0x98, 0x85, 0xD6, 0xCB, 0xEC, 0xF1,
    0x13, 0x0E, 0x29, 0x34, 0x67, 0x7A, 0x5D, 0x40, 0xFB, 0xE6, 0xC1, 0xDC, 0x8F, 0x92, 0xB5, 0xA8,
    0xDE, 0xC3, 0xE4, 0xF9, 0xAA, 0xB7, 0x90, 0x8D, 0x36, 0x2B, 0x0C, 0x11, 0x42, 0x5F, 0x78, 0x65,
    0x94, 0x89, 0xAE, 0xB3, 0xE0, 0xFD, 0xDA, 0xC7, 0x7C, 0x61, 0x46, 0x5B, 0x08, 0x15, 0x32, 0x2F,
    0x59, 0x44, 0x63, 0x7E, 0x2D, 0x30, 0x17, 0x0A, 0xB1, 0xAC, 0x8B, 0x96, 0xC5, 0xD8, 0xFF, 0xE2,
    0x26, 0x3B, 0x1C, 0x01, 0x52, 0x4F, 0x68, 0x75, 0xCE, 0xD3, 0xF4, 0xE9, 0xBA, 0xA7, 0x80, 0x9D,
    0xEB, 0xF6, 0xD1, 0xCC, 0x9F, 0x82, 0xA5, 0xB8, 0x03, 0x1E, 0x39, 0x24, 0x77, 0x6A, 0x4D, 0x50,
    0xA1, 0xBC, 0x9B, 0x86, 0xD5, 0xC8, 0xEF, 0xF2, 0x49, 0x54, 0x73, 0x6E, 0x3D, 0x20, 0x07, 0x1A,
    0x6C, 0x71, 0x56, 0x4B, 0x18, 0x05, 0x22, 0x3F, 0x84, 0x99, 0xBE, 0xA3, 0xF0, 0xED, 0xCA, 0xD7,
    0x35, 0x28, 0x0F, 0x12, 0x41, 0x5C, 0x7B, 0x66, 0xDD, 0xC0, 0xE7, 0xFA, 0xA9, 0xB4, 0x93, 0x8E,
    0xF8, 0xE5, 0xC2, 0xDF, 0x8C, 0x91, 0xB6, 0xAB, 0x10, 0x0D, 0x2A, 0x37, 0x64, 0x79, 0x5E, 0x43,
    0xB2, 0xAF, 0x88, 0x95, 0xC6, 0xDB, 0xFC, 0xE1, 0x5A, 0x47, 0x60, 0x7D, 0x2E, 0x33, 0x14, 0x09,
    0x7F, 0x62, 0x45, 0x58, 0x0B, 0x16, 0x31, 0x2C, 0x97, 0x8A, 0xAD, 0xB0, 0xE3, 0xFE, 0xD9, 0xC4,
};

uint8_t chc5_crc8(const uint8_t *blk, uint8_t skip)
{
    uint8_t c = 0xFF;
    unsigned i;

    for (i = 0; i < 64; i++)
        if (i != skip)
            c = chc5_crc8_tab[c ^ blk[i]];
    return (uint8_t)(c ^ 0xFF);
}

static unsigned g_trig_pending;

static void usb_monitor_count_triggers(const chc5_camio_i2c_t *a)
{
    static uint8_t prev_sw;
    static bool    prev_sw_valid = false;

    if (prev_sw_valid && a->sw_trig_seq != prev_sw) {
        g_trig_pending += (uint8_t)(a->sw_trig_seq - prev_sw);
        if (g_trig_pending > 16)
            g_trig_pending = 16;
    }
    prev_sw       = a->sw_trig_seq;
    prev_sw_valid = true;
}

static void usb_monitor_fire_triggers(void)
{
    while (g_trig_pending) {
        g_trig_pending--;
        (void)camio_sync_fire(CHC5_TRIGEV_SOFTWARE);
    }
}

static int usb_monitor_apply_aux(int fd)
{
    chc5_camio_i2c_t a;
    struct camio_cfg c;

    if (pread(fd, &a, sizeof(a), CHC5_AUXCFG_OFFSET) != (ssize_t)sizeof(a))
        return -1;

    if (a.seq != a.seq_echo) {
        LOG_WARN("usb_monitor: aux torn read (seq %u != echo %u), retry",
                 a.seq, a.seq_echo);
        return -1;
    }

    {
        uint8_t want = chc5_crc8((const uint8_t *)&a, CHC5_AUX_CRC_OFF);
        if (a.crc8 != want) {
            LOG_WARN("usb_monitor: aux seq=%u CRC mismatch (0x%02X, want 0x%02X) -- refused",
                     a.seq, a.crc8, want);
            return 0;
        }
    }

    if (a.aux_ver != CHC5_I2C_AUX_VER) {
        LOG_WARN("usb_monitor: aux block ver %u != %u -- ignoring",
                 a.aux_ver, CHC5_I2C_AUX_VER);
        return 0;
    }

    if (a.link_mode == 1 || a.link_mode == 2)
        config_set_link_bw_mode(CFG_LINK_USB, a.link_mode == 2);
    if (a.link_limit_mbs != 0 && a.link_limit_mbs != 0xFFFF)
        config_set_link_bw_limit(CFG_LINK_USB, (uint32_t)a.link_limit_mbs * 1000000u);

    {
        static uint16_t prev_glo, prev_ghi;
        if (a.ae_gain_lower_db10 != prev_glo || a.ae_gain_upper_db10 != prev_ghi) {
            config_set_ae_gain_limits_db10(a.ae_gain_lower_db10,
                                           a.ae_gain_upper_db10 ? a.ae_gain_upper_db10
                                                                : CFG_AE_GAIN_UNSET);
            if (a.ae_gain_upper_db10)
                LOG_INFO("usb_monitor: AutoGain limits %.1f .. %.1f dB",
                         a.ae_gain_lower_db10 / 10.0, a.ae_gain_upper_db10 / 10.0);
            else
                LOG_INFO("usb_monitor: AutoGain limits %.1f dB .. sensor max",
                         a.ae_gain_lower_db10 / 10.0);
            prev_glo = a.ae_gain_lower_db10;
            prev_ghi = a.ae_gain_upper_db10;
        }
    }

    uint32_t hm = (a.flags & CHC5_AUX_F_IO_SET) ? a.host_mask : 0;

    {
        static chc5_camio_i2c_t prev;
        static bool prev_valid;
        if (prev_valid && (a.flags & CHC5_AUX_F_IO_SET)) {
#define AUX_CHG(cond, m) if (cond) hm |= (m);
            uint8_t fd = a.flags ^ prev.flags;
            AUX_CHG(fd & CHC5_AUX_F_IN_INVERT,                         CHC5_AUX_M_IN_INVERT)
            AUX_CHG(fd & CHC5_AUX_F_OUT_INVERT,                        CHC5_AUX_M_OUT_INVERT)
            AUX_CHG(fd & CHC5_AUX_F_OUT_USER,                          CHC5_AUX_M_OUT_USER)
            AUX_CHG(fd & CHC5_AUX_F_STROBE_EN,                         CHC5_AUX_M_STROBE_ENABLE)
            AUX_CHG(a.in_function        != prev.in_function,          CHC5_AUX_M_IN_FUNCTION)
            AUX_CHG(a.trig_src           != prev.trig_src,             CHC5_AUX_M_TRIG_SRC)
            AUX_CHG(a.in_activation      != prev.in_activation,        CHC5_AUX_M_IN_ACTIVATION)
            AUX_CHG(a.trigger_divider    != prev.trigger_divider,      CHC5_AUX_M_TRIG_DIVIDER)
            AUX_CHG(a.trigger_delay_us   != prev.trigger_delay_us,     CHC5_AUX_M_TRIG_DELAY)
            AUX_CHG((a.trig_flags ^ prev.trig_flags) & CHC5_AUX_TF_WIDTH_MODE, CHC5_AUX_M_WIDTH_MODE)
            AUX_CHG(a.in_filter_us       != prev.in_filter_us,         CHC5_AUX_M_IN_FILTER)
            AUX_CHG(a.out_source         != prev.out_source,           CHC5_AUX_M_OUT_SOURCE)
            AUX_CHG(a.strobe_src         != prev.strobe_src,           CHC5_AUX_M_STROBE_SRC)
            AUX_CHG(a.strobe_delay_us    != prev.strobe_delay_us,      CHC5_AUX_M_STROBE_DELAY)
            AUX_CHG(a.strobe_duration_us != prev.strobe_duration_us,   CHC5_AUX_M_STROBE_DUR)
            AUX_CHG(a.strobe_minon_us    != prev.strobe_minon_us,      CHC5_AUX_M_STROBE_MINON)
            AUX_CHG(a.sync_role          != prev.sync_role,            CHC5_AUX_M_SYNC_ROLE)
#undef AUX_CHG
        }
        prev = a;
        prev_valid = true;
    }
    if (!hm) {
        usb_monitor_count_triggers(&a);
        return 0;
    }

    memset(&c, 0, sizeof(c));
    c.in_invert        = (a.flags & CHC5_AUX_F_IN_INVERT)   ? 1 : 0;
    c.strobe_enable    = (a.flags & CHC5_AUX_F_STROBE_EN)   ? 1 : 0;
    c.strobe_invert    = (a.flags & CHC5_AUX_F_STROBE_INV)  ? 1 : 0;
    c.out_invert       = (a.flags & CHC5_AUX_F_OUT_INVERT)  ? 1 : 0;
    c.out_user_value   = (a.flags & CHC5_AUX_F_OUT_USER)    ? 1 : 0;
    c.in_function      = a.in_function;
    c.in_activation    = a.in_activation;
    c.trigger_divider  = a.trigger_divider;
    c.out_source       = a.out_source;
    c.strobe_src       = a.strobe_src;
    c.sync_role        = a.sync_role;
    c.trigger_delay_ns   = chc5camio_us_to_ns(a.trigger_delay_us);
    c.trig_src           = a.trig_src == 1 ? CHC5CAMIO_TRIG_SRC_SOFTWARE
                                           : CHC5CAMIO_TRIG_SRC_LINE0;
    c.width_mode         = (a.trig_flags & CHC5_AUX_TF_WIDTH_MODE) ? 1 : 0;
    c.in_filter_ns       = a.in_filter_us > 0xFFFFFFFFu / 1000u ? 0xFFFFFFFFu : a.in_filter_us * 1000u;
    c.strobe_delay_us    = a.strobe_delay_us;
    c.strobe_duration_us = a.strobe_duration_us;
    c.strobe_minon_us    = a.strobe_minon_us;

    uint32_t mask = 0;
#define AUX_M(m, f) if (hm & (m)) mask |= (f);
    AUX_M(CHC5_AUX_M_IN_FUNCTION,   CAMIO_F(IN_FUNCTION))
    AUX_M(CHC5_AUX_M_TRIG_SRC,      CAMIO_F(TRIG_SRC))
    AUX_M(CHC5_AUX_M_IN_ACTIVATION, CAMIO_F(IN_ACTIVATION))
    AUX_M(CHC5_AUX_M_IN_INVERT,     CAMIO_F(IN_INVERT))
    AUX_M(CHC5_AUX_M_TRIG_DIVIDER,  CAMIO_F(TRIG_DIVIDER))
    AUX_M(CHC5_AUX_M_TRIG_DELAY,    CAMIO_F(TRIG_DELAY))
    AUX_M(CHC5_AUX_M_WIDTH_MODE,    CAMIO_F(WIDTH_MODE))
    AUX_M(CHC5_AUX_M_IN_FILTER,     CAMIO_F(IN_FILTER))
    AUX_M(CHC5_AUX_M_OUT_SOURCE,    CAMIO_F(OUT_SOURCE))
    AUX_M(CHC5_AUX_M_OUT_INVERT,    CAMIO_F(OUT_INVERT))
    AUX_M(CHC5_AUX_M_OUT_USER,      CAMIO_F(OUT_USER))
    AUX_M(CHC5_AUX_M_STROBE_ENABLE, CAMIO_F(STROBE_ENABLE))
    AUX_M(CHC5_AUX_M_STROBE_SRC,    CAMIO_F(STROBE_SRC))
    AUX_M(CHC5_AUX_M_STROBE_DELAY,  CAMIO_F(STROBE_DELAY))
    AUX_M(CHC5_AUX_M_STROBE_DUR,    CAMIO_F(STROBE_DUR))
    AUX_M(CHC5_AUX_M_STROBE_MINON,  CAMIO_F(STROBE_MINON))
    AUX_M(CHC5_AUX_M_SYNC_ROLE,     CAMIO_F(SYNC_ROLE))
#undef AUX_M

    LOG_INFO("usb_monitor: aux I/O fields 0x%07x (in=%u src=%u) seq=%u",
             mask, c.in_function, c.trig_src, a.seq);
    config_update_camio(&c, mask);
    usb_monitor_count_triggers(&a);
    return 0;
}

static void *usb_monitor_thread(void *arg)
{
    (void)arg;

    uint8_t prev_seq     = 0xFF;
    bool     first_pass  = true;
    bool     prev_cnt_valid = false;
    uint16_t prev_cnt[2 * sizeof(chc5_i2c_cfg_t)];
    uint8_t  file_buf[CHC5_SLAVE_FILE_SIZE];
    uint8_t prev_aux_seq = 0xFF;
    uint8_t warned_proto = CHC5_I2C_PROTO_VER;

    int fd = open(USB_CHARDEV_PATH, O_RDONLY);
    if (fd < 0) {
        LOG_ERR("usb_monitor: cannot open %s: %s",
                USB_CHARDEV_PATH, strerror(errno));
        return NULL;
    }

    {
        uint16_t probe;
        if (pread(fd, &probe, sizeof(probe), CHC5_SLAVE_CNT_OFFSET)
                != (ssize_t)sizeof(probe)) {
            LOG_ERR("usb_monitor: %s has no write counters (kernel i2c-slave-chc5 "
                    "too old) -- USB control disabled", USB_CHARDEV_PATH);
            close(fd);
            return NULL;
        }
    }
    LOG_INFO("usb_monitor: started on %s (write-counters + POLLPRI notify)",
             USB_CHARDEV_PATH);

    while (g_running) {
        chc5_i2c_cfg_t  w;
        imgsensor_cfg_t cur;
        ssize_t rd;

        usb_wait_next(fd, &first_pass);

        rd = pread(fd, file_buf, sizeof(file_buf), 0);
        if (rd != (ssize_t)sizeof(file_buf))
            continue;
        memcpy(&w, file_buf + CHC5_DYNCFG_OFFSET, sizeof(w));

        if (w.write_seq == 0xFF && w.width == 0xFFFF &&
            w.height == 0xFFFF) {
            ;
            continue;
        }

        if (w.write_seq == 0 && w.width == 0 && w.height == 0) {
            ;
            continue;
        }

        if (w.proto_ver == CHC5_I2C_PROTO_VER &&
            w.aux_seq != 0x00 && w.aux_seq != 0xFF &&
            w.aux_seq != prev_aux_seq) {
            if (usb_monitor_apply_aux(fd) == 0)
                prev_aux_seq = w.aux_seq;
        }

        if (w.write_seq == prev_seq) {
            usb_monitor_fire_triggers();
            ;
            continue;
        }
        prev_seq = w.write_seq;

        if (w.proto_ver != CHC5_I2C_PROTO_VER) {
            if (warned_proto != w.proto_ver) {
                LOG_WARN("usb_monitor: FX3 I2C proto v%u != required v%u -- "
                         "ignoring config (flash FX3 + PS in lockstep)",
                         w.proto_ver, CHC5_I2C_PROTO_VER);
                warned_proto = w.proto_ver;
            }
            ;
            continue;
        }
        {
            static unsigned crc_refused;
            uint8_t want = chc5_crc8((const uint8_t *)&w, CHC5_CFG_CRC_OFF);
            if (w.crc8 != want) {
                crc_refused++;
                LOG_WARN("usb_monitor: config seq=%u CRC mismatch (0x%02X, want 0x%02X) "
                         "-- refused, %u so far", w.write_seq, w.crc8, want, crc_refused);
                continue;
            }
        }
        usb_cfg_from_wire(&w, &cur);

        if (cur.black_level == 0xFFFF)
            cur.black_level = 0;
        if (cur.black_level > 4095)
            cur.black_level = 4095;
        if (cur.auto_flags == 0xFF)
            cur.auto_flags = 0;

        if (cur.width == 0 || cur.width > 8192 ||
            cur.height == 0 || cur.height > 8192 ||
            !pixfmt_is_valid(cur.pixel_format) ||
            (cur.stream_enable & ~0x13) != 0 ||
            (cur.binning >> 4) == 3 || (cur.binning & 0x0F) == 3) {
            LOG_WARN("usb_monitor: ignoring invalid config: "
                     "w=%u h=%u pfnc=0x%08X stream=0x%02X bin=0x%02X seq=%u",
                     cur.width, cur.height, cur.pixel_format,
                     cur.stream_enable, cur.binning, cur.write_seq);
            ;
            continue;
        }

        LOG_INFO("usb_monitor: new config from CYUSB3014: "
                 "%ux%u+%u+%u pfnc=0x%08X %.3ffps exp=%uus gain=%u bin=0x%02X stream=0x%02X seq=%u",
                 cur.width, cur.height, cur.offset_x, cur.offset_y,
                 cur.pixel_format, cur.fps_mhz / 1000.0,
                 cur.exposure_us, cur.analog_gain, cur.binning,
                 cur.stream_enable, cur.write_seq);

        bool host_streaming = (cur.stream_enable & 0x10) != 0;
        uint8_t stream_cmd  = cur.stream_enable & 0x0F;
        uint8_t auto_flags  = cur.auto_flags;
        uint8_t flip_flags  = cur.flip_flags;
        cur.stream_enable = 0;
        cur.auto_flags    = 0;
        cur.flip_flags    = 0;
        cur.aux_seq       = 0;

        bool gate_live = false;
        uint16_t cnt[2 * sizeof(chc5_i2c_cfg_t)];
        memcpy(cnt, file_buf + CHC5_SLAVE_CNT_OFFSET, sizeof(cnt));
        if (prev_cnt_valid) {
            bool regressed = false;
            size_t i;
            for (i = 0; i < sizeof(chc5_i2c_cfg_t); i++)
                if (cnt[i] < prev_cnt[i]) { regressed = true; break; }
            if (!regressed) {
                gate_live = true;
                imgsensor_cfg_t live;
                config_get_sensor(&live);
                if (!cfg_field_written(cnt, prev_cnt,
                        offsetof(chc5_i2c_cfg_t, exposure_us), 4))
                    cur.exposure_us = live.exposure_us;
                if (!cfg_field_written(cnt, prev_cnt,
                        offsetof(chc5_i2c_cfg_t, analog_gain), 2))
                    cur.analog_gain = live.analog_gain;
                if (!cfg_field_written(cnt, prev_cnt,
                        offsetof(chc5_i2c_cfg_t, black_level), 2))
                    cur.black_level = live.black_level;
                {
                    size_t   ws   = offsetof(chc5_i2c_cfg_t, write_seq);
                    uint16_t dseq = (uint16_t)(cnt[ws] - prev_cnt[ws]);
                    if (dseq > 1)
                        LOG_INFO("usb_monitor: %u FX3 sessions coalesced "
                                 "into this block (latest state applied)",
                                 dseq);
                }
            } else {
                LOG_WARN("usb_monitor: write counters regressed -- "
                         "slave driver re-probed; applying full block");
                prev_cnt_valid = false;
            }
        }
        {
            static bool tamper_warned = false;
            size_t i;
            for (i = sizeof(chc5_i2c_cfg_t);
                 i < 2 * sizeof(chc5_i2c_cfg_t) && !tamper_warned; i++)
                if (cnt[i]) {
                    LOG_WARN("usb_monitor: FX3 WROTE the PS-owned readback "
                             "region (byte 0x%zx, count %u) -- firmware fault",
                             i, cnt[i]);
                    tamper_warned = true;
                }
        }

        if (stream_cmd == 1) {
            imgsensor_cfg_t live;
            config_get_sensor(&live);
            uint8_t sfx = FLIP_FLAG_X(flip_flags), sfy = FLIP_FLAG_Y(flip_flags);
            bool srx = config_get_reverse_x(), sry = config_get_reverse_y();
            bool flip_change = (sfx == AUTO_ENABLE && !srx) || (sfx == AUTO_DISABLE && srx) ||
                               (sfy == AUTO_ENABLE && !sry) || (sfy == AUTO_DISABLE && sry);
            if (live.stream_enable &&
                (live.width != cur.width || live.height != cur.height ||
                 live.offset_x != cur.offset_x || live.offset_y != cur.offset_y ||
                 live.binning != cur.binning ||
                 live.pixel_format != cur.pixel_format || flip_change)) {
                LOG_WARN("usb_monitor: START with a new image format %ux%u "
                         "pfnc=0x%08X%s while a %ux%u stream is running (the "
                         "HDMI preview being handed over, or a host session "
                         "whose STOP was lost) -- stopping it first",
                         cur.width, cur.height, cur.pixel_format,
                         flip_change ? " (new flip)" : "",
                         live.width, live.height);
                config_set_stream(false);
            }
        }

        config_set_sensor(&cur, CFG_SRC_USB);

        if (pixfmt_is_valid(cur.pixel_format))
            config_set_pixfmt_usb(cur.pixel_format);

        if (flip_flags != 0) {
            uint8_t fx = FLIP_FLAG_X(flip_flags);
            uint8_t fy = FLIP_FLAG_Y(flip_flags);
            bool    rx0 = config_get_reverse_x(), ry0 = config_get_reverse_y();

            if (fx == AUTO_ENABLE)       config_set_reverse_x(true);
            else if (fx == AUTO_DISABLE) config_set_reverse_x(false);

            if (fy == AUTO_ENABLE)       config_set_reverse_y(true);
            else if (fy == AUTO_DISABLE) config_set_reverse_y(false);

            if ((rx0 != config_get_reverse_x() || ry0 != config_get_reverse_y()) &&
                !config_get_stream_active())
                media_pipeline_refresh_cfa_order();
        }

        if (stream_cmd == 1) {
            LOG_INFO("usb_monitor: stream START command");
            config_set_stream(true);
        } else if (stream_cmd == 2) {
            LOG_INFO("usb_monitor: stream STOP command");
            config_set_stream(false);
        }
        else if (stream_cmd == 3) {
        }
        else {
            imgsensor_cfg_t live;
            config_get_sensor(&live);
            bool local_intent = (live.stream_enable != 0);

            if (host_streaming && !local_intent) {
                LOG_WARN("usb_monitor: missed START -- host=streaming, local=stopped");
                config_set_stream(true);
            } else if (!host_streaming && local_intent) {
                LOG_WARN("usb_monitor: missed STOP -- host=stopped, local=streaming");
                config_set_stream(false);
            }
        }

        usb_monitor_fire_triggers();

        bool was_ae = config_get_ae_enabled(), was_ag = config_get_auto_gain();
        if (auto_flags != 0) {
            uint8_t ae  = AUTO_FLAG_EXPOSURE(auto_flags);
            uint8_t ag  = AUTO_FLAG_GAIN(auto_flags);
            uint8_t awb = AUTO_FLAG_WB(auto_flags);

            if (ae == AUTO_ENABLE)       config_set_auto_exposure(true);
            else if (ae == AUTO_DISABLE) config_set_auto_exposure(false);
            else if (ae == AUTO_ONCE)  { config_set_auto_exposure(true);
                                         config_request_ae_once_exposure(); }

            if (ag == AUTO_ENABLE)       config_set_auto_gain(true);
            else if (ag == AUTO_DISABLE) config_set_auto_gain(false);
            else if (ag == AUTO_ONCE)  { config_set_auto_gain(true);
                                         config_request_ae_once_gain(); }

            if (awb == AUTO_ENABLE)       config_set_auto_wb(true);
            else if (awb == AUTO_DISABLE) config_set_auto_wb(false);
            else if (awb == AUTO_ONCE)  { config_set_auto_wb(true);
                                          config_request_awb_once(); }
        }

        {
            static bool     prev_valid = false;
            static uint32_t prev_exp;
            static uint16_t prev_gain;
            bool exp_wr  = gate_live
                ? cfg_field_written(cnt, prev_cnt, offsetof(chc5_i2c_cfg_t, exposure_us),
                                    sizeof(w.exposure_us))
                : (!prev_valid || w.exposure_us != prev_exp);
            bool gain_wr = gate_live
                ? cfg_field_written(cnt, prev_cnt, offsetof(chc5_i2c_cfg_t, analog_gain),
                                    sizeof(w.analog_gain))
                : (!prev_valid || w.analog_gain != prev_gain);

            if (was_ae && !config_get_ae_enabled() && exp_wr &&
                w.exposure_us != 0 && w.exposure_us != 0xFFFFFFFFu) {
                config_set_exposure_us(w.exposure_us);
                LOG_INFO("usb_monitor: AE off with exposure %uus in the same block -- "
                         "the host's value, not the held one", w.exposure_us);
            }
            if (was_ag && !config_get_auto_gain() && gain_wr &&
                w.analog_gain != 0xFFFF) {
                config_set_gain_raw(w.analog_gain);
                LOG_INFO("usb_monitor: auto gain off with gain %u (0.1 dB) in the same "
                         "block -- the host's value, not the held one", w.analog_gain);
            }
            prev_exp   = w.exposure_us;
            prev_gain  = w.analog_gain;
            prev_valid = true;
        }

        if (cur.proto_ver >= 3)
        {
            static uint16_t prev_br = 0, prev_bb = 0;
            static uint8_t  prev_ct = 0, prev_gmin = 0, prev_gmax = 0, prev_rate = 0;
#define USB_WROTE(field) (gate_live && cfg_field_written(cnt, prev_cnt, \
                offsetof(chc5_i2c_cfg_t, field), sizeof(w.field)))
            if (cur.balance_ratio_r &&
                (USB_WROTE(balance_ratio_r) || cur.balance_ratio_r != prev_br))
                config_set_wb_ratio(0, (float)cur.balance_ratio_r / 256.0f);
            if (cur.balance_ratio_b &&
                (USB_WROTE(balance_ratio_b) || cur.balance_ratio_b != prev_bb))
                config_set_wb_ratio(2, (float)cur.balance_ratio_b / 256.0f);
            if (cur.awb_color_temp &&
                (USB_WROTE(awb_color_temp) || cur.awb_color_temp != prev_ct))
                config_set_color_temp(cur.awb_color_temp == 1 ? 0
                                      : (uint16_t)cur.awb_color_temp * 100u);
            if (cur.awb_gain_min_x100 &&
                (USB_WROTE(awb_gain_min_x100) || cur.awb_gain_min_x100 != prev_gmin))
                config_set_awb_gain_min((float)cur.awb_gain_min_x100 / 100.0f);
            if (cur.awb_gain_max_x10 &&
                (USB_WROTE(awb_gain_max_x10) || cur.awb_gain_max_x10 != prev_gmax))
                config_set_awb_gain_max((float)cur.awb_gain_max_x10 / 10.0f);
            if (cur.awb_rate_x100 &&
                (USB_WROTE(awb_rate_x100) || cur.awb_rate_x100 != prev_rate))
                config_set_awb_damp((float)cur.awb_rate_x100 / 100.0f);
            prev_br = cur.balance_ratio_r;   prev_bb = cur.balance_ratio_b;
            prev_ct = cur.awb_color_temp;    prev_gmin = cur.awb_gain_min_x100;
            prev_gmax = cur.awb_gain_max_x10; prev_rate = cur.awb_rate_x100;
        }

        if (cur.ae_metering_mode <= 5 &&
            (!gate_live || USB_WROTE(ae_metering_mode)))
            config_set_ae_metering_mode(cur.ae_metering_mode);
        if (cur.ae_target != 0 &&
            (!gate_live || USB_WROTE(ae_target)))
            config_set_ae_target((float)cur.ae_target / 255.0f);
        if (gate_live ? (USB_WROTE(ae_exp_lower_us) && cur.ae_exp_lower_us != 0xFFFFFFFFu)
                      : (cur.ae_exp_lower_us != 0 && cur.ae_exp_lower_us != 0xFFFFFFFFu))
            config_set_ae_exp_lower_us(cur.ae_exp_lower_us);
        if (gate_live ? (USB_WROTE(ae_exp_upper_us) && cur.ae_exp_upper_us != 0xFFFFFFFFu)
                      : (cur.ae_exp_upper_us != 0 && cur.ae_exp_upper_us != 0xFFFFFFFFu))
            config_set_ae_exp_upper_us(cur.ae_exp_upper_us);
        if (cur.ae_roi_w != 0 && cur.ae_roi_h != 0 &&
            cur.ae_roi_w != 0xFFFF && cur.ae_roi_h != 0xFFFF &&
            (!gate_live || USB_WROTE(ae_roi_x) || USB_WROTE(ae_roi_y) ||
             USB_WROTE(ae_roi_w) || USB_WROTE(ae_roi_h)))
            config_set_ae_roi(cur.ae_roi_x, cur.ae_roi_y,
                              cur.ae_roi_w, cur.ae_roi_h);
        {
            static uint16_t prev_gamma = CHC5_GAMMA_NO_CHANGE;
            if (cur.gamma_x100 != CHC5_GAMMA_NO_CHANGE &&
                (USB_WROTE(gamma_x100) || cur.gamma_x100 != prev_gamma ||
                 stream_cmd == 1))
                config_set_gamma_x100(cur.gamma_x100);
            prev_gamma = cur.gamma_x100;
        }
#undef USB_WROTE

        memcpy(prev_cnt, cnt, sizeof(prev_cnt));
        prev_cnt_valid = true;

        config_usb_block_seen(cur.write_seq);

        ;
    }

    close(fd);
    LOG_INFO("usb_monitor: stopped");
    return NULL;
}

int usb_monitor_start(void)
{
    g_running = 1;
    if (pthread_create(&g_thread, NULL, usb_monitor_thread, NULL) != 0) {
        LOG_ERR("usb_monitor: pthread_create failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

void usb_monitor_stop(void)
{
    g_running = 0;
    pthread_join(g_thread, NULL);
}
