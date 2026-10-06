/* SPDX-License-Identifier: CC-BY-NC-ND-4.0 */
/*
 * usb_monitor.h - Poll I2C slave chardev for USB camera config
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#ifndef CAMCFGD_USB_MONITOR_H
#define CAMCFGD_USB_MONITOR_H

#include "common.h"

void usb_cfg_from_wire(const chc5_i2c_cfg_t *w, imgsensor_cfg_t *c);

int  usb_monitor_start(void);

void usb_monitor_stop(void);

#endif
