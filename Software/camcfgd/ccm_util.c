// SPDX-License-Identifier: CC-BY-NC-ND-4.0
/*
 * ccm_util.c - colour matrix interpolation helpers
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */
#include <math.h>
#include "ccm_util.h"

void ccm_interp_ct(const ccm_ct_entry *list, int n, float ct, int32_t out[12])
{
    if (n < 1)
        return;
    if (n == 1 || ct <= list[0].kelvin) {
        for (int i = 0; i < 12; i++) out[i] = list[0].m[i];
        return;
    }
    if (ct >= list[n - 1].kelvin) {
        for (int i = 0; i < 12; i++) out[i] = list[n - 1].m[i];
        return;
    }
    for (int i = 0; i + 1 < n; i++) {
        if (ct <= list[i + 1].kelvin) {
            float span = list[i + 1].kelvin - list[i].kelvin;
            float t = (span > 1e-3f) ? (ct - list[i].kelvin) / span : 0.0f;
            for (int k = 0; k < 12; k++) {
                float v = (float)list[i].m[k] +
                          t * (float)(list[i + 1].m[k] - list[i].m[k]);
                out[k] = (int32_t)lrintf(v);
            }
            return;
        }
    }
}
void ccm_apply_saturation(const int32_t in[12], float s, int32_t out[12])
{
    static const float L[3] = { 0.299f, 0.587f, 0.114f };
    float m[12];

    for (int i = 0; i < 12; i++)
        m[i] = (float)in[i];

    float o[12];
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 4; col++) {
            float acc = 0.0f;
            for (int k = 0; k < 3; k++) {
                float msk = s * (row == k ? 1.0f : 0.0f) + (1.0f - s) * L[k];
                acc += msk * m[k * 4 + col];
            }
            o[row * 4 + col] = acc;
        }
    }
    for (int i = 0; i < 12; i++) {
        float v = o[i];
        if (v < -32768.0f) v = -32768.0f;
        if (v >  32767.0f) v =  32767.0f;
        out[i] = (int32_t)lrintf(v);
    }
}
float piecewise_interp(const float *xs, const float *ys, int n, float x)
{
    if (n <= 0)
        return 0.0f;
    if (x <= xs[0])
        return ys[0];
    if (x >= xs[n - 1])
        return ys[n - 1];
    for (int i = 0; i + 1 < n; i++) {
        if (x <= xs[i + 1]) {
            float span = xs[i + 1] - xs[i];
            float t = (span > 1e-9f) ? (x - xs[i]) / span : 0.0f;
            return ys[i] + t * (ys[i + 1] - ys[i]);
        }
    }
    return ys[n - 1];
}
