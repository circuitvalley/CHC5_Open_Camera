/* SPDX-License-Identifier: CC-BY-NC-ND-4.0 */
/*
 * genconv.h - GenICam <-> sensor unit conversions
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#ifndef CAMCFGD_GENCONV_H
#define CAMCFGD_GENCONV_H

#include "common.h"

uint8_t genconv_pfnc_to_bits(uint32_t pfnc);

uint32_t genconv_pfnc_to_pixpack_mbus(uint32_t pfnc);

int genconv_apply_register(uint32_t addr, const uint8_t *value, int len);

#endif
