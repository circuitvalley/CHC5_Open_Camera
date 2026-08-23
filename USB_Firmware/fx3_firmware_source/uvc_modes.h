/*
 * uvc_modes.h - EEPROM mode table definitions
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */
#ifndef _UVC_MODES_H_
#define _UVC_MODES_H_

#include <cyu3types.h>

#define UVC_MODE_MAX          8u
#define UVC_EEPROM_ADDR       0xA0u
#define UVC_MODEPART_BASE     0x38000u
#define UVC_MODE_HDR_SIZE     64u
#define UVC_MODE_SLOT_SIZE    0x2800u
#define UVC_MODE_MAGIC        0x444F4D43u
#define UVC_MODE_ENTRY_SIZE   16u

#define UVC_HDR_OFF_GAIN_MAX_DB10   36u
#define UVC_GAIN_MAX_DB10_FALLBACK  720u

typedef struct __attribute__((packed)) uvc_mode_s {
    uint16_t width;
    uint16_t height;
    uint16_t fps;
    uint16_t reserved0;
    uint32_t pixel_format;
    uint8_t  binning;
    uint8_t  src;
    uint16_t reserved;
} uvc_mode_t;

extern uvc_mode_t g_uvc_modes[UVC_MODE_MAX];
extern uint8_t    g_uvc_nmodes;

extern uint16_t   g_uvc_gain_max_db10;

void CyFxUvcLoadModesAndBuild(void);

uint8_t *CyFxUvcGetSSConfigDscr(void);
uint8_t *CyFxUvcGetHSConfigDscr(void);

#endif
