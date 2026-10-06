/*
 * sensor_chc5.c - CHC5 camera head I2C config/readback driver
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */
#include <cyu3system.h>
#include <cyu3os.h>
#include <cyu3dma.h>
#include <cyu3error.h>
#include <cyu3uart.h>
#include <cyu3i2c.h>
#include <cyu3types.h>
#include <cyu3gpio.h>
#include <cyu3utils.h>
#include "sensor_chc5.h"
#include "uvc_modes.h"

static chc5_i2c_cfg_t sensor_cfg_buf = { 0 };

chc5_i2c_cfg_t *sensor_cfg        = NULL;
chc5_i2c_cfg_t *selected_img_mode = NULL;

#define I2C_GAP_US  1000u
void sensor_i2c_error_read(void)
{
#ifndef I2C_NO_ERROR_READ
    CyU3PI2cError_t err = (CyU3PI2cError_t)0xFF;

    (void)CyU3PI2cGetErrorCode(&err);
#endif
}

static void SensorI2CAccessDelay(CyU3PReturnStatus_t status)
{
    if (status == CY_U3P_SUCCESS)
        CyU3PBusyWait(I2C_GAP_US);
    else
        sensor_i2c_error_read();
}

#define I2C_LOCK_WAIT_MS  500u
static CyU3PMutex g_i2c_lock;
static CyBool_t   g_i2c_lock_ok = CyFalse;

void sensor_i2c_lock_init(void)
{
    g_i2c_lock_ok = (CyU3PMutexCreate(&g_i2c_lock, CYU3P_INHERIT) == CY_U3P_SUCCESS);
}

static CyBool_t i2c_lock(void)
{
    return g_i2c_lock_ok && CyU3PMutexGet(&g_i2c_lock, I2C_LOCK_WAIT_MS) == CY_U3P_SUCCESS;
}

static void i2c_unlock(CyBool_t held)
{
    if (held)
        CyU3PMutexPut(&g_i2c_lock);
}

CyU3PReturnStatus_t sensor_i2c_write(uint8_t reg, uint8_t *data, uint8_t size)
{
    CyBool_t held;
    CyU3PReturnStatus_t status;
    CyU3PI2cPreamble_t preamble;

    preamble.buffer[0] = SENSOR_ADDR_WR;
    preamble.buffer[1] = reg;
    preamble.length    = 2;
    preamble.ctrlMask  = 0x0000;

    held = i2c_lock();
    status = CyU3PI2cTransmitBytes(&preamble, data, size, 0);
    SensorI2CAccessDelay(status);
    i2c_unlock(held);
    if (status != CY_U3P_SUCCESS)
        CyU3PDebugPrint(4, "WR fail 0x%x\r\n", status);
    return status;
}

CyU3PReturnStatus_t sensor_i2c_read(uint8_t reg, uint8_t *data, uint8_t size)
{
    CyBool_t held;
    CyU3PReturnStatus_t status;
    CyU3PI2cPreamble_t preamble;

    preamble.length    = 3;
    preamble.buffer[0] = SENSOR_ADDR_RD & I2C_SLAVEADDR_MASK;
    preamble.buffer[1] = reg;
    preamble.buffer[2] = SENSOR_ADDR_RD;
    preamble.ctrlMask  = 1 << 1;

    held = i2c_lock();
    status = CyU3PI2cReceiveBytes(&preamble, data, size, 0);
    SensorI2CAccessDelay(status);
    i2c_unlock(held);
    if (status != CY_U3P_SUCCESS)
        CyU3PDebugPrint(4, "RD fail 0x%x\r\n", status);
    return status;
}

static uint8_t         g_write_seq = 0;
static chc5_i2c_cfg_t sensor_cfg_last;
static CyBool_t        g_cfg_last_valid = CyFalse;
#if UVC_TEST_HOOKS
volatile uint8_t g_test_start_hook = 0;
#endif

uint8_t sensor_last_write_seq(void)
{
    return g_write_seq;
}

#define I2C_RETRY_GAP_US     1000u
#define I2C_RETRY_WINDOW_MS  5u
#define I2C_MAX_WR_CHUNK  15u
static CyU3PReturnStatus_t sensor_i2c_write_retry(uint8_t reg, uint8_t *data, uint8_t size)
{
    uint8_t done = 0;
    while (done < size)
    {
        uint8_t chunk = (uint8_t)((size - done > I2C_MAX_WR_CHUNK) ? I2C_MAX_WR_CHUNK
                                                                   : (size - done));
        uint8_t at = (uint8_t)(reg + done);
        CyU3PReturnStatus_t status = sensor_i2c_write(at, data + done, chunk);
        if (status != CY_U3P_SUCCESS)
        {
            uint32_t since = CyU3PGetTime();
            uint8_t  tries = 1;

            do {
                CyU3PBusyWait(I2C_RETRY_GAP_US);
                status = sensor_i2c_write(at, data + done, chunk);
                tries++;
            } while (status != CY_U3P_SUCCESS &&
                     (uint32_t)(CyU3PGetTime() - since) < I2C_RETRY_WINDOW_MS);
            if (status != CY_U3P_SUCCESS)
            {
                CyU3PDebugPrint(1, "[CFG] wr@0x%x refused %d times in %d ms, 0x%x\r\n",
                                at, tries, I2C_RETRY_WINDOW_MS, status);
                return status;
            }
            CyU3PDebugPrint(4, "[CFG] wr@0x%x taken on try %d\r\n", at, tries);
        }
        done += chunk;
    }
    return CY_U3P_SUCCESS;
}

static const uint8_t chc5_crc8_tab[256] = {
    0x00, 0x1D, 0x3A, 0x27, 0x74, 0x69, 0x4E, 0x53, 0xE8, 0xF5, 0xD2, 0xCF, 0x9C, 0x81, 0xA6, 0xBB,
    0xCD, 0xD0, 0xF7, 0xEA, 0xB9, 0xA4, 0x83, 0x9E, 0x25, 0x38, 0x1F, 0x02, 0x51, 0x4C, 0x6B, 0x76,
    0x87, 0x9A, 0xBD, 0xA0, 0xF3, 0xEE, 0xC9, 0xD4, 0x6F, 0x72, 0x55, 0x48, 0x1B, 0x06, 0x21, 0x3C,
    0x4A, 0x57, 0x70, 0x6D, 0x3E, 0x23, 0x04, 0x19, 0xA2, 0xBF, 0x98, 0x85, 0xD6, 0xCB, 0xEC, 0xF1,
    0x13, 0x0E, 0x29, 0x34, 0x67, 0x7A, 0x5D, 0x40, 0xFB, 0xE6, 0xC1, 0xDC, 0x8F, 0x92, 0xB5, 0xA8,
    0xDE, 0xC3, 0xE4, 0xF9, 0xAA, 0xB7, 0x90, 0x8D, 0x36, 0x2B, 0x0C, 0x11, 0x42, 0x5F, 0x78, 0x65,
    0x94, 0x89, 0xAE, 0xB3, 0xE0, 0xFD, 0xDA, 0xC7, 0x7C, 0x61, 0x46, 0x5B, 0x08, 0x15, 0x32, 0x2F,
    0x59, 0x44, 0x63, 0x7E, 0x2D, 0x30, 0x17, 0x0A, 0xB1, 0xAC, 0x8B, 0x96, 0xC5, 0xD8, 0xFF, 0xE2,
    0x26, 0x3B, 0x1C, 0x01, 0x52, 0x4F, 0x68, 0x75, 0xCE, 0xD3, 0xF4, 0xE9, 0xBA, 0xA7, 0x80, 0x9D,
    0xEB, 0xF6, 0xD1, 0xCC, 0x9F, 0x82, 0xA5, 0xB8, 0x03, 0x1E, 0x39, 0x24, 0x77, 0x6A, 0x4D, 0x50,
    0xA1, 0xBC, 0x9B, 0x86, 0xD5, 0xC8, 0xEF, 0xF2, 0x49, 0x54, 0x73, 0x6E, 0x3D, 0x20, 0x07, 0x1A,
    0x6C, 0x71, 0x56, 0x4B, 0x18, 0x05, 0x22, 0x3F, 0x84, 0x99, 0xBE, 0xA3, 0xF0, 0xED, 0xCA, 0xD7,
    0x35, 0x28, 0x0F, 0x12, 0x41, 0x5C, 0x7B, 0x66, 0xDD, 0xC0, 0xE7, 0xFA, 0xA9, 0xB4, 0x93, 0x8E,
    0xF8, 0xE5, 0xC2, 0xDF, 0x8C, 0x91, 0xB6, 0xAB, 0x10, 0x0D, 0x2A, 0x37, 0x64, 0x79, 0x5E, 0x43,
    0xB2, 0xAF, 0x88, 0x95, 0xC6, 0xDB, 0xFC, 0xE1, 0x5A, 0x47, 0x60, 0x7D, 0x2E, 0x33, 0x14, 0x09,
    0x7F, 0x62, 0x45, 0x58, 0x0B, 0x16, 0x31, 0x2C, 0x97, 0x8A, 0xAD, 0xB0, 0xE3, 0xFE, 0xD9, 0xC4,
};

uint8_t chc5_crc8(const uint8_t *blk, uint8_t skip)
{
    uint8_t c = 0xFF;
    uint8_t i;

    for (i = 0; i < 64; i++)
        if (i != skip)
            c = chc5_crc8_tab[c ^ blk[i]];
    return (uint8_t)(c ^ 0xFF);
}

CyU3PReturnStatus_t sensor_write_config(void)
{
    CyU3PReturnStatus_t status;
    uint8_t *cur  = (uint8_t *)sensor_cfg;
    uint8_t *prev = (uint8_t *)&sensor_cfg_last;
    uint16_t footer_off = (uint16_t)((uint8_t *)&sensor_cfg->crc8 - cur);
    uint16_t footer_len = (uint16_t)(sizeof(*sensor_cfg) - footer_off);
    int      first = -1, last = -1;
    uint16_t i;

    sensor_cfg->proto_ver   = CHC5_I2C_PROTO_VER;
    g_write_seq++;
    sensor_cfg->write_seq = g_write_seq;

    if (g_cfg_last_valid &&
        (sensor_cfg->stream_enable & STREAM_CMD_MASK) != STREAM_CMD_START)
    {
        for (i = 0; i < footer_off; i++)
            if (cur[i] != prev[i]) { if (first < 0) first = (int)i; last = (int)i; }
    }
    else { first = 0; last = (int)footer_off - 1; }

    CyU3PDebugPrint(4, "[CFG] s=%d se=%d span[%d..%d] %dx%d exp=%d g=%d af=0x%x\r\n",
        g_write_seq, sensor_cfg->stream_enable, first, last,
        sensor_cfg->width, sensor_cfg->height,
        (uint32_t)sensor_cfg->exposure_us, sensor_cfg->analog_gain,
        sensor_cfg->auto_flags);

    sensor_cfg->crc8 = chc5_crc8(cur, CHC5_CFG_CRC_OFF);

    if (first >= 0)
    {
        status = sensor_i2c_write_retry((uint8_t)(CHC5_DYNCFG_I2C_REG + first),
                                        cur + first, (uint8_t)(last - first + 1));
        if (status != CY_U3P_SUCCESS)
            return status;
    }

#if UVC_TEST_HOOKS
    if (g_test_start_hook && (sensor_cfg->stream_enable & STREAM_CMD_MASK) == STREAM_CMD_START)
    {
        uint8_t hook = g_test_start_hook;

        g_test_start_hook = 0;
        CyU3PDebugPrint(4, "[TEST] START seq %d: %s\r\n", g_write_seq,
                        (hook & TEST_HOOK_LOSE_START) ? "commit lost" : "reported failed");
        if (hook & TEST_HOOK_LOSE_START)
        {
            CyU3PMemCopy(prev, cur, sizeof(*sensor_cfg));
            g_cfg_last_valid = CyTrue;
            return CY_U3P_SUCCESS;
        }
        (void)sensor_i2c_write_retry((uint8_t)(CHC5_DYNCFG_I2C_REG + footer_off),
                                     cur + footer_off, (uint8_t)footer_len);
        return CY_U3P_ERROR_FAILURE;
    }
#endif
    status = sensor_i2c_write_retry((uint8_t)(CHC5_DYNCFG_I2C_REG + footer_off),
                                    cur + footer_off, (uint8_t)footer_len);
    if (status == CY_U3P_SUCCESS)
    {
        CyU3PMemCopy(prev, cur, sizeof(*sensor_cfg));
        g_cfg_last_valid = CyTrue;
    }
    return status;
}

uint8_t SensorI2cBusTest(void)
{
    return CY_U3P_SUCCESS;
}

void SensorInit(void)
{
    SensorI2cBusTest();
    sensor_cfg        = &sensor_cfg_buf;
    selected_img_mode = sensor_cfg;

    sensor_cfg->ae_metering_mode = 0xFF;
    sensor_cfg->ae_exp_lower_us  = 0xFFFFFFFFu;
    sensor_cfg->ae_exp_upper_us  = 0xFFFFFFFFu;
}

static CyBool_t g_streaming = CyFalse;

CyBool_t sensor_is_streaming(void)          { return g_streaming; }
void     sensor_set_streaming(CyBool_t a)   { g_streaming = a; }

CyBool_t sensor_stream_stop(void)
{
    if (!g_streaming)
        return CyFalse;
    sensor_cfg->stream_enable = STREAM_ACTIVE_FLAG | STREAM_CMD_STOP;
    if (sensor_write_config() != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint(2, "STOP write to PS failed, will retry\r\n");
        return CyFalse;
    }
    g_streaming = CyFalse;
    return CyTrue;
}

CyBool_t sensor_resend_stop(void)
{
    sensor_cfg->stream_enable = STREAM_ACTIVE_FLAG | STREAM_CMD_STOP;
    g_cfg_last_valid = CyFalse;
    return sensor_write_config() == CY_U3P_SUCCESS;
}

CyBool_t sensor_resend_start(CyBool_t restart)
{
    sensor_cfg->stream_enable = STREAM_ACTIVE_FLAG |
                                (restart ? STREAM_CMD_START : STREAM_CMD_NONE);
    g_cfg_last_valid = CyFalse;
    return sensor_write_config() == CY_U3P_SUCCESS;
}

static void sensor_set_binning_codes(uint8_t mode_binning)
{
    sensor_cfg->tp_bin = (uint8_t)((sensor_cfg->tp_bin & CHC5_TPBIN_TP_MASK) |
        (((mode_binning >> 4) & CHC5_TPBIN_BIN_MASK) << CHC5_TPBIN_BINX_SHIFT) |
        ((mode_binning & CHC5_TPBIN_BIN_MASK) << CHC5_TPBIN_BINY_SHIFT));
}

void sensor_build_config_from_modes(void)
{
    selected_img_mode = sensor_cfg;
    if (g_uvc_nmodes >= 1)
    {
        const uvc_mode_t *m = &g_uvc_modes[0];
        uint16_t fps = m->fps ? m->fps : 30;
        sensor_cfg->width        = m->width;
        sensor_cfg->height       = m->height;
        sensor_cfg->offset_x     = m->offset_x;
        sensor_cfg->offset_y     = m->offset_y;
        sensor_cfg->fps_mhz      = (uint32_t)fps * 1000u;
        sensor_set_binning_codes(m->binning);
        sensor_cfg->pixel_format = m->pixel_format;
        sensor_cfg->exposure_us  = 1000000u / fps;
        sensor_cfg->black_level  = 0;
    }
}

void sensor_handle_uvc_control(uint8_t frame_index, uint32_t interval)
{
    if (frame_index >= 1 && frame_index <= g_uvc_nmodes)
    {
        const uvc_mode_t *m = &g_uvc_modes[frame_index - 1];
        uint32_t mode_mhz = (m->fps ? m->fps : 30u) * 1000u;
        uint32_t fps_mhz  = mode_mhz;

        if (interval)
        {
            fps_mhz = (uint32_t)((10000000000ULL + interval / 2u) / interval);
            if (fps_mhz < 1000u)     fps_mhz = 1000u;
            if (fps_mhz > mode_mhz)  fps_mhz = mode_mhz;
        }
        sensor_cfg->width        = m->width;
        sensor_cfg->height       = m->height;
        sensor_cfg->offset_x     = m->offset_x;
        sensor_cfg->offset_y     = m->offset_y;
        sensor_set_binning_codes(m->binning);
        sensor_cfg->pixel_format = m->pixel_format;
        sensor_cfg->fps_mhz      = fps_mhz;
    }
}

void     sensor_set_analog_gain(uint16_t gain)   { sensor_cfg->analog_gain = gain; }
uint16_t sensor_get_analog_gain(void)            { return sensor_cfg->analog_gain; }
void     sensor_set_exposure(uint32_t us)        { sensor_cfg->exposure_us = us; }
uint32_t sensor_get_exposure(void)               { return sensor_cfg->exposure_us; }
void     sensor_set_test_pattern(uint8_t p)      { sensor_cfg->tp_bin = (uint8_t)((sensor_cfg->tp_bin & ~CHC5_TPBIN_TP_MASK) |
                                                                        (p & CHC5_TPBIN_TP_MASK)); }
uint8_t  sensor_get_test_pattern(void)           { return (uint8_t)(sensor_cfg->tp_bin & CHC5_TPBIN_TP_MASK); }
void     sensor_set_gamma_x100(uint16_t v)       { sensor_cfg->gamma_x100 = v; }
void     sensor_set_auto_flags(uint8_t f)        { sensor_cfg->auto_flags = f; }
uint8_t  sensor_get_auto_flags(void)             { return sensor_cfg->auto_flags; }
