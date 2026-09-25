// SPDX-License-Identifier: CC-BY-NC-ND-4.0
/*
 * imaging_pub.c - read the imaging configuration published by chc5_platformd
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#include "imaging_pub.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int imaging_pub_load(uint16_t *hdmi_max_fps, uint16_t *hdmi_crop_x,
                     uint16_t *hdmi_crop_y, uint16_t *ae_target_permille,
                     uint16_t *ae_metering_mode, uint16_t *hdmi_standalone,
                     uint16_t *ae_force_enable, uint16_t *hdmi_enable,
                     uint16_t *ae_speed, uint16_t *ae_priority,
                     uint16_t *ae_highlight, uint16_t *ae_flicker,
                     uint16_t *ae_once, uint32_t *exposure_us,
                     uint16_t *awb_force_enable, uint16_t *awb_gain_min_x100,
                     uint16_t *awb_gain_max_x10, uint16_t *awb_rate_x100,
                     uint16_t *awb_color_temp_d100, uint16_t *hdmi_crop_auto)
{
    FILE *f = fopen(IMAGING_PUB_PATH, "r");
    if (!f)
        return -1;

    int got = 0;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\0')
            continue;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        long v = strtol(val, NULL, 10);
        if (v < 0)     v = 0;
        if (v > 65535) v = 65535;
        uint16_t uv = (uint16_t)v;

        if (hdmi_max_fps && strcmp(key, "hdmi_max_fps") == 0) {
            *hdmi_max_fps = uv; got++;
        } else if (hdmi_crop_auto && strcmp(key, "hdmi_crop_auto") == 0) {
            *hdmi_crop_auto = uv; got++;
        } else if (hdmi_crop_x && strcmp(key, "hdmi_crop_x") == 0) {
            *hdmi_crop_x = uv; got++;
        } else if (hdmi_crop_y && strcmp(key, "hdmi_crop_y") == 0) {
            *hdmi_crop_y = uv; got++;
        } else if (ae_target_permille && strcmp(key, "ae_target_permille") == 0) {
            *ae_target_permille = uv; got++;
        } else if (ae_metering_mode && strcmp(key, "ae_metering_mode") == 0) {
            *ae_metering_mode = uv; got++;
        } else if (hdmi_standalone && strcmp(key, "hdmi_standalone") == 0) {
            *hdmi_standalone = uv; got++;
        } else if (ae_force_enable && strcmp(key, "ae_force_enable") == 0) {
            *ae_force_enable = uv; got++;
        } else if (hdmi_enable && strcmp(key, "hdmi_enable") == 0) {
            *hdmi_enable = uv; got++;
        } else if (ae_speed && strcmp(key, "ae_speed") == 0) {
            *ae_speed = uv; got++;
        } else if (ae_priority && strcmp(key, "ae_priority") == 0) {
            *ae_priority = uv; got++;
        } else if (ae_highlight && strcmp(key, "ae_highlight") == 0) {
            *ae_highlight = uv; got++;
        } else if (ae_flicker && strcmp(key, "ae_flicker") == 0) {
            *ae_flicker = uv; got++;
        } else if (ae_once && strcmp(key, "ae_once") == 0) {
            *ae_once = uv; got++;
        } else if (awb_force_enable && strcmp(key, "awb_force_enable") == 0) {
            *awb_force_enable = uv; got++;
        } else if (awb_gain_min_x100 && strcmp(key, "awb_gain_min_x100") == 0) {
            *awb_gain_min_x100 = uv; got++;
        } else if (awb_gain_max_x10 && strcmp(key, "awb_gain_max_x10") == 0) {
            *awb_gain_max_x10 = uv; got++;
        } else if (awb_rate_x100 && strcmp(key, "awb_rate_x100") == 0) {
            *awb_rate_x100 = uv; got++;
        } else if (awb_color_temp_d100 && strcmp(key, "awb_color_temp_d100") == 0) {
            *awb_color_temp_d100 = uv; got++;
        } else if (exposure_us && strcmp(key, "exposure_us") == 0) {
            long e = strtol(val, NULL, 10);
            if (e < 0) e = 0;
            if (e > 1000000) e = 1000000;
            *exposure_us = (uint32_t)e; got++;
        }
    }
    fclose(f);
    return (got > 0) ? 0 : -1;
}
