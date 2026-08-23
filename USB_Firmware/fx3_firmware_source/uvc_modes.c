/*
 * uvc_modes.c - EEPROM mode table and runtime UVC descriptor build
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */
#include <cyu3system.h>
#include <cyu3os.h>
#include <cyu3error.h>
#include <cyu3uart.h>
#include <cyu3i2c.h>
#include <cyu3types.h>
#include <cyu3utils.h>
#include "uvc_modes.h"
#include "sensor_chc5.h"

extern const uint8_t CyFxUSBSSConfigDscr[];
extern const uint8_t CyFxUSBHSConfigDscr[];

uvc_mode_t g_uvc_modes[UVC_MODE_MAX];
uint8_t    g_uvc_nmodes;

static uint8_t g_ss_cfg[512];
static uint8_t g_hs_cfg[256];

uint8_t *CyFxUvcGetSSConfigDscr(void) { return g_ss_cfg; }
uint8_t *CyFxUvcGetHSConfigDscr(void) { return g_hs_cfg; }

static const uvc_mode_t g_baked_ladder[] = {
    { 1920, 1080, 30, 0, PFNC_YUV422_8, 0, 0, 0 },
};

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
static void wr32(uint8_t *p, uint32_t v)
{ p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF; }

static uint32_t CyFxUvcCrc32Upd(uint32_t crc, const uint8_t *p, uint32_t n)
{
    uint32_t b;
    while (n--)
    {
        crc ^= *p++;
        for (b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return crc;
}

static CyU3PReturnStatus_t CyFxUvcEepromRead(uint32_t addr, uint8_t *buf, uint32_t len)
{
    CyU3PI2cPreamble_t  pre;
    CyU3PReturnStatus_t s;
    while (len)
    {
        uint16_t n = (len > 64u) ? 64u : (uint16_t)len;
        pre.buffer[0] = (uint8_t)(UVC_EEPROM_ADDR        | ((addr >> 15) & 0x0E));
        pre.buffer[1] = (uint8_t)(addr >> 8);
        pre.buffer[2] = (uint8_t)(addr & 0xFF);
        pre.buffer[3] = (uint8_t)(UVC_EEPROM_ADDR | 0x01 | ((addr >> 15) & 0x0E));
        pre.length    = 4;
        pre.ctrlMask  = 0x0004;
        s = CyU3PI2cReceiveBytes(&pre, buf, n, 0);
        if (s != CY_U3P_SUCCESS) return s;
        addr += n; buf += n; len -= n;
    }
    return CY_U3P_SUCCESS;
}

static CyBool_t CyFxUvcModeSlotValid(uint32_t slot, uint32_t *outLen,
                                     uint16_t *outGainMaxDb10)
{
    uint8_t  hdr[UVC_MODE_HDR_SIZE];
    uint8_t  tmp[64];
    uint32_t plen, want, crc, off;

    if (CyFxUvcEepromRead(slot, hdr, UVC_MODE_HDR_SIZE) != CY_U3P_SUCCESS) return CyFalse;
    if (rd32(hdr + 0) != UVC_MODE_MAGIC) return CyFalse;
    plen = rd32(hdr + 12);
    if (plen == 0 || (plen % UVC_MODE_ENTRY_SIZE) != 0 ||
        plen > (UVC_MODE_MAX * UVC_MODE_ENTRY_SIZE)) return CyFalse;
    want = rd32(hdr + 4);

    crc = CyFxUvcCrc32Upd(0xFFFFFFFFu, hdr + 8, UVC_MODE_HDR_SIZE - 8);
    for (off = 0; off < plen; )
    {
        uint16_t n = (plen - off > 64u) ? 64u : (uint16_t)(plen - off);
        if (CyFxUvcEepromRead(slot + UVC_MODE_HDR_SIZE + off, tmp, n) != CY_U3P_SUCCESS) return CyFalse;
        crc = CyFxUvcCrc32Upd(crc, tmp, n);
        off += n;
    }
    if ((crc ^ 0xFFFFFFFFu) != want) return CyFalse;

    *outLen = plen;
    if (outGainMaxDb10)
        *outGainMaxDb10 = (uint16_t)(hdr[UVC_HDR_OFF_GAIN_MAX_DB10] |
                                     ((uint16_t)hdr[UVC_HDR_OFF_GAIN_MAX_DB10 + 1] << 8));
    return CyTrue;
}

static uint8_t emit_frame(uint8_t *p, uint8_t idx, const uvc_mode_t *m)
{
    uint16_t w = m->width, h = m->height;
    uint16_t fps    = m->fps ? m->fps : 30;
    uint32_t ival   = 10000000u / fps;
    uint32_t fbytes = (uint32_t)w * (uint32_t)h * 2u;
    uint64_t br     = (uint64_t)w * h * 16u * fps;
    if (br > 0xFFFFFFFFu) br = 0xFFFFFFFFu;

    p[0] = 0x1E;
    p[1] = 0x24;
    p[2] = 0x05;
    p[3] = idx;
    p[4] = 0x00;
    wr16(p + 5,  w);
    wr16(p + 7,  h);
    wr32(p + 9,  (uint32_t)br);
    wr32(p + 13, (uint32_t)br);
    wr32(p + 17, fbytes);
    wr32(p + 21, ival);
    p[25] = 1;
    wr32(p + 26, ival);
    return 0x1E;
}

static void build_cfg(uint8_t *out, const uint8_t *tmpl,
                      const uvc_mode_t *modes, uint8_t n)
{
    uint16_t total = rd16(tmpl + 2);
    uint16_t off, fmt = 0, vshdr = 0, last01 = 0;

    for (off = 9; off + 2 < total; )
    {
        uint8_t blen = tmpl[off];
        if (blen == 0) break;
        if (tmpl[off + 1] == 0x24)
        {
            if (tmpl[off + 2] == 0x01) last01 = off;
            else if (tmpl[off + 2] == 0x04) { fmt = off; vshdr = last01; break; }
        }
        off += blen;
    }
    if (fmt == 0) { CyU3PMemCopy(out, (uint8_t *)tmpl, total); return; }

    uint16_t frames_start = (uint16_t)(fmt + tmpl[fmt]);
    uint16_t fe = frames_start;
    while (fe + 2 < total && tmpl[fe + 1] == 0x24 && tmpl[fe + 2] == 0x05)
        fe = (uint16_t)(fe + tmpl[fe]);
    uint16_t old_frames = (uint16_t)(fe - frames_start);

    CyU3PMemCopy(out, (uint8_t *)tmpl, frames_start);
    uint16_t w = frames_start;
    uint8_t  i;
    for (i = 0; i < n; i++)
        w = (uint16_t)(w + emit_frame(out + w, (uint8_t)(i + 1), &modes[i]));
    uint16_t new_frames = (uint16_t)(w - frames_start);
    CyU3PMemCopy(out + w, (uint8_t *)(tmpl + fe), (uint16_t)(total - fe));

    uint16_t newtotal = (uint16_t)(total - old_frames + new_frames);
    wr16(out + 2, newtotal);
    wr16(out + vshdr + 4,
         (uint16_t)(rd16(tmpl + vshdr + 4) + new_frames - old_frames));
    out[fmt + 4] = n;
}

uint16_t g_uvc_gain_max_db10 = UVC_GAIN_MAX_DB10_FALLBACK;

void CyFxUvcLoadModesAndBuild(void)
{
    uint32_t plen = 0;
    uint16_t gmax = 0;

    CyU3PMemCopy((uint8_t *)g_uvc_modes, (uint8_t *)g_baked_ladder, sizeof(g_baked_ladder));
    g_uvc_nmodes = (uint8_t)(sizeof(g_baked_ladder) / sizeof(g_baked_ladder[0]));

    if (CyFxUvcModeSlotValid(UVC_MODEPART_BASE, &plen, &gmax))
    {
        if (gmax > 0u) {
            g_uvc_gain_max_db10 = gmax;
            CyU3PDebugPrint(4, "UVC gain max: %d.%d dB from EEPROM\r\n",
                            gmax / 10, gmax % 10);
        }
        uint8_t nb = (uint8_t)(plen / UVC_MODE_ENTRY_SIZE);
        uint8_t tmp[UVC_MODE_MAX * UVC_MODE_ENTRY_SIZE];
        if (nb >= 1 && nb <= UVC_MODE_MAX &&
            CyFxUvcEepromRead(UVC_MODEPART_BASE + UVC_MODE_HDR_SIZE, tmp, plen) == CY_U3P_SUCCESS)
        {
            CyU3PMemCopy((uint8_t *)g_uvc_modes, tmp, plen);
            g_uvc_nmodes = nb;
            CyU3PDebugPrint(4, "UVC modes: EEPROM slot, %d modes\r\n", nb);
        }
    }
    else
    {
        CyU3PDebugPrint(4, "UVC modes: baked ladder, %d modes\r\n", g_uvc_nmodes);
    }

    build_cfg(g_ss_cfg, CyFxUSBSSConfigDscr, g_uvc_modes, g_uvc_nmodes);
    {
        uvc_mode_t hs  = g_uvc_modes[0];
        uint32_t   bpf = (uint32_t)hs.width * hs.height * 2u;
        uint32_t   cap = bpf ? (40000000u / bpf) : 30u;
        if (cap > 30u) cap = 30u;
        if (cap < 1u)  cap = 1u;
        if (hs.fps == 0 || hs.fps > cap) hs.fps = (uint16_t)cap;
        build_cfg(g_hs_cfg, CyFxUSBHSConfigDscr, &hs, 1);
    }
}
