/* SPDX-License-Identifier: CC-BY-NC-ND-4.0 */
/*
 * imaging_pub.h - read chc5_platformd's published imaging/output config
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#ifndef CAMCFGD_IMAGING_PUB_H
#define CAMCFGD_IMAGING_PUB_H

#include <stdint.h>

#ifndef IMAGING_PUB_PATH
#define IMAGING_PUB_PATH   "/run/chc5_platformd/imaging"
#endif

int imaging_pub_load(uint16_t *hdmi_max_fps, uint16_t *hdmi_crop_x,
                     uint16_t *hdmi_crop_y, uint16_t *ae_target_permille,
                     uint16_t *ae_metering_mode, uint16_t *hdmi_standalone,
                     uint16_t *ae_force_enable, uint16_t *hdmi_enable,
                     uint16_t *ae_speed, uint16_t *ae_priority,
                     uint16_t *ae_highlight, uint16_t *ae_flicker,
                     uint16_t *ae_once, uint32_t *exposure_us,
                     uint16_t *awb_force_enable, uint16_t *awb_gain_min_x100,
                     uint16_t *awb_gain_max_x10, uint16_t *awb_rate_x100,
                     uint16_t *awb_color_temp_d100, uint16_t *hdmi_crop_auto);

#endif
