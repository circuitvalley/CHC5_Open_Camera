/* SPDX-License-Identifier: CC-BY-NC-ND-4.0 */
/*
 * camio_sync.h - camcfgd client for the chc5_camio_sync aux-I/O + sync block
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#ifndef CAMCFGD_CAMIO_SYNC_H
#define CAMCFGD_CAMIO_SYNC_H

#include <stdbool.h>
#include <stdint.h>
#include "chc5_camio.h"

int  camio_sync_open(void);
bool camio_sync_present(void);
void camio_sync_close(void);

int  camio_sync_hw_state(void);
int  camio_sync_has_ptp_pulse(void);

int  camio_sync_apply(const struct camio_cfg *cfg);

int  camio_sync_read_status(uint32_t *status, uint32_t *in_status,
                            uint32_t *sync_status);

#endif
