/* SPDX-License-Identifier: CC-BY-NC-ND-4.0 */
/*
 * camcfgd_client.h - IPC client library for camcfgd
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#ifndef CAMCFGD_CLIENT_H
#define CAMCFGD_CLIENT_H

#include "common.h"

int camcfgd_connect(void);

void camcfgd_disconnect(void);

int camcfgd_is_connected(void);

int camcfgd_send_sensor_cfg(const imgsensor_cfg_t *cfg);

int camcfgd_send_genreg(uint32_t addr, const uint8_t *value, int len);

int camcfgd_stream_ctrl(int enable);

int camcfgd_get_config(struct camcfg_state_s *out);

#endif
