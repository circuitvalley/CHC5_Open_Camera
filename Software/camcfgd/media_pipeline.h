/* SPDX-License-Identifier: CC-BY-NC-ND-4.0 */
/*
 * media_pipeline.h - Media controller discovery & V4L2 pipeline config
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#ifndef CAMCFGD_MEDIA_PIPELINE_H
#define CAMCFGD_MEDIA_PIPELINE_H

#include "common.h"

int  media_pipeline_init(void);
void media_pipeline_close(void);

int  media_pipeline_set_format(const imgsensor_cfg_t *cfg);
int  media_pipeline_set_pixpack_format(uint32_t pfnc_usb, uint32_t pfnc_eth);
int  media_pipeline_set_sensor_controls(const imgsensor_cfg_t *cfg);


int  media_pipeline_stream_on(void);
int  media_pipeline_stream_off(void);
void media_pipeline_refresh_cfa_order(void);

int  media_pipeline_set_ccm(const int32_t *matrix, bool enable);
void media_pipeline_ccm_tune_tick(void);
void media_pipeline_apply_gamma(void);




int  media_pipeline_set_aestats_fmt(uint32_t width, uint32_t height);
void media_pipeline_apply_manual_wb(void);

int  media_pipeline_set_wb_gains(float gain_r, float gain_b);


int32_t  media_pipeline_get_gain_max(void);
int32_t  media_pipeline_get_bl_default(void);

#define CHC5_BLKC_SUBTRACT_MAX  0x3FFF

int32_t  blkc_subtract_for(int32_t black_level);

float media_pipeline_get_actual_fps(void);

float media_pipeline_get_max_fps(void);

double media_pipeline_get_gain_db_max(void);

#endif
