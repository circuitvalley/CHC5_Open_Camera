/* SPDX-License-Identifier: CC-BY-NC-ND-4.0 */
/*
 * ccm_util.h - colour matrix interpolation helpers
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */
#ifndef CAMCFGD_CCM_UTIL_H
#define CAMCFGD_CCM_UTIL_H

#include <stdint.h>

#define CCM_CT_MAX 8

typedef struct {
    float   kelvin;
    int32_t m[12];
} ccm_ct_entry;

void  ccm_interp_ct(const ccm_ct_entry *list, int n, float ct, int32_t out[12]);
void  ccm_apply_saturation(const int32_t in[12], float s, int32_t out[12]);
float piecewise_interp(const float *xs, const float *ys, int n, float x);

#endif
