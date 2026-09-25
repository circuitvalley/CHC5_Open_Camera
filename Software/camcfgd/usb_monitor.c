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

#define USB_POLL_INTERVAL_MS   60
#define USB_NOTIFY_TIMEOUT_MS  1000
#define USB_COALESCE_MS        50

static void usb_wait_next(int fd, bool have_cnt, bool *first)
{
    if (*first) { *first = false; return; }

    usleep(USB_COALESCE_MS * 1000);

    if (have_cnt) {
        struct pollfd pw = { .fd = fd, .events = POLLPRI | POLLERR };
        (void)poll(&pw, 1, USB_NOTIFY_TIMEOUT_MS);
    } else {
        usleep(USB_POLL_INTERVAL_MS * 1000);
    }
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

    if (a.aux_ver != CHC5_I2C_AUX_VER) {
        LOG_WARN("usb_monitor: aux block ver %u != %u -- ignoring",
                 a.aux_ver, CHC5_I2C_AUX_VER);
        return 0;
    }

    memset(&c, 0, sizeof(c));
    c.present          = 1;
    c.enable           = (a.flags & CHC5_AUX_F_ENABLE)      ? 1 : 0;
    c.in_invert        = (a.flags & CHC5_AUX_F_IN_INVERT)   ? 1 : 0;
    c.strobe_enable    = (a.flags & CHC5_AUX_F_STROBE_EN)   ? 1 : 0;
    c.strobe_invert    = (a.flags & CHC5_AUX_F_STROBE_INV)  ? 1 : 0;
    c.out_invert       = (a.flags & CHC5_AUX_F_OUT_INVERT)  ? 1 : 0;
    c.out_user_value   = (a.flags & CHC5_AUX_F_OUT_USER)    ? 1 : 0;
    c.sync_role_by     = (a.flags & CHC5_AUX_F_ROLE_BY_I2C) ? 1 : 0;
    c.in_function      = a.in_function;
    c.in_activation    = a.in_activation;
    c.trigger_divider  = a.trigger_divider;
    c.out_source       = a.out_source;
    c.strobe_src       = a.strobe_src;
    c.sync_role        = a.sync_role;
    c.sync_trig_route  = a.sync_trig_route;
    c.trigger_delay_us   = a.trigger_delay_us;
    c.strobe_delay_us    = a.strobe_delay_us;
    c.strobe_duration_us = a.strobe_duration_us;
    c.strobe_minon_us    = a.strobe_minon_us;

    if (c.sync_role > CHC5CAMIO_ROLE_SLAVE ||
        (c.out_source == CHC5CAMIO_OUT_PTP_PULSE && camio_sync_has_ptp_pulse() != 1) ||
        c.out_source > CHC5CAMIO_OUT_PTP_PULSE) {
        struct camio_cfg cur;

        config_get_camio(&cur);
        if (c.sync_role > CHC5CAMIO_ROLE_SLAVE) {
            LOG_WARN("usb_monitor: SyncRole %u not supported -- kept %u",
                     c.sync_role, cur.sync_role);
            c.sync_role = cur.sync_role;
        }
        if (c.out_source > CHC5CAMIO_OUT_PASSTHROUGH &&
            (c.out_source != CHC5CAMIO_OUT_PTP_PULSE || camio_sync_has_ptp_pulse() != 1)) {
            LOG_WARN("usb_monitor: LineSource %u not supported -- kept %u",
                     c.out_source, cur.out_source);
            c.out_source = cur.out_source;
        }
    }

    LOG_INFO("usb_monitor: aux I/O update in=%u strobe=%u role=%u seq=%u",
             c.in_function, c.strobe_enable, c.sync_role, a.seq);
    config_set_camio(&c);
    return 0;
}

static void *usb_monitor_thread(void *arg)
{
    (void)arg;

    uint8_t prev_seq     = 0xFF;
    bool     first_pass  = true;
    bool     prev_cnt_valid = false;
    uint16_t prev_cnt[2 * sizeof(imgsensor_cfg_t)];
    uint8_t  file_buf[CHC5_SLAVE_FILE_SIZE];
    uint8_t prev_aux_seq = 0xFF;
    uint8_t warned_proto = CHC5_I2C_PROTO_VER;

    int fd = open(USB_CHARDEV_PATH, O_RDONLY);
    if (fd < 0) {
        LOG_ERR("usb_monitor: cannot open %s: %s",
                USB_CHARDEV_PATH, strerror(errno));
        return NULL;
    }

    bool have_cnt;
    {
        uint16_t probe;
        have_cnt = (pread(fd, &probe, sizeof(probe), CHC5_SLAVE_CNT_OFFSET)
                    == (ssize_t)sizeof(probe));
    }
    LOG_INFO("usb_monitor: started on %s (%s)", USB_CHARDEV_PATH,
             have_cnt ? "write-counters + POLLPRI notify"
                      : "legacy kernel: 60 ms polling, no counters");

    while (g_running) {
        imgsensor_cfg_t cur;
        ssize_t rd;
        bool    cnt_ok;

        usb_wait_next(fd, have_cnt, &first_pass);

        rd = pread(fd, file_buf,
                   have_cnt ? sizeof(file_buf) : sizeof(imgsensor_cfg_t), 0);
        if (rd < (ssize_t)sizeof(imgsensor_cfg_t))
            continue;
        cnt_ok = have_cnt && rd == (ssize_t)sizeof(file_buf);
        memcpy(&cur, file_buf + CHC5_DYNCFG_OFFSET, sizeof(cur));

        if (cur.write_seq == 0xFF && cur.width == 0xFFFF &&
            cur.height == 0xFFFF) {
            ;
            continue;
        }

        if (cur.write_seq == 0 && cur.width == 0 && cur.height == 0) {
            ;
            continue;
        }

        if (cur.proto_ver == CHC5_I2C_PROTO_VER &&
            cur.aux_seq != 0x00 && cur.aux_seq != 0xFF &&
            cur.aux_seq != prev_aux_seq) {
            if (usb_monitor_apply_aux(fd) == 0)
                prev_aux_seq = cur.aux_seq;
        }

        if (cur.write_seq == prev_seq) {
            ;
            continue;
        }
        prev_seq = cur.write_seq;

        if (cur.proto_ver != CHC5_I2C_PROTO_VER) {
            if (warned_proto != cur.proto_ver) {
                LOG_WARN("usb_monitor: FX3 I2C proto v%u != required v%u -- "
                         "ignoring config (flash FX3 + PS in lockstep)",
                         cur.proto_ver, CHC5_I2C_PROTO_VER);
                warned_proto = cur.proto_ver;
            }
            ;
            continue;
        }

        if (cur.black_level == 0xFFFF)
            cur.black_level = 0;
        if (cur.black_level > 4095)
            cur.black_level = 4095;
        if (cur.binning == 0xFF)
            cur.binning = 0;
        if (cur.auto_flags == 0xFF)
            cur.auto_flags = 0;
        if (!(cur.proto_flags & CHC5_PF_GAMMA))
            cur.gamma_x100 = CHC5_GAMMA_NO_CHANGE;

        if (cur.width == 0 || cur.width > 8192 ||
            cur.height == 0 || cur.height > 8192 ||
            !pixfmt_is_valid(cur.pixel_format)) {
            LOG_WARN("usb_monitor: ignoring invalid config: "
                     "w=%u h=%u pfnc=0x%08X seq=%u",
                     cur.width, cur.height, cur.pixel_format, cur.write_seq);
            ;
            continue;
        }

        LOG_INFO("usb_monitor: new config from CYUSB3014: "
                 "%ux%u+%u+%u pfnc=0x%08X %ufps exp=%uus gain=%u bin=0x%02X stream=0x%02X seq=%u",
                 cur.width, cur.height, cur.offset_x, cur.offset_y,
                 cur.pixel_format, cur.fps,
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
        uint16_t cnt[2 * sizeof(imgsensor_cfg_t)];
        if (cnt_ok) {
            memcpy(cnt, file_buf + CHC5_SLAVE_CNT_OFFSET, sizeof(cnt));
            if (prev_cnt_valid) {
                bool regressed = false;
                size_t i;
                for (i = 0; i < sizeof(imgsensor_cfg_t); i++)
                    if (cnt[i] < prev_cnt[i]) { regressed = true; break; }
                if (!regressed) {
                    gate_live = true;
                    imgsensor_cfg_t live;
                    config_get_sensor(&live);
                    if (!cfg_field_written(cnt, prev_cnt,
                            offsetof(imgsensor_cfg_t, exposure_us), 4))
                        cur.exposure_us = live.exposure_us;
                    if (!cfg_field_written(cnt, prev_cnt,
                            offsetof(imgsensor_cfg_t, analog_gain), 2))
                        cur.analog_gain = live.analog_gain;
                    if (!cfg_field_written(cnt, prev_cnt,
                            offsetof(imgsensor_cfg_t, black_level), 2))
                        cur.black_level = live.black_level;
                    {
                        uint16_t dseq = (uint16_t)(cnt[0x3F] - prev_cnt[0x3F]);
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
                for (i = sizeof(imgsensor_cfg_t);
                     i < 2 * sizeof(imgsensor_cfg_t) && !tamper_warned; i++)
                    if (cnt[i]) {
                        LOG_WARN("usb_monitor: FX3 WROTE the PS-owned readback "
                                 "region (byte 0x%zx, count %u) -- firmware fault",
                                 i, cnt[i]);
                        tamper_warned = true;
                    }
            }
        }

        if (stream_cmd == 1) {
            imgsensor_cfg_t live;
            config_get_sensor(&live);
            if (live.stream_enable &&
                (live.width != cur.width || live.height != cur.height ||
                 live.offset_x != cur.offset_x || live.offset_y != cur.offset_y ||
                 live.binning != cur.binning ||
                 live.pixel_format != cur.pixel_format)) {
                LOG_WARN("usb_monitor: START with a new image format %ux%u "
                         "pfnc=0x%08X while a %ux%u stream is running (the "
                         "HDMI preview being handed over, or a host session "
                         "whose STOP was lost) -- stopping it first",
                         cur.width, cur.height, cur.pixel_format,
                         live.width, live.height);
                config_set_stream(false);
            }
        }

        config_set_sensor(&cur, CFG_SRC_USB);

        if (pixfmt_is_valid(cur.pixel_format))
            config_set_pixfmt_usb(cur.pixel_format);

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

        if (cur.proto_ver >= 3)
        {
            static uint16_t prev_br = 0, prev_bb = 0;
            static uint8_t  prev_ct = 0, prev_gmin = 0, prev_gmax = 0, prev_rate = 0;
#define USB_WROTE(field) (gate_live && cfg_field_written(cnt, prev_cnt, \
                offsetof(imgsensor_cfg_t, field), sizeof(cur.field)))
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
        if ((cur.link_bw_mode == 1 || cur.link_bw_mode == 2) &&
            (!gate_live || USB_WROTE(link_bw_mode)))
            config_set_link_bw_mode(CFG_LINK_USB, cur.link_bw_mode == 2);
        if (cur.link_bw_limit != 0 && cur.link_bw_limit != 0xFFFFFFFFu &&
            (!gate_live || USB_WROTE(link_bw_limit)))
            config_set_link_bw_limit(CFG_LINK_USB, cur.link_bw_limit);
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

        if (cnt_ok) {
            memcpy(prev_cnt, cnt, sizeof(prev_cnt));
            prev_cnt_valid = true;
        }

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
