/* SPDX-License-Identifier: CC-BY-NC-ND-4.0 */
/*
 * config.h - unified config state with mutex-protected access
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#ifndef CAMCFGD_CONFIG_H
#define CAMCFGD_CONFIG_H

#include "common.h"
#include "ccm_util.h"

struct camio_cfg;

void config_init(void);
void config_destroy(void);

void config_set_sensor(const imgsensor_cfg_t *cfg, uint8_t source);

void config_set_exposure_us(uint32_t exposure_us);
void config_set_gain_raw(uint16_t gain);
void config_set_black_level(uint16_t level);
uint16_t config_get_black_level(void);
void config_set_test_pattern(uint8_t pat);
void config_set_resolution(uint16_t w, uint16_t h);
void config_set_offset(uint16_t x, uint16_t y);
void config_set_fps(uint16_t fps);
void config_set_fps_f(float fps);
float config_get_fps_f(void);
void  config_set_max_fps(float fps);
void  config_set_actual_fps(float fps);
void   config_load_sensor_caps(void);
void   config_apply_bayer_order(void);
bool   config_take_ccm_update(int32_t out[12], bool *enable);
void    config_set_awb_gain_min(float v);
void    config_set_awb_gain_max(float v);
void    config_set_awb_damp(float v);
uint8_t config_get_awb_gray_thr(void);
const uint8_t (*config_get_gamma_lut(void))[256];
void config_reload_gamma(void);
uint8_t config_get_gamma_x10(void);
void config_set_gamma_x100(uint16_t v);
bool config_get_output_gamma(uint8_t out[3][256], bool *host_off);
uint8_t config_get_awb_gray_en(void);
uint8_t config_get_awb_y_lo(void);
uint8_t config_get_awb_y_hi(void);



void  config_set_awb_ct_estimate(float kelvin);
float config_get_awb_ct_estimate(void);

void  config_set_saturation(float s);
float config_get_saturation(void);

void config_set_pixfmt_usb(uint32_t pfnc);
void config_set_pixfmt_eth(uint32_t pfnc);

void config_set_stream(bool enable);

void config_set_stream_active(bool active);

void config_set_pipeline_ready(bool ready);


void config_set_ae_roi(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void config_get_ae_roi(uint16_t *x, uint16_t *y, uint16_t *w, uint16_t *h);

void config_set_ae_target(float target01);

void config_set_ae_metering_mode(uint8_t mode);

void config_publish_exposure(void);

void config_mark_stream_dirty(void);

void config_clear_pending(void);

void config_apply_imaging(uint16_t hdmi_max_fps, uint16_t hdmi_crop_x,
                          uint16_t hdmi_crop_y, float ae_target,
                          uint8_t ae_metering_mode, uint8_t hdmi_standalone,
                          uint8_t ae_force_enable, uint8_t hdmi_enable,
                          uint8_t ae_speed, uint8_t ae_priority,
                          uint8_t ae_highlight, uint8_t ae_flicker,
                          uint8_t ae_once, uint32_t exposure_us,
                          uint8_t awb_force_enable, uint8_t awb_gain_min_x100,
                          uint8_t awb_gain_max_x10, uint8_t awb_rate_x100,
                          uint8_t awb_color_temp_d100, uint8_t hdmi_crop_auto);


void config_set_auto_exposure(bool enable);
void config_set_auto_gain(bool enable);

void config_get_full(struct camcfg_state_s *out);

void config_get_sensor(imgsensor_cfg_t *out);

bool config_get_stream_active(void);

bool config_get_host_active(void);

bool config_get_usb_active(void);

void     config_set_ae_exp_lower_us(uint32_t us);
void     config_set_ae_exp_upper_us(uint32_t us);

#define CFG_LINK_USB 0
#define CFG_LINK_ETH 1
void     config_set_link_bw_limit(int link, uint32_t bps);
void     config_set_link_bw_mode(int link, bool on);
uint32_t config_get_link_bw_bps(int link);

bool config_get_ae_enabled(void);

bool config_get_auto_gain(void);

void config_set_auto_wb(bool enable);
bool config_get_auto_wb(void);

void config_set_reverse_x(bool enable);
void config_set_reverse_y(bool enable);
bool config_get_reverse_x(void);
bool config_get_reverse_y(void);
int  config_get_effective_hflip(void);
int  config_get_effective_vflip(void);
void    config_set_live_cfa_order(int order);
uint8_t config_get_live_cfa_order(void);
void config_request_awb_once(void);
void  config_set_wb_ratio(int channel, float ratio);
float config_get_wb_ratio(int channel);
void  config_set_color_temp(uint16_t kelvin);




uint8_t config_get_ae_once(void);
void    config_request_ae_once_exposure(void);
void    config_request_ae_once_gain(void);

uint8_t config_get_hdmi_standalone(void);
uint8_t config_get_hdmi_enable(void);

void config_hdmi_standalone_tick(void);

uint16_t config_get_hdmi_max_fps(void);

void config_get_hdmi_crop(uint16_t *x, uint16_t *y, uint8_t *automatic);

uint32_t config_wait_dirty(void);

uint32_t config_peek_dirty(void);

void config_signal_shutdown(void);

void config_set_camio(const struct camio_cfg *cfg);
void config_get_camio(struct camio_cfg *out);

void config_publish_camio(void);

#endif
