// SPDX-License-Identifier: CC-BY-NC-ND-4.0
/*
 * main.c - camera configuration daemon
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>

#include "common.h"
#include "config.h"
#include "camio_sync.h"
#include "usb_monitor.h"
#include "ipc_server.h"
#include "media_pipeline.h"
#include "imaging_pub.h"
#include "camio_pub.h"
#include "version.h"

#ifdef HAVE_SYSTEMD
#  include <systemd/sd-daemon.h>
#else
static inline int sd_notify(int unset_environment, const char *state)
{
    (void)unset_environment;
    (void)state;
    return 0;
}
#endif

static volatile int g_running = 1;

static void sighandler(int sig)
{
    (void)sig;
    g_running = 0;
    config_signal_shutdown();
}

static int64_t sync_fps_override(void)
{
    uint32_t pmhz = camio_sync_follow_rate_mhz();

    if (pmhz)
        return pmhz;
    return camio_sync_source() == CHC5_SYNC_PTP ? 0 : -1;
}

static int camio_facts_refresh(void)
{
    struct camio_facts f;
    float max_fps = media_pipeline_get_max_fps();

    memset(&f, 0, sizeof(f));
    f.can_trigger  = media_pipeline_can_trigger();
    f.can_follow   = media_pipeline_can_follow();
    f.follow_xhs   = media_pipeline_follower_xhs();
    (void)media_pipeline_line_timing(&f.line_ps, &f.frame_lines);
    f.min_frame_ns = max_fps > 0.0f ? (uint32_t)(1e9f / max_fps) : 0;
    config_get_camio_facts(&f);
    return camio_sync_set_sensor(&f);
}

static void apply_loop(void)
{
    LOG_INFO("apply_loop: started");
    uint32_t applied_start_gen = 0;

    while (g_running) {
        config_set_max_fps(media_pipeline_get_max_fps());
        config_set_actual_fps(media_pipeline_get_actual_fps());
        config_load_sensor_caps();
        {
            int32_t ccm[12];
            bool    ccm_en = false;
            if (config_take_ccm_update(ccm, &ccm_en))
                media_pipeline_set_ccm(ccm_en ? ccm : NULL, ccm_en);
        }
        media_pipeline_ccm_tune_tick();
        media_pipeline_apply_manual_wb();

        {
            uint16_t fps = 25, cx = 0, cy = 0, aep = 500, mm = 1, hs = 0, af = 0,
                     he = 0, spd = 40, prio = 0, hl = 1, fk = 0, once = 0, awbf = 0,
                     awbmn = 0, awbmx = 0, awbrt = 0, awbct = 0, ca = 1;
            uint32_t exp_us = 0;
            if (imaging_pub_load(&fps, &cx, &cy, &aep, &mm, &hs, &af, &he,
                                 &spd, &prio, &hl, &fk, &once, &exp_us, &awbf,
                                 &awbmn, &awbmx, &awbrt, &awbct, &ca) == 0)
                config_apply_imaging(fps, cx, cy, (float)aep / 1000.0f,
                                     (uint8_t)mm, (uint8_t)hs, (uint8_t)af,
                                     (uint8_t)he, (uint8_t)spd, (uint8_t)prio,
                                     (uint8_t)hl, (uint8_t)fk, (uint8_t)once, exp_us,
                                     (uint8_t)awbf, (uint8_t)awbmn,
                                     (uint8_t)awbmx, (uint8_t)awbrt, (uint8_t)awbct,
                                     (uint8_t)ca);
        }

        {
            struct camio_cfg wcc;
            uint32_t mask;
            if (camio_pub_poll(&wcc, &mask)) {
                LOG_INFO("camio: web/backup config changed (fields 0x%07x)", mask);
                config_update_camio(&wcc, mask);
            }
        }

        config_hdmi_standalone_tick();

        struct camcfg_snapshot_s snap;
        uint32_t flags = config_wait_dirty_snapshot(&snap);
        if (!g_running)
            break;

        sd_notify(0, "WATCHDOG=1");

        config_publish_exposure();
        config_set_pipeline_ready(access("/dev/media0", F_OK) == 0);
        config_publish_camio();

        if (flags == 0)
            continue;

        uint8_t usb_seq = snap.usb_seq;
        bool usb_seq_ok = snap.usb_seq_ok;

        struct camcfg_state_s state = snap.state;
        if (state.ae_exposure_auto) state.sensor_cfg.exposure_us = state.ae_current_exposure_us;
        if (state.ae_gain_auto)     state.sensor_cfg.analog_gain = state.ae_current_gain;
        {
            int64_t ov = sync_fps_override();
            if (ov >= 0)
                state.sensor_cfg.fps_mhz = (uint32_t)ov;
        }

        if (flags & DIRTY_PIPELINE_FMT) {
            bool was_streaming = config_get_stream_active();
            if (was_streaming) {
                LOG_INFO("apply: stopping stream for pipeline reconfig");
                media_pipeline_stream_off();
                config_set_stream_active(false);
            }

            LOG_INFO("apply: pipeline format %ux%u pfnc=0x%08X",
                     state.sensor_cfg.width,
                     state.sensor_cfg.height,
                     state.sensor_cfg.pixel_format);
            media_pipeline_set_format(&state.sensor_cfg);

            LOG_INFO("apply: re-apply sensor controls for new mode");
            media_pipeline_set_sensor_controls(&state.sensor_cfg);

            config_set_max_fps(media_pipeline_get_max_fps());
            config_set_actual_fps(media_pipeline_get_actual_fps());

            if (was_streaming) {
                if (flags & DIRTY_STREAM) {
                    LOG_DBG("apply: stream %s left to the stream branch",
                            state.sensor_cfg.stream_enable ? "start" : "stop");
                } else {
                    LOG_INFO("apply: restarting stream after reconfig");
                    if (media_pipeline_stream_on() == 0)
                        config_set_stream_active(true);
                }
            }
        }

        if (flags & DIRTY_SENSOR_CTRL) {
            if ((flags & DIRTY_PIPELINE_FMT) || state.fmt_pending) {
                LOG_DBG("apply: deferring sensor controls (format change pending)");
            } else {
                LOG_INFO("apply: sensor controls exp=%uus gain=%u fps=%.3f test=%u",
                         state.sensor_cfg.exposure_us,
                         state.sensor_cfg.analog_gain,
                         state.sensor_cfg.fps_mhz / 1000.0,
                         state.sensor_cfg.test_pattern);
                media_pipeline_set_sensor_controls(&state.sensor_cfg);
            }
        }

        if (flags & DIRTY_PIXPACK) {
            LOG_INFO("apply: pixel-packer format usb=0x%08X eth=0x%08X",
                     state.pixfmt_usb, state.pixfmt_eth);
            media_pipeline_set_pixpack_format(state.pixfmt_usb,
                                              state.pixfmt_eth);
        }

        if (flags & (DIRTY_PIPELINE_FMT | DIRTY_SENSOR_CTRL | DIRTY_CAMIO)) {
            if (camio_facts_refresh())
                flags |= DIRTY_CAMIO;
        }

        if (flags & DIRTY_CAMIO) {
            struct camio_cfg cc;
            config_get_camio(&cc);
            if (cc.present) {
                LOG_INFO("apply: camio aux I/O (in_func=%u strobe_en=%u role=%u)",
                         cc.in_function, cc.strobe_enable, cc.sync_role);
                int pre_stopped = 0;
                if (camio_sync_plan_follower(&cc) && !camio_sync_wants_follower() &&
                    config_get_stream_active()) {
                    LOG_INFO("apply: stopping the stream before camio drives the sensor's XVS/XHS");
                    media_pipeline_stream_off();
                    config_set_stream_active(false);
                    if (media_pipeline_wait_sensor_off(200) != 0)
                        LOG_WARN("apply: sensor still powered 200 ms after stream off -- XVS/XHS may be driven by both for a moment");
                    pre_stopped = 1;
                }
                camio_sync_apply(&cc);
                int fm = media_pipeline_set_sync_follower(camio_sync_wants_follower());
                {
                    static int64_t last_ov = -1;
                    int64_t ov = sync_fps_override();
                    if (ov != last_ov) {
                        last_ov = ov;
                        config_mark_sensor_ctrl_dirty();
                    }
                }
                int want_fast = camio_sync_uses_xtrig();
                if (want_fast && camio_sync_has_trigger() != 1) {
                    LOG_WARN("apply: TriggerMode On needs the camio v3 trigger block -- sensor stays free run");
                    want_fast = 0;
                }
                int tm = media_pipeline_set_trigger_mode(want_fast);
                if (tm < 0)
                    LOG_WARN("apply: sensor has no trigger mode -- TriggerMode On has no effect");
                if (((tm == 1 || fm == 1) && config_get_stream_active()) || pre_stopped) {
                    if (!(flags & DIRTY_STREAM)) {
                        LOG_INFO("apply: restarting stream for the sensor %s",
                                 fm == 1 || pre_stopped ? "sync role" : "trigger mode");
                        if (!pre_stopped) {
                            media_pipeline_stream_off();
                            config_set_stream_active(false);
                        }
                        if (media_pipeline_stream_on() == 0)
                            config_set_stream_active(true);
                    }
                }
            }
        }

        if (flags & DIRTY_GAMMA) {
            if (config_get_stream_active())
                media_pipeline_apply_gamma();
        }

        if (flags & DIRTY_STREAM) {
            if (state.sensor_cfg.stream_enable && config_get_stream_active() &&
                snap.start_gen == applied_start_gen) {
                LOG_INFO("apply: stream already on for this START -- no restart");
            } else if (state.sensor_cfg.stream_enable) {
                applied_start_gen = snap.start_gen;
                if (config_get_stream_active()) {
                    LOG_INFO("apply: stream restart");
                    media_pipeline_stream_off();
                    config_set_stream_active(false);
                }

                if (state.fmt_pending) {
                    LOG_INFO("apply: deferred pipeline format %ux%u",
                             state.sensor_cfg.width,
                             state.sensor_cfg.height);
                    media_pipeline_set_format(&state.sensor_cfg);

                    LOG_INFO("apply: re-apply sensor controls for new mode");
                    media_pipeline_set_sensor_controls(&state.sensor_cfg);

                    if (camio_facts_refresh()) {
                        struct camio_cfg cc;

                        config_get_camio(&cc);
                        if (cc.present)
                            camio_sync_apply(&cc);
                    }
                }
                if (state.pixpack_pending || state.fmt_pending) {
                    LOG_INFO("apply: deferred pixel-packer usb=0x%08X eth=0x%08X",
                             state.pixfmt_usb, state.pixfmt_eth);
                    media_pipeline_set_pixpack_format(state.pixfmt_usb,
                                                      state.pixfmt_eth);
                }
                config_clear_pending_seen(snap.pending_gen);

                LOG_INFO("apply: stream ON");
                if (media_pipeline_stream_on() == 0)
                    config_set_stream_active(true);
            } else {
                if (config_get_stream_active()) {
                    LOG_INFO("apply: stream OFF");
                    media_pipeline_stream_off();
                    config_set_stream_active(false);
                }
            }
        }

        if (flags & DIRTY_AE_CFG) {
            LOG_INFO("apply: AE config ae_enabled=%d target=%.2f",
                     state.ae_exposure_auto,
                     state.imaging.ae_target_brightness);
        }

        config_set_actual_fps(media_pipeline_get_actual_fps());
        if (usb_seq_ok)
            config_set_usb_block_applied(usb_seq);
        config_publish_exposure();
        camio_sync_applied(snap.camio_gen);
    }

    LOG_INFO("apply_loop: stopped");
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n\n"
        "Camera Configuration Manager Daemon for CHC5\n\n"
        "  -s PATH   IPC socket path [%s]\n"
        "  -d PATH   USB I2C chardev path [%s]\n"
        "  -v        Show version and exit\n"
        "  -h        Show this help\n",
        prog, CAMCFGD_SOCK_PATH, USB_CHARDEV_PATH);
}

static void print_version(void)
{
    printf("camcfgd %s-g%s%s (rev %d, build %d, %s)\n",
           CAMCFGD_VERSION_STRING, CAMCFGD_GIT_SHA,
           CAMCFGD_GIT_DIRTY, CAMCFGD_GIT_REV,
           CAMCFGD_BUILD_NUM, CAMCFGD_BUILD_DATE);
}

int main(int argc, char **argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "s:d:vh")) != -1) {
        switch (opt) {
        case 's':
            break;
        case 'd':
            break;
        case 'v':
            print_version();
            return 0;
        case 'h':
        default:
            usage(argv[0]);
            return (opt == 'h') ? 0 : 1;
        }
    }

    LOG_INFO("=== camcfgd %s-g%s%s rev %d build %d (%s) ===",
             CAMCFGD_VERSION_STRING, CAMCFGD_GIT_SHA,
             CAMCFGD_GIT_DIRTY, CAMCFGD_GIT_REV,
             CAMCFGD_BUILD_NUM, CAMCFGD_BUILD_DATE);

    {
        FILE *f = fopen("/run/camcfgd/version", "w");
        if (f) {
            fprintf(f, "%s-g%s%s rev %d build %d (%s)\n",
                    CAMCFGD_VERSION_STRING, CAMCFGD_GIT_SHA, CAMCFGD_GIT_DIRTY,
                    CAMCFGD_GIT_REV, CAMCFGD_BUILD_NUM, CAMCFGD_BUILD_DATE);
            fclose(f);
        } else {
            LOG_ERR("Could not write /run/camcfgd/version: %s", strerror(errno));
        }
    }

    config_init();
    LOG_INFO("Config subsystem initialised");

    {
        uint16_t fps = 25, cx = 0, cy = 0, ae_permille = 500, mm = 1, hs = 0, af = 0,
                 he = 0, spd = 40, prio = 0, hl = 1, fk = 0, once = 0, awbf = 0,
                 awbmn = 0, awbmx = 0, awbrt = 0, awbct = 0, ca = 1;
        uint32_t exp_us = 0;
        if (imaging_pub_load(&fps, &cx, &cy, &ae_permille, &mm, &hs, &af, &he,
                             &spd, &prio, &hl, &fk, &once, &exp_us, &awbf,
                             &awbmn, &awbmx, &awbrt, &awbct, &ca) == 0) {
            config_apply_imaging(fps, cx, cy, (float)ae_permille / 1000.0f,
                                 (uint8_t)mm, (uint8_t)hs, (uint8_t)af, (uint8_t)he,
                                 (uint8_t)spd, (uint8_t)prio, (uint8_t)hl,
                                 (uint8_t)fk, (uint8_t)once, exp_us, (uint8_t)awbf,
                                 (uint8_t)awbmn, (uint8_t)awbmx, (uint8_t)awbrt,
                                 (uint8_t)awbct, (uint8_t)ca);
            LOG_INFO("imaging: applied published config "
                     "(hdmi_fps=%u crop=%s ae_target=%.2f)",
                     fps, ca ? "auto-centre" : "manual",
                     (float)ae_permille / 1000.0f);
            config_peek_dirty();
        } else {
            LOG_INFO("imaging: no published config at %s, using defaults",
                     IMAGING_PUB_PATH);
        }
    }

    if (media_pipeline_init() < 0) {
        LOG_ERR("Media pipeline init failed - this camera cannot produce a "
                "frame (no /dev/media0, or no sensor entity in the graph)");
        config_set_pipeline_ready(false);
    } else {
        config_set_pipeline_ready(true);
    }

    LOG_INFO("Clearing stale stream state");
    media_pipeline_stream_off();

    config_load_sensor_caps();
    {
        imgsensor_cfg_t scfg;
        config_get_sensor(&scfg);
        media_pipeline_set_format(&scfg);
        media_pipeline_set_sensor_controls(&scfg);
    }

    signal(SIGINT,  sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGPIPE, SIG_IGN);

    if (usb_monitor_start() < 0)
        LOG_WARN("USB monitor start failed");

    if (ipc_server_start() < 0)
        LOG_ERR("IPC server start failed");

    LOG_INFO("=== camcfgd ready ===");

    sd_notify(0, "READY=1\n"
                 "STATUS=Camera config daemon ready");

    apply_loop();

    sd_notify(0, "STOPPING=1\n"
                 "STATUS=Shutting down");
    LOG_INFO("=== camcfgd shutting down ===");

    ipc_server_stop();
    usb_monitor_stop();

    if (config_get_stream_active())
        media_pipeline_stream_off();

    media_pipeline_close();
    config_destroy();

    LOG_INFO("=== camcfgd done ===");
    return 0;
}
