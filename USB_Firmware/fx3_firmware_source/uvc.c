/*
 * uvc.c - UVC application: DMA/GPIF streaming and EP0 control handling
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */
#include <cyu3system.h>
#include <cyu3os.h>
#include <cyu3dma.h>
#include <cyu3error.h>
#include <cyu3usb.h>
#include <cyu3uart.h>
#include <cyu3gpif.h>
#include <cyu3i2c.h>
#include <cyu3gpio.h>
#include <cyu3pib.h>
#include <cyu3utils.h>
#include <cyu3socket.h>

#include "uvc.h"

#include "camera_ptzcontrol.h"
#include "cyfxgpif2config.h"
#include "uvc_settings.h"
#include "version.h"
#include "uvc_modes.h"
#include "led_status.h"
/*************************************************************************************************
                                         Global Variables
 *************************************************************************************************/
static CyU3PThread   uvcAppThread;                      /* UVC video streaming thread. */
static CyU3PThread   uvcAppEP0Thread;                   /* UVC control request handling thread. */
static CyU3PEvent    glFxUVCEvent;                      /* Event group used to signal threads. */
CyU3PDmaMultiChannel glChHandleUVCStream;               /* DMA multi-channel handle. */

/* Current UVC control request fields. See USB specification for definition. */
uint8_t  bmReqType, bRequest;                           /* bmReqType and bRequest fields. */
uint16_t wValue, wIndex, wLength;                       /* wValue, wIndex and wLength fields. */

CyU3PUSBSpeed_t usbSpeed = CY_U3P_NOT_CONNECTED;        /* Current USB connection speed. */

CyBool_t        glIsApplnActive  = CyFalse;
static CyBool_t glIsConfigured = CyFalse;               /* Whether Application is in configured state or not */

static volatile CyBool_t glPipeArmed = CyFalse;

static uint8_t glCommitBufferFailureCount = 0;

static volatile CyBool_t glLedHardStop = CyFalse;
#if LED_STATUS_ENABLED
static volatile CyBool_t glLedLinkDegraded = CyFalse;
#define LED_THREAD_STACK     (0x0400)
#define LED_THREAD_PRIORITY  (12)
static CyU3PThread glLedThread;
#endif

/*Variable to track whether the reason for DMA Reset is Frame Timer overflow or Commit Buffer Failure*/
static volatile uint8_t glDmaResetFlag = CY_FX_UVC_DMA_RESET_EVENT_NOT_ACTIVE;

static uint8_t glUvcVcErrorCode = CY_FX_UVC_VC_ERROR_CODE_NO_ERROR;

static volatile uint32_t glLastProdTimeMs = 0;

static CyBool_t glStopRetry   = CyFalse;
static uint32_t glStopRetryMs = 0;

static CyBool_t glStopAckPending = CyFalse;
static uint8_t  glStopAckSeq     = 0;
static uint32_t glStopAckSinceMs = 0;
static uint32_t glStopAckPollMs  = 0;
static uint32_t glStopAckSentMs  = 0;
static CyBool_t glStartHeld      = CyFalse;
static CyBool_t glStartAckPending   = CyFalse;
static CyBool_t glStartAckWriteFail = CyFalse;
static CyBool_t glStartAckRestart   = CyFalse;
static uint8_t  glStartAckSeq       = 0;
static uint32_t glStartAckSinceMs   = 0;
static uint32_t glStartAckPollMs    = 0;
static uint32_t glStartAckSentMs    = 0;

static CyBool_t glMuxSelValue  = CyTrue;
static CyBool_t glMuxRetryDone = CyFalse;
static uint32_t glMuxRetryMs   = 0;

#ifdef BACKFLOW_DETECT
uint8_t back_flow_detected = 0;                         /* Whether buffer overflow error is detected. */
#endif

#ifdef USB_DEBUG_INTERFACE
CyU3PDmaChannel  glDebugCmdChannel;                     /* Channel to receive debug commands on. */
CyU3PDmaChannel  glDebugRspChannel;                     /* Channel to send debug responses on. */
uint8_t         *glDebugRspBuffer;                      /* Buffer used to send debug responses. */
#endif

/* UVC Probe Control Settings for a USB 3.0 connection. */
uint8_t glProbeCtrl[CY_FX_UVC_MAX_PROBE_SETTING] = {
    0x00, 0x00,                 /* bmHint : no hit */
    0x01,                       /* Use 1st Video format index */
    0x01,                       /* Use 1st Video frame index */
    DBVAL(INTERVAL_30),     /* Desired frame interval in the unit of 100ns: 30 fps */
    0x00, 0x00,                 /* Key frame rate in key frame/video frame units: only applicable
                                   to video streaming with adjustable compression parameters */
    0x00, 0x00,                 /* PFrame rate in PFrame / key frame units: only applicable to
                                   video streaming with adjustable compression parameters */
    0x00, 0x00,                 /* Compression quality control: only applicable to video streaming
                                   with adjustable compression parameters */
    0x00, 0x00,                 /* Window size for average bit rate: only applicable to video
                                   streaming with adjustable compression parameters */
    0x00, 0x00,                 /* Internal video streaming i/f latency in ms */
    DBVAL(MAX_FRAME_SIZE),     /* Max video frame size in bytes */
    0x00, 0x80, 0x00, 0x00,

#ifndef FX3_UVC_1_0_SUPPORT
    /* UVC 1.1 Probe Control has additional fields from UVC 1.0 */
    0x00, 0x60, 0xE3, 0x16,             /* Device Clock */
    0x00,                               /* Framing Information - Ignored for uncompressed format*/
    0x00,                               /* Preferred payload format version */
    0x00,                               /* Minimum payload format version */
    0x00                                /* Maximum payload format version */
#endif
};

/* UVC Probe Control Setting for a USB 2.0 connection. */
uint8_t glProbeCtrl20[CY_FX_UVC_MAX_PROBE_SETTING] = {
    0x00, 0x00,                 /* bmHint : no hit */
    0x01,                       /* Use 1st Video format index */
    0x01,                       /* Use 1st Video frame index */
    DBVAL(INTERVAL_30),         /* Desired frame interval in the unit of 100ns: 30 fps */
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,                 /* Internal video streaming i/f latency in ms */
    DBVAL(MAX_MODE0_FRAME_SIZE),
    0x00, 0x80, 0x00, 0x00,    /* No. of bytes device can rx in single payload = 16 KB */

#ifndef FX3_UVC_1_0_SUPPORT
    /* UVC 1.1 Probe Control has additional fields from UVC 1.0 */
    0x00, 0x60, 0xE3, 0x16,             /* Device Clock */
    0x00,                               /* Framing Information - Ignored for uncompressed format*/
    0x00,                               /* Preferred payload format version */
    0x00,                               /* Minimum payload format version */
    0x00                                /* Maximum payload format version */
#endif
};

/* Video Probe Commit Control. This array is filled out when the host sends down the SET_CUR request. */
static uint8_t glCommitCtrl[CY_FX_UVC_MAX_PROBE_SETTING_ALIGNED];

static uint8_t glEp0Buffer[CY_FX_UVC_MAX_PROBE_SETTING_ALIGNED];

static CyU3PQueue       glSensorCmdQueue;
static uint32_t          glSensorCmdQueueBuf[SENSOR_CMD_QUEUE_SIZE];
static volatile uint32_t glPendingFrameInterval = 0;
static volatile uint32_t glPendingExposure = 0;
static uint8_t glAeMode = 0x02;
static uint8_t glWbAuto = 1;
static uint16_t glGammaX100 = UVC_GAMMA_DEF;

static uint8_t ae_auto_flags(void)
{
    uint8_t v = (glAeMode == 0x02) ? AUTO_CMD_ENABLE : AUTO_CMD_DISABLE;
    return (uint8_t)((v << AUTO_EXPOSURE_SHIFT) | (v << AUTO_GAIN_SHIFT));
}
static uint8_t wb_auto_flags(void)
{
    uint8_t v = glWbAuto ? AUTO_CMD_ENABLE : AUTO_CMD_DISABLE;
    return (uint8_t)(v << AUTO_WB_SHIFT);
}

static uint8_t all_auto_flags(void)
{
    return (uint8_t)(ae_auto_flags() | wb_auto_flags());
}

#define RB_CACHE_TTL_MS  3000u
static imgsensor_readback_t glRbCache;
static uint32_t glRbCacheMs   = 0;
static CyBool_t glRbAttempted = CyFalse;
static CyBool_t glRbValid     = CyFalse;
#define RB_FAST_TTL_MS      300u
#define RB_FAST_WINDOW_MS  RB_CACHE_TTL_MS
static uint32_t glRbTouchMs   = 0;
static CyBool_t glRbTouched   = CyFalse;

static void uvc_readback_touch(void)
{
    glRbTouchMs = CyU3PGetTime();
    glRbTouched = CyTrue;
}

static void uvc_refresh_readback(void)
{
    uint32_t now = CyU3PGetTime();
    uint32_t ttl = RB_CACHE_TTL_MS;
    if (glRbTouched)
    {
        if ((uint32_t)(now - glRbTouchMs) < RB_FAST_WINDOW_MS)
            ttl = RB_FAST_TTL_MS;
        else
            glRbTouched = CyFalse;
    }
    if (!glRbAttempted || (uint32_t)(now - glRbCacheMs) >= ttl)
    {
        imgsensor_readback_t rb;
        glRbAttempted = CyTrue;
        glRbCacheMs   = now;
        if (sensor_i2c_read(CHC5_READBACK_I2C_REG, (uint8_t *)&rb, sizeof(rb)) == CY_U3P_SUCCESS
            && rb.rb_seq == rb.rb_seq_echo
            && rb.exposure_us != 0xFFFFFFFFu)
        {
            glRbCache = rb;
            glRbValid = CyTrue;
        }
    }
}

static uint32_t uvc_live_exposure(void)
{
    uvc_refresh_readback();
    if (glRbValid && (glRbCache.ae_status & RB_AE_STATUS_EXP_ACTIVE))
        return glRbCache.exposure_us;
    return 0;
}
static uint16_t uvc_live_gain(void)
{
    uvc_refresh_readback();
    if (glRbValid && (glRbCache.ae_status & RB_AE_STATUS_GAIN_ACTIVE))
        return glRbCache.analog_gain;
    return 0;
}

static void uvc_probe_fill(uint8_t *ctrl, uint8_t fi, uint32_t ival, uint32_t fsz)
{
    ctrl[2]  = 1;
    ctrl[3]  = fi;
    ctrl[4]  = (uint8_t)(ival);       ctrl[5]  = (uint8_t)(ival >> 8);
    ctrl[6]  = (uint8_t)(ival >> 16); ctrl[7]  = (uint8_t)(ival >> 24);
    ctrl[18] = (uint8_t)(fsz);        ctrl[19] = (uint8_t)(fsz >> 8);
    ctrl[20] = (uint8_t)(fsz >> 16);  ctrl[21] = (uint8_t)(fsz >> 24);
    ctrl[22] = (uint8_t)(CY_FX_UVC_STREAM_BUF_SIZE);
    ctrl[23] = (uint8_t)(CY_FX_UVC_STREAM_BUF_SIZE >> 8);
    ctrl[24] = (uint8_t)(CY_FX_UVC_STREAM_BUF_SIZE >> 16);
    ctrl[25] = (uint8_t)(CY_FX_UVC_STREAM_BUF_SIZE >> 24);
}

static uint8_t uvc_negotiate(uint8_t *ctrl)
{
    uint8_t  n    = (usbSpeed == CY_U3P_SUPER_SPEED) ? g_uvc_nmodes : 1u;
    uint8_t  fi   = ctrl[3];
    uint32_t want = (uint32_t)ctrl[4] | ((uint32_t)ctrl[5] << 8) |
                    ((uint32_t)ctrl[6] << 16) | ((uint32_t)ctrl[7] << 24);
    uint32_t fps, ival;
    const uvc_mode_t *m;

    if (n < 1u)  n  = 1u;
    if (fi < 1u) fi = 1u;
    if (fi > n)  fi = n;
    m   = &g_uvc_modes[fi - 1];
    fps = (usbSpeed == CY_U3P_SUPER_SPEED) ? m->fps : g_uvc_hs_fps;
    if (fps == 0u)
        fps = 30u;
    ival = 10000000u / fps;
    if (want >= ival && want <= 10000000u)
        ival = want;

    uvc_probe_fill(ctrl, fi, ival, (uint32_t)m->width * m->height * 2u);
    return fi;
}

static void uvc_probe_init(void)
{
    uint32_t fsz = (uint32_t)g_uvc_modes[0].width * g_uvc_modes[0].height * 2u;
    uint32_t fps = g_uvc_modes[0].fps ? g_uvc_modes[0].fps : 30u;

    uvc_probe_fill(glProbeCtrl, 1, 10000000u / fps, fsz);
    fps = g_uvc_hs_fps ? g_uvc_hs_fps : 30u;
    uvc_probe_fill(glProbeCtrl20, 1, 10000000u / fps, fsz);
}

uint8_t volatile glUVCHeader[CY_FX_UVC_MAX_HEADER] =
{
    0x0C,                               /* Header Length */
    0x80,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

#ifdef DEBUG_PRINT_FRAME_COUNT
volatile static uint32_t glFrameCount = 0;              /* Number of video frames transferred so far. */
volatile static uint32_t glDmaDone = 1;                 /* Number of buffers transferred in the current frame. */
#endif

/* Add the UVC packet header to the top of the specified DMA buffer. */
void
CyFxUVCAddHeader (
        uint8_t *buffer_p,              /* Buffer pointer */
        uint8_t frameInd                /* EOF or normal frame indication */
        )
{
    /* Copy header to buffer */
    CyU3PMemCopy (buffer_p, (uint8_t *)glUVCHeader, CY_FX_UVC_MAX_HEADER);

    /* The EOF flag needs to be set if this is the last packet for this video frame. */
    if (frameInd & CY_FX_UVC_HEADER_EOF)
    {
        buffer_p[1] |= CY_FX_UVC_HEADER_EOF;
        glUVCHeader[1] ^= CY_FX_UVC_HEADER_FRAME_ID;
    }
}

void
CyFxAppErrorHandler (
        CyU3PReturnStatus_t apiRetStatus    /* API return status */
        )
{
#if LED_STATUS_ENABLED
    led_status_fatal();
#endif
    CyU3PDebugPrint (1, "FATAL ERROR: 0x%x\r\n", apiRetStatus);
    for (;;)
    {
        CyU3PThreadSleep (1000);
    }
}

static void
CyFxUVCApplnAbortHandler (
        void)
{
    uint32_t cmd = SENSOR_CMD_ABORT;

    if (CyU3PQueueSend (&glSensorCmdQueue, &cmd, CYU3P_NO_WAIT) == CY_U3P_SUCCESS)
        CyU3PEventSet (&glFxUVCEvent, CY_FX_UVC_SENSOR_WRITE_EVENT, CYU3P_EVENT_OR);
    else
        CyU3PEventSet (&glFxUVCEvent, CY_FX_UVC_STREAM_ABORT_EVENT, CYU3P_EVENT_OR);
}

/* This is the Callback function to handle the USB Events */
static void
CyFxUVCApplnUSBEventCB (
        CyU3PUsbEventType_t evtype,  /* Event type */
        uint16_t             evdata  /* Event data */
        )
{
    (void)evdata;
    switch (evtype)
    {
        case CY_U3P_USB_EVENT_SUSPEND:
            CyU3PDebugPrint (4, "UsbEventCB: SUSPEND encountered...\r\n");
            if (glIsApplnActive || glPipeArmed)
                CyFxUVCApplnAbortHandler ();
            CyU3PEventSet (&glFxUVCEvent, CY_FX_USB_SUSPEND_EVENT_HANDLER, CYU3P_EVENT_OR);
            break;

        case CY_U3P_USB_EVENT_EP_UNDERRUN:
            CyU3PDebugPrint (4, "UsbEventCB: CY_U3P_USB_EVENT_EP_UNDERRUN encountered...\r\n");
            break;

        case CY_U3P_USB_EVENT_SETCONF:
            if ((CyU3PUsbGetSpeed () != CY_U3P_SUPER_SPEED) && !glMuxRetryDone)
            {
                if (glIsApplnActive || glPipeArmed)
                    CyFxUVCApplnAbortHandler ();
                glMuxRetryDone = CyTrue;
                glMuxRetryMs   = CyU3PGetTime ();
                CyU3PConnectState (CyFalse, CyTrue);
                CyU3PThreadSleep (200);
                glMuxSelValue = !glMuxSelValue;
                CyU3PGpioSetValue (FX3_MUXSEL_GPIO, glMuxSelValue);
                CyU3PThreadSleep (100);
                CyU3PConnectState (CyTrue, CyTrue);
                break;
            }
            CyU3PDebugPrint (4, "UsbEventCB: SETCONF speed=%d muxsel=%d\r\n",
                             CyU3PUsbGetSpeed (), glMuxSelValue);
            glIsConfigured = CyTrue;
            glLedHardStop  = CyFalse;
#if LED_STATUS_ENABLED
            glLedLinkDegraded = (CyU3PUsbGetSpeed () != CY_U3P_SUPER_SPEED);
            led_status_poke();
#endif
            CyU3PUsbLPMEnable ();
            if (glIsApplnActive || glPipeArmed)
                CyFxUVCApplnAbortHandler ();
            break;

        case CY_U3P_USB_EVENT_RESET:
        case CY_U3P_USB_EVENT_DISCONNECT:
        case CY_U3P_USB_EVENT_CONNECT:
            glIsConfigured = CyFalse;
            glLedHardStop  = CyFalse;
            if ((evtype == CY_U3P_USB_EVENT_DISCONNECT) &&
                (!glMuxRetryDone || (uint32_t)(CyU3PGetTime () - glMuxRetryMs) > 1000u))
            {
                glMuxRetryDone = CyFalse;
                glMuxSelValue  = CyTrue;
                CyU3PGpioSetValue (FX3_MUXSEL_GPIO, glMuxSelValue);
            }
#if LED_STATUS_ENABLED
            if (evtype == CY_U3P_USB_EVENT_DISCONNECT)
                glLedLinkDegraded = CyFalse;
            led_status_poke();
#endif

            /* Stop the video streamer application and enable LPM. */
            CyU3PUsbLPMEnable ();
            if (glIsApplnActive || glPipeArmed)
            {
                CyU3PDebugPrint (4, "UsbEventCB: Call App Stop\r\n");
                CyFxUVCApplnAbortHandler ();
            }
            break;

        default:
            break;
    }
}

/* Callback to handle the USB Setup Requests and UVC Class events */
static CyBool_t
CyFxUVCApplnUSBSetupCB (
        uint32_t setupdat0, /* SETUP Data 0 */
        uint32_t setupdat1  /* SETUP Data 1 */
        )
{
    CyBool_t uvcHandleReq = CyFalse;
    uint32_t status;

    /* Obtain Request Type and Request */
    bmReqType = (uint8_t)(setupdat0 & CY_FX_USB_SETUP_REQ_TYPE_MASK);
    bRequest  = (uint8_t)((setupdat0 & CY_FX_USB_SETUP_REQ_MASK) >> 8);
    wValue    = (uint16_t)((setupdat0 & CY_FX_USB_SETUP_VALUE_MASK) >> 16);
    wIndex    = (uint16_t)(setupdat1 & CY_FX_USB_SETUP_INDEX_MASK);
    wLength   = (uint16_t)((setupdat1 & CY_FX_USB_SETUP_LENGTH_MASK) >> 16);

    /* Check for UVC Class Requests */
    switch (bmReqType)
    {
        case CY_FX_USB_UVC_GET_REQ_TYPE:
        case CY_FX_USB_UVC_SET_REQ_TYPE:
            /* UVC Specific requests are handled in the EP0 thread. */
            switch (wIndex & 0xFF)
            {
                case CY_FX_UVC_CONTROL_INTERFACE:
                    {
                        uvcHandleReq = CyTrue;
                        status = CyU3PEventSet (&glFxUVCEvent, CY_FX_UVC_VIDEO_CONTROL_REQUEST_EVENT, CYU3P_EVENT_OR);
                        if (status != CY_U3P_SUCCESS)
                        {
                            CyU3PDebugPrint (4, "Set CY_FX_UVC_VIDEO_CONTROL_REQUEST_EVENT Failed %x\r\n", status);
                            CyU3PUsbStall (0, CyTrue, CyFalse);
                        }
                    }
                    break;

                case CY_FX_UVC_STREAM_INTERFACE:
                    {
                        uvcHandleReq = CyTrue;
                        status = CyU3PEventSet (&glFxUVCEvent, CY_FX_UVC_VIDEO_STREAM_REQUEST_EVENT, CYU3P_EVENT_OR);
                        if (status != CY_U3P_SUCCESS)
                        {
                            /* Error handling */
                            CyU3PDebugPrint (4, "Set CY_FX_UVC_VIDEO_STREAM_REQUEST_EVENT Failed %x\r\n", status);
                            CyU3PUsbStall (0, CyTrue, CyFalse);
                        }
                    }
                    break;

                default:
                    break;
            }
            break;

        case CY_FX_USB_SET_INTF_REQ_TYPE:
            if (bRequest == CY_FX_USB_SET_INTERFACE_REQ)
            {
                /* Some hosts send Set Interface Alternate Setting 0 command while stopping the video
                 * stream. The application uses this event to stop streaming. */
                if ((wIndex == CY_FX_UVC_STREAM_INTERFACE) && (wValue == 0))
                {
                    /* Stop GPIF state machine to stop data transfers through FX3 */
                    CyU3PDebugPrint (4, "Alternate setting 0..\r\n");

                    /* Clear the stall condition and sequence numbers. */
                    CyU3PUsbStall (CY_FX_EP_BULK_VIDEO, CyFalse, CyTrue);
                    CyFxUVCApplnAbortHandler ();

                    uvcHandleReq = CyTrue;
                    /* Complete Control request handshake */
                    CyU3PUsbAckSetup ();
                }
            }
            else if ((bRequest == CY_U3P_USB_SC_SET_FEATURE) || (bRequest == CY_U3P_USB_SC_CLEAR_FEATURE))
            {
                CyU3PDebugPrint(4, "USBSetupCB:In SET_FTR %d::%d\r\n", glIsApplnActive, glIsConfigured);
                if (glIsConfigured)
                {
                    uvcHandleReq = CyTrue;
                    CyU3PUsbAckSetup ();
                }
            }

            break;

        case CY_U3P_USB_TARGET_ENDPT:
            if (bRequest == CY_U3P_USB_SC_CLEAR_FEATURE)
            {
                if (wIndex == CY_FX_EP_BULK_VIDEO)
                {
                    /* Windows OS sends Clear Feature Request after it stops streaming,
                     * however MAC OS sends clear feature request right after it sends a
                     * Commit -> SET_CUR request. Hence, stop the video streaming and clear
                     * the stall condition and sequence numbers */
                    CyU3PDebugPrint (4, "Clear feature request detected...\r\n");

                    /* Clear the stall condition and sequence numbers. */
                    CyU3PUsbStall (CY_FX_EP_BULK_VIDEO, CyFalse, CyTrue);
                    CyFxUVCApplnAbortHandler();

                    uvcHandleReq = CyTrue;
                    /* Complete Control request handshake */
                    CyU3PUsbAckSetup ();
                }
            }
            break;

        default:
            break;
    }

    /* Return status of request handling to the USB driver */
    return uvcHandleReq;
}

void
CyFxUvcApplnDmaCallback (
        CyU3PDmaMultiChannel *chHandle,
        CyU3PDmaCbType_t      type,
        CyU3PDmaCBInput_t    *input
        )
{
    CyU3PDmaBuffer_t    dmaBuffer;
    CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

    (void)input;
    if (type != CY_U3P_DMA_CB_PROD_EVENT)
        return;

    glLastProdTimeMs = CyU3PGetTime ();

    /* There is a possibility that CyU3PDmaMultiChannelGetBuffer will return CY_U3P_ERROR_INVALID_SEQUENCE here.
     * In such a case, do nothing. We make up for this missed produce event by making repeated commit actions
     * in subsequent produce event callbacks.
     */
    status = CyU3PDmaMultiChannelGetBuffer (chHandle, &dmaBuffer, CYU3P_NO_WAIT);
    while (status == CY_U3P_SUCCESS)
    {
        CyBool_t eof = (dmaBuffer.count != CY_FX_UVC_BUF_FULL_SIZE) ? CyTrue : CyFalse;

        CyFxUVCAddHeader (dmaBuffer.buffer - CY_FX_UVC_MAX_HEADER,
                          eof ? CY_FX_UVC_HEADER_EOF : CY_FX_UVC_HEADER_FRAME);

        /* Commit Buffer to USB*/
        status = CyU3PDmaMultiChannelCommitBuffer (chHandle, (dmaBuffer.count + CY_FX_UVC_MAX_HEADER), 0);
        if (status != CY_U3P_SUCCESS)
        {
            if (glDmaResetFlag == CY_FX_UVC_DMA_RESET_EVENT_NOT_ACTIVE)
            {
                glDmaResetFlag = CY_FX_UVC_DMA_RESET_COMMIT_BUFFER_FAILURE;
                CyU3PEventSet (&glFxUVCEvent, CY_FX_UVC_DMA_RESET_EVENT, CYU3P_EVENT_OR);
            }
            break;
        }

#ifdef DEBUG_PRINT_FRAME_COUNT
        glDmaDone++;
#endif
        if (eof)
        {
#ifdef DEBUG_PRINT_FRAME_COUNT
            glFrameCount++;
#endif
            CyU3PEventSet (&glFxUVCEvent, CY_FX_UVC_FRAME_DONE_EVENT, CYU3P_EVENT_OR);
        }

        /* Check if any more buffers are ready to go, and commit them here. */
        status = CyU3PDmaMultiChannelGetBuffer (chHandle, &dmaBuffer, CYU3P_NO_WAIT);
    }
}

/* GpifCB callback function is invoked when FV triggers GPIF interrupt */
void
CyFxGpifCB (
        uint8_t currentState            /* GPIF state which triggered the interrupt. */
        )
{
    switch (currentState)
    {
        case PARTIAL_BUF_IN_SCK0:
            CyU3PDmaSocketSetWrapUp (CY_U3P_PIB_SOCKET_0);
            break;
        case FULL_BUF_IN_SCK0:
            break;
        case PARTIAL_BUF_IN_SCK1:
            CyU3PDmaSocketSetWrapUp (CY_U3P_PIB_SOCKET_1);
            break;
        case FULL_BUF_IN_SCK1:
            break;

        default:
            /* This should not happen. Do nothing. */
            return;
    }

    CyU3PGpifControlSWInput (CyTrue);
    CyU3PGpifControlSWInput (CyFalse);
}

/* This function initializes the Debug Module for the UVC Application */
static void
CyFxUVCApplnDebugInit (
        void)
{
    CyU3PUartConfig_t uartConfig;
    CyU3PReturnStatus_t apiRetStatus;

    /* Initialize the UART for printing debug messages */
    apiRetStatus = CyU3PUartInit ();
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "UART initialization failed!\n");
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* Set UART Configuration */
    uartConfig.baudRate = CY_U3P_UART_BAUDRATE_115200;
    uartConfig.stopBit  = CY_U3P_UART_ONE_STOP_BIT;
    uartConfig.parity   = CY_U3P_UART_NO_PARITY;
    uartConfig.txEnable = CyTrue;
    uartConfig.rxEnable = CyFalse;
    uartConfig.flowCtrl = CyFalse;
    uartConfig.isDma    = CyTrue;

    /* Set the UART configuration */
    apiRetStatus = CyU3PUartSetConfig (&uartConfig, NULL);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* Set the UART transfer */
    apiRetStatus = CyU3PUartTxSetBlockXfer (0xFFFFFFFF);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* Initialize the Debug logger module. */
    apiRetStatus = CyU3PDebugInit (CY_U3P_LPP_SOCKET_UART_CONS, UVC_DEBUG_LEVEL);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* Disable log message headers. */
    CyU3PDebugPreamble (CyFalse);
}

/* I2C initialization. */
static void
CyFxUVCApplnI2CInit (void)
{
    CyU3PI2cConfig_t i2cConfig;;
    CyU3PReturnStatus_t status;

    status = CyU3PI2cInit ();
    if (status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "I2C initialization failed!\n");
        CyFxAppErrorHandler (status);
    }

    /*  Set I2C Configuration */
    i2cConfig.bitRate    = 100000;      /*  100 KHz */
    i2cConfig.isDma      = CyFalse;
    i2cConfig.busTimeout = 0x00100000;
    i2cConfig.dmaTimeout = 0xffff;

    status = CyU3PI2cSetConfig (&i2cConfig, 0);
    if (CY_U3P_SUCCESS != status)
    {
        CyU3PDebugPrint (4, "I2C configuration failed!\n");
        CyFxAppErrorHandler (status);
    }

    sensor_i2c_lock_init ();
}

#ifdef BACKFLOW_DETECT
static void CyFxUvcAppPibCallback (
        CyU3PPibIntrType cbType,
        uint16_t cbArg)
{
    if ((cbType == CYU3P_PIB_INTR_ERROR) && ((cbArg == 0x1005) || (cbArg == 0x1006)))
    {
        if (!back_flow_detected)
        {
            CyU3PDebugPrint (4, "Backflow detected...\r\n");
            back_flow_detected = 1;
        }
    }
}
#endif

#ifdef USB_DEBUG_INTERFACE
static void
CyFxUvcAppDebugCallback (
        CyU3PDmaChannel   *handle,
        CyU3PDmaCbType_t   type,
        CyU3PDmaCBInput_t *input)
{
    if (type == CY_U3P_DMA_CB_PROD_EVENT)
    {
        /* Data has been received. Notify the EP0 thread which handles the debug commands as well. */
        CyU3PEventSet (&glFxUVCEvent, CY_FX_USB_DEBUG_CMD_EVENT, CYU3P_EVENT_OR);
    }
}
#endif

/*
 * Load the GPIF configuration on the GPIF-II engine. This operation is performed at start-up.
 */
static void
CyFxUvcAppGpifInit (
        void)
{
    CyU3PReturnStatus_t apiRetStatus;

    apiRetStatus =  CyU3PGpifLoad ((CyU3PGpifConfig_t *) &CyFxGpifConfig);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        /* Error Handling */
        CyU3PDebugPrint (4, "Loading GPIF Configuration failed, Error Code = %d\r\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }
}

/* Callback for LPM requests. Always return true to allow host to transition device
 * into required LPM state U1/U2/U3. When data transmission is active LPM management
 * is explicitly disabled to prevent data transmission errors.
 */
static CyBool_t
CyFxUVCAppLPMRqtCB (
        CyU3PUsbLinkPowerMode link_mode         /*USB 3.0 linkmode requested by Host */
        )
{
    return CyTrue;
}

/* This function initializes the USB Module, creates event group,
   sets the enumeration descriptors, configures the Endpoints and
   configures the DMA module for the UVC Application */
static void
CyFxUVCApplnInit (void)
{
    CyU3PDmaMultiChannelConfig_t dmaMultiConfig;
    CyU3PEpConfig_t              endPointConfig;
    CyU3PReturnStatus_t          apiRetStatus;
    CyU3PGpioClock_t             gpioClock;
    CyU3PGpioSimpleConfig_t      gpioConfig;
    CyU3PPibClock_t              pibclock;

#ifdef USB_DEBUG_INTERFACE
    CyU3PDmaChannelConfig_t channelConfig;
#endif

    /* Create UVC event group */
    apiRetStatus = CyU3PEventCreate (&glFxUVCEvent);
    if (apiRetStatus != 0)
    {
        CyU3PDebugPrint (4, "UVC Create Event failed, Error Code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    apiRetStatus = CyU3PQueueCreate (&glSensorCmdQueue,
            1,
            &glSensorCmdQueueBuf[0],
            sizeof(glSensorCmdQueueBuf));
    if (apiRetStatus != 0)
    {
        CyU3PDebugPrint (4, "Sensor Cmd Queue create failed, Error Code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

#ifdef UVC_PTZ_SUPPORT
    CyFxUvcAppPTZInit ();
#endif

    /* Init the GPIO module */
    gpioClock.fastClkDiv = 2;
    gpioClock.slowClkDiv = 2;
    gpioClock.simpleDiv  = CY_U3P_GPIO_SIMPLE_DIV_BY_2;
    gpioClock.clkSrc     = CY_U3P_SYS_CLK;
    gpioClock.halfDiv    = 0;

    /* Initialize Gpio interface */
    apiRetStatus = CyU3PGpioInit (&gpioClock, NULL);
    if (apiRetStatus != 0)
    {
        CyU3PDebugPrint (4, "GPIO Init failed, Error Code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

#if LED_STATUS_ENABLED
    led_status_init();
#endif

    apiRetStatus = CyU3PDeviceGpioOverride (DEBUG1_GPIO, CyTrue);
    if (apiRetStatus != 0)
    {
        CyU3PDebugPrint (4, "GPIO Override failed, Error Code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    gpioConfig.outValue    = CyTrue;
    gpioConfig.driveLowEn  = CyTrue;
    gpioConfig.driveHighEn = CyTrue;
    gpioConfig.inputEn     = CyFalse;
    gpioConfig.intrMode    = CY_U3P_GPIO_NO_INTR;
    apiRetStatus           = CyU3PGpioSetSimpleConfig (DEBUG1_GPIO, &gpioConfig);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "GPIO Set Config Error, Error Code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    CyU3PGpioSetValue (FX3_MUXSEL_GPIO, glMuxSelValue);
    apiRetStatus = CyU3PDeviceGpioOverride (FX3_MUXSEL_GPIO, CyTrue);
    if (apiRetStatus != 0)
    {
        CyU3PDebugPrint (4, "GPIO Override failed, Error Code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    gpioConfig.outValue    = glMuxSelValue;
    gpioConfig.driveLowEn  = CyTrue;
    gpioConfig.driveHighEn = CyTrue;
    gpioConfig.inputEn     = CyFalse;
    gpioConfig.intrMode    = CY_U3P_GPIO_NO_INTR;
    apiRetStatus           = CyU3PGpioSetSimpleConfig (FX3_MUXSEL_GPIO, &gpioConfig);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "GPIO Set Config Error, Error Code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* Initialize the P-port. */
    pibclock.clkDiv      = 2;
    pibclock.clkSrc      = CY_U3P_SYS_CLK;
    pibclock.isDllEnable = CyFalse;
    pibclock.isHalfDiv   = CyFalse;

    apiRetStatus = CyU3PPibInit (CyTrue, &pibclock);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "PIB Function Failed to Start, Error Code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    CyFxUvcAppGpifInit ();

    /* Register the GPIF State Machine callback used to get frame end notifications.
     * We use the fast callback version which is triggered from ISR context.
     */
    CyU3PGpifRegisterSMIntrCallback (CyFxGpifCB);

#ifdef BACKFLOW_DETECT
    back_flow_detected = 0;
    CyU3PPibRegisterCallback (CyFxUvcAppPibCallback, CYU3P_PIB_INTR_ERROR);
#endif

    CyU3PThreadSleep(100);
    SensorInit ();

    CyFxUvcLoadModesAndBuild ();
    sensor_build_config_from_modes ();
    CyFxUvcApplyIdentity ();
    uvc_probe_init ();

    /* USB initialization. */
    apiRetStatus = CyU3PUsbStart ();
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB Function Failed to Start, Error Code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* Setup the Callback to Handle the USB Setup Requests */
    CyU3PUsbRegisterSetupCallback (CyFxUVCApplnUSBSetupCB, CyFalse);

    /* Setup the Callback to Handle the USB Events */
    CyU3PUsbRegisterEventCallback (CyFxUVCApplnUSBEventCB);

    /* Register a callback to handle LPM requests from the USB 3.0 host. */
    CyU3PUsbRegisterLPMRequestCallback (CyFxUVCAppLPMRqtCB);

    /* Register the USB device descriptors with the driver. */
    CyU3PUsbSetDesc (CY_U3P_USB_SET_HS_DEVICE_DESCR, 0, (uint8_t *)CyFxUSBDeviceDscr);
    CyU3PUsbSetDesc (CY_U3P_USB_SET_SS_DEVICE_DESCR, 0, (uint8_t *)CyFxUSBDeviceDscrSS);

    /* BOS and Device qualifier descriptors. */
    CyU3PUsbSetDesc (CY_U3P_USB_SET_DEVQUAL_DESCR, 0, (uint8_t *)CyFxUSBDeviceQualDscr);
    CyU3PUsbSetDesc (CY_U3P_USB_SET_SS_BOS_DESCR, 0, (uint8_t *)CyFxUSBBOSDscr);

    /* Configuration descriptors. */
    CyU3PUsbSetDesc (CY_U3P_USB_SET_HS_CONFIG_DESCR, 0, CyFxUvcGetHSConfigDscr ());
    CyU3PUsbSetDesc (CY_U3P_USB_SET_FS_CONFIG_DESCR, 0, (uint8_t *)CyFxUSBFSConfigDscr);
    CyU3PUsbSetDesc (CY_U3P_USB_SET_SS_CONFIG_DESCR, 0, CyFxUvcGetSSConfigDscr ());

    /* String Descriptors */
    CyU3PUsbSetDesc (CY_U3P_USB_SET_STRING_DESCR, 0, (uint8_t *)CyFxUSBStringLangIDDscr);
    CyU3PUsbSetDesc (CY_U3P_USB_SET_STRING_DESCR, 1, (uint8_t *)CyFxUSBManufactureDscr);
    CyU3PUsbSetDesc (CY_U3P_USB_SET_STRING_DESCR, 2, (uint8_t *)CyFxUSBProductDscr);
    CyU3PUsbSetDesc (CY_U3P_USB_SET_STRING_DESCR, 3, CyFxUSBSerialDscr);

    /* Configure the video streaming endpoint. */
    endPointConfig.enable   = 1;
    endPointConfig.epType   = CY_U3P_USB_EP_BULK;
    endPointConfig.pcktSize = CY_FX_EP_BULK_VIDEO_PKT_SIZE;
    endPointConfig.isoPkts  = 1;
    endPointConfig.burstLen = 16;
    endPointConfig.streams  = 0;
    apiRetStatus = CyU3PSetEpConfig (CY_FX_EP_BULK_VIDEO, &endPointConfig);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        /* Error Handling */
        CyU3PDebugPrint (4, "USB Set Endpoint config failed, Error Code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* Configure the status interrupt endpoint.
       Note: This endpoint is not being used by the application as of now. This can be used in case
       UVC device needs to notify the host about any error conditions. A MANUAL_OUT DMA channel
       can be associated with this endpoint and used to send these data packets.
     */
    endPointConfig.enable   = 1;
    endPointConfig.epType   = CY_U3P_USB_EP_INTR;
    endPointConfig.pcktSize = 64;
    endPointConfig.isoPkts  = 0;
    endPointConfig.streams  = 0;
    endPointConfig.burstLen = 1;
    apiRetStatus = CyU3PSetEpConfig (CY_FX_EP_CONTROL_STATUS, &endPointConfig);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        /* Error Handling */
        CyU3PDebugPrint (4, "USB Set Endpoint config failed, Error Code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* Create a DMA Manual channel for sending the video data to the USB host. */
    CyU3PMemSet ((uint8_t *)&dmaMultiConfig, 0, sizeof (dmaMultiConfig));
    dmaMultiConfig.size           = CY_FX_UVC_STREAM_BUF_SIZE;
    dmaMultiConfig.count          = CY_FX_UVC_STREAM_BUF_COUNT;
    dmaMultiConfig.validSckCount  = 2;
    dmaMultiConfig.prodSckId [0]  = (CyU3PDmaSocketId_t)CY_U3P_PIB_SOCKET_0;
    dmaMultiConfig.prodSckId [1]  = (CyU3PDmaSocketId_t)CY_U3P_PIB_SOCKET_1;
    dmaMultiConfig.consSckId [0]  = (CyU3PDmaSocketId_t)(CY_U3P_UIB_SOCKET_CONS_0 | CY_FX_EP_VIDEO_CONS_SOCKET);
    dmaMultiConfig.prodAvailCount = 0;
    dmaMultiConfig.prodHeader     = 12;                 /* 12 byte UVC header to be added. */
    dmaMultiConfig.prodFooter     = 4;                  /* 4 byte footer to compensate for the 12 byte header. */
    dmaMultiConfig.consHeader     = 0;
    dmaMultiConfig.dmaMode        = CY_U3P_DMA_MODE_BYTE;
    dmaMultiConfig.notification   = CY_U3P_DMA_CB_PROD_EVENT;
    dmaMultiConfig.cb             = CyFxUvcApplnDmaCallback;
    apiRetStatus = CyU3PDmaMultiChannelCreate (&glChHandleUVCStream, CY_U3P_DMA_TYPE_MANUAL_MANY_TO_ONE,
            &dmaMultiConfig);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        /* Error handling */
        CyU3PDebugPrint (4, "DMA Channel Creation Failed, Error Code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

#ifdef USB_DEBUG_INTERFACE
    /* Configure the endpoints and create DMA channels used by the USB debug interface.
       The command (OUT) endpoint is configured in packet mode and enabled to receive data.
       Once the CY_U3P_DMA_CB_PROD_EVENT callback is received, the received data packet is
       processed and the data is returned through the CyU3PDmaChannelSetupSendBuffer API call.
     */

    endPointConfig.enable   = 1;
    endPointConfig.epType   = CY_U3P_USB_EP_BULK;
    endPointConfig.pcktSize = 1024;                     /* Use SuperSpeed settings here. */
    endPointConfig.isoPkts  = 0;
    endPointConfig.streams  = 0;
    endPointConfig.burstLen = 1;

    apiRetStatus = CyU3PSetEpConfig (CY_FX_EP_DEBUG_CMD, &endPointConfig);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "Debug Command endpoint config failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    CyU3PUsbSetEpPktMode (CY_FX_EP_DEBUG_CMD, CyTrue);

    apiRetStatus = CyU3PSetEpConfig (CY_FX_EP_DEBUG_RSP, &endPointConfig);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "Debug Response endpoint config failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    channelConfig.size           = 1024;
    channelConfig.count          = 1;
    channelConfig.prodSckId      = CY_U3P_UIB_SOCKET_PROD_0 | CY_FX_EP_DEBUG_CMD_SOCKET;
    channelConfig.consSckId      = CY_U3P_CPU_SOCKET_CONS;
    channelConfig.prodAvailCount = 0;
    channelConfig.prodHeader     = 0;
    channelConfig.prodFooter     = 0;
    channelConfig.consHeader     = 0;
    channelConfig.dmaMode        = CY_U3P_DMA_MODE_BYTE;
    channelConfig.notification   = CY_U3P_DMA_CB_PROD_EVENT;
    channelConfig.cb             = CyFxUvcAppDebugCallback;

    apiRetStatus = CyU3PDmaChannelCreate (&glDebugCmdChannel, CY_U3P_DMA_TYPE_MANUAL_IN, &channelConfig);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "Debug Command channel create failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    apiRetStatus = CyU3PDmaChannelSetXfer (&glDebugCmdChannel, 0);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "Debug channel SetXfer failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    channelConfig.size           = 1024;
    channelConfig.count          = 0;           /* No buffers allocated. We will only use the SetupSend API. */
    channelConfig.prodSckId      = CY_U3P_CPU_SOCKET_PROD;
    channelConfig.consSckId      = CY_U3P_UIB_SOCKET_CONS_0 | CY_FX_EP_DEBUG_RSP_SOCKET;
    channelConfig.prodAvailCount = 0;
    channelConfig.prodHeader     = 0;
    channelConfig.prodFooter     = 0;
    channelConfig.consHeader     = 0;
    channelConfig.dmaMode        = CY_U3P_DMA_MODE_BYTE;
    channelConfig.notification   = 0;
    channelConfig.cb             = 0;

    apiRetStatus = CyU3PDmaChannelCreate (&glDebugRspChannel, CY_U3P_DMA_TYPE_MANUAL_OUT, &channelConfig);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "Debug Response channel create failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    glDebugRspBuffer = (uint8_t *)CyU3PDmaBufferAlloc (1024);
    if (glDebugRspBuffer == 0)
    {
        CyU3PDebugPrint (4, "Failed to allocate memory for debug buffer\r\n");
        CyFxAppErrorHandler (CY_U3P_ERROR_MEMORY_ERROR);
    }
#endif

    apiRetStatus = CyU3PConnectState (CyTrue, CyTrue);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (1, "USB Connect failed, Error Code = %d\r\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

#if LED_STATUS_ENABLED
    led_status_set_connected(CyTrue);
#endif
}

static void
CyFxUvcApplnStop (
        void)
{
#ifdef DEBUG_PRINT_FRAME_COUNT
    /* Clear state variables. */
    glDmaDone    = 1;
    glFrameCount = 0;
#endif /* DEBUG_PRINT_FRAME_COUNT */

    glPipeArmed = CyFalse;

    /* Disable the GPIF state machine. */
    CyU3PGpifDisable (CyFalse);
    glDmaResetFlag = CY_FX_UVC_DMA_RESET_EVENT_NOT_ACTIVE;

    /* Place the EP in NAK mode before cleaning up the pipe. */
    CyU3PUsbSetEpNak (CY_FX_EP_BULK_VIDEO, CyTrue);
    CyU3PBusyWait (125);

    /* Reset and flush the endpoint pipe. */
    CyU3PDmaMultiChannelReset (&glChHandleUVCStream);
    CyU3PUsbFlushEp (CY_FX_EP_BULK_VIDEO);
    CyU3PUsbSetEpNak (CY_FX_EP_BULK_VIDEO, CyFalse);
    CyU3PBusyWait (125);

    /* Allow USB low power link transitions at this stage. */
    CyU3PUsbLPMEnable ();

    CyU3PDebugPrint (4, "Application Stopped\r\n");
}

static CyBool_t
CyFxUvcApplnStart (
        void)
{
    CyU3PReturnStatus_t   apiRetStatus;

    if (glPipeArmed)
        CyFxUvcApplnStop ();

#ifdef DEBUG_PRINT_FRAME_COUNT
    /* Clear state variables. */
    glDmaDone    = 1;
    glFrameCount = 0;
#endif /* DEBUG_PRINT_FRAME_COUNT */

    /* Start with frame ID 0. */
    glUVCHeader[1] &= ~CY_FX_UVC_HEADER_FRAME_ID;

    /* Make sure we return to an active USB link state and stay there. */
    CyU3PUsbLPMDisable ();
    if (CyU3PUsbGetSpeed () == CY_U3P_SUPER_SPEED)
    {
        CyU3PUsbSetLinkPowerState (CyU3PUsbLPM_U0);
        CyU3PBusyWait (200);
    }
    else
    {
        CyU3PUsb2Resume ();
    }

    /* Place the EP in NAK mode before cleaning up the pipe. */
    CyU3PUsbSetEpNak (CY_FX_EP_BULK_VIDEO, CyTrue);
    CyU3PBusyWait (125);

    /* Reset and flush the endpoint pipe. */
    CyU3PUsbFlushEp (CY_FX_EP_BULK_VIDEO);
    CyU3PDmaMultiChannelReset (&glChHandleUVCStream);

    /* Set DMA Channel transfer size, first producer socket */
    apiRetStatus = CyU3PDmaMultiChannelSetXfer (&glChHandleUVCStream, 0, 0);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (1, "DMA Channel Set Transfer Failed, Error Code = %d\r\n", apiRetStatus);
        CyU3PUsbSetEpNak (CY_FX_EP_BULK_VIDEO, CyFalse);
        CyU3PUsbLPMEnable ();
        return CyFalse;
    }

    glDmaResetFlag   = CY_FX_UVC_DMA_RESET_EVENT_NOT_ACTIVE;
    glLastProdTimeMs = CyU3PGetTime ();
    glPipeArmed      = CyTrue;

    CyU3PUsbSetEpNak (CY_FX_EP_BULK_VIDEO, CyFalse);
    CyU3PBusyWait (125);

    /* Start the state machine from the designated start state. */
    apiRetStatus = CyU3PGpifSMSwitch(CY_FX_UVC_INVALID_GPIF_STATE, START_SCK0,
            CY_FX_UVC_INVALID_GPIF_STATE, ALPHA_START_SCK0, CY_FX_UVC_GPIF_SWITCH_TIMEOUT);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "GPIF switch failed (0x%x), reloading GPIF\r\n", apiRetStatus);
        apiRetStatus = CyU3PGpifLoad ((CyU3PGpifConfig_t *) &CyFxGpifConfig);
        if (apiRetStatus == CY_U3P_SUCCESS)
        {
            CyU3PGpifRegisterSMIntrCallback (CyFxGpifCB);
            apiRetStatus = CyU3PGpifSMSwitch(CY_FX_UVC_INVALID_GPIF_STATE, START_SCK0,
                    CY_FX_UVC_INVALID_GPIF_STATE, ALPHA_START_SCK0, CY_FX_UVC_GPIF_SWITCH_TIMEOUT);
        }
        if (apiRetStatus != CY_U3P_SUCCESS)
        {
            CyU3PDebugPrint (1, "GPIF start failed, Error Code = %d\r\n", apiRetStatus);
            CyFxUvcApplnStop ();
            return CyFalse;
        }
    }

    CyU3PDebugPrint (4, "Application Started\r\n");
    return CyTrue;
}

static void
uvc_stop_ack_arm (
        void)
{
    glStopAckSeq     = sensor_last_write_seq ();
    glStopAckSinceMs = CyU3PGetTime ();
    glStopAckPollMs  = glStopAckSinceMs;
    glStopAckSentMs  = glStopAckSinceMs;
    glStopAckPending = CyTrue;
}

static CyBool_t
uvc_stop_applied (
        void)
{
    imgsensor_readback_t rb;
    uint32_t now;

    if (!glStopAckPending)
        return CyTrue;
    now = CyU3PGetTime ();
    if ((uint32_t)(now - glStopAckSinceMs) >= CY_FX_UVC_STOP_ACK_TIMEOUT_MS)
    {
        CyU3PDebugPrint (4, "STOP seq %d not applied after %d ms -- starting anyway\r\n",
                         glStopAckSeq, CY_FX_UVC_STOP_ACK_TIMEOUT_MS);
        glStopAckPending = CyFalse;
        return CyTrue;
    }
    if ((uint32_t)(now - glStopAckPollMs) < CY_FX_UVC_STOP_ACK_POLL_MS)
        return CyFalse;
    glStopAckPollMs = now;
    if (sensor_i2c_read (CHC5_READBACK_I2C_REG, (uint8_t *)&rb, sizeof (rb)) != CY_U3P_SUCCESS
        || rb.rb_seq != rb.rb_seq_echo
        || !(rb.rb_flags & RB_F_APPLIED_SEQ)
        || rb.exposure_us == 0xFFFFFFFFu)
        return CyFalse;
    if ((uint8_t)(rb.applied_seq - glStopAckSeq) < 0x80u)
    {
        glStopAckPending = CyFalse;
        return CyTrue;
    }
    if ((uint32_t)(now - glStopAckSentMs) >= CY_FX_UVC_STOP_ACK_RESEND_MS)
    {
        CyU3PDebugPrint (4, "STOP seq %d not applied after %d ms -- sending it again\r\n",
                         glStopAckSeq, CY_FX_UVC_STOP_ACK_RESEND_MS);
        if (sensor_resend_stop ())
            glStopAckSeq = sensor_last_write_seq ();
        glStopAckSentMs = now;
    }
    return CyFalse;
}

static void
uvc_start_ack_arm (
        CyBool_t written,
        CyBool_t restart)
{
    glStartAckRestart   = restart;
    glStartAckSeq       = sensor_last_write_seq ();
    glStartAckSinceMs   = CyU3PGetTime ();
    glStartAckPollMs    = glStartAckSinceMs;
    glStartAckSentMs    = glStartAckSinceMs;
    glStartAckWriteFail = !written;
    glStartAckPending   = CyTrue;
}

static void
uvc_start_ack_poll (
        void)
{
    imgsensor_readback_t rb;
    uint32_t now;

    if (!glStartAckPending)
        return;
    if (!glIsApplnActive || !sensor_is_streaming ())
    {
        glStartAckPending = CyFalse;
        return;
    }
    now = CyU3PGetTime ();
    if ((uint32_t)(now - glStartAckSinceMs) >= CY_FX_UVC_START_ACK_TIMEOUT_MS)
    {
        CyU3PDebugPrint (4, "START seq %d not applied after %d ms -- giving up\r\n",
                         glStartAckSeq, CY_FX_UVC_START_ACK_TIMEOUT_MS);
        glStartAckPending = CyFalse;
        return;
    }
    if (glStartAckWriteFail && !glStartAckRestart)
    {
        if ((uint32_t)(now - glStartAckSentMs) < CY_FX_UVC_STOP_RETRY_MS)
            return;
    }
    else
    {
        if ((uint32_t)(now - glStartAckPollMs) < CY_FX_UVC_START_ACK_POLL_MS)
            return;
        glStartAckPollMs = now;
        if (sensor_i2c_read (CHC5_READBACK_I2C_REG, (uint8_t *)&rb, sizeof (rb)) != CY_U3P_SUCCESS
            || rb.rb_seq != rb.rb_seq_echo
            || !(rb.rb_flags & RB_F_APPLIED_SEQ)
            || rb.exposure_us == 0xFFFFFFFFu)
            return;
        if ((uint8_t)(rb.applied_seq - glStartAckSeq) < 0x80u)
        {
            glStartAckPending = CyFalse;
            return;
        }
        if ((uint32_t)(now - glStartAckSentMs) < CY_FX_UVC_START_ACK_RESEND_MS)
            return;
    }
    CyU3PDebugPrint (4, "START seq %d not applied -- asserting it again\r\n", glStartAckSeq);
    glStartAckWriteFail = !sensor_resend_start (glStartAckRestart);
    glStartAckSentMs    = now;
}

static void
uvc_stream_stop (
        void)
{
    if (glPipeArmed)
        CyFxUvcApplnStop ();
    glIsApplnActive = CyFalse;
#if LED_STATUS_ENABLED
    led_status_poke();
#endif
    if (sensor_is_streaming ())
    {
        if (sensor_stream_stop ())
            uvc_stop_ack_arm ();
        else
        {
            glStopRetry   = CyTrue;
            glStopRetryMs = CyU3PGetTime ();
        }
    }
}

static void
uvc_stream_start (
        void)
{
    CyBool_t restart = sensor_is_streaming ();
    CyBool_t written;

    selected_img_mode->stream_enable =
        (restart ? STREAM_STATE_ON : STREAM_STATE_OFF) | STREAM_CMD_START;
    CyU3PDebugPrint (4, "START %dx%d mfps=%d\r\n", selected_img_mode->width,
                     selected_img_mode->height, selected_img_mode->fps_mhz);
    written = (sensor_write_config () == CY_U3P_SUCCESS);
    if (!written)
        CyU3PDebugPrint (1, "START write to PS failed -- re-asserted until applied\r\n");
    sensor_set_streaming (CyTrue);
    glStopRetry     = CyFalse;
    glLedHardStop   = CyFalse;
    glIsApplnActive = CyTrue;
#if LED_STATUS_ENABLED
    led_status_poke();
#endif
    if (!CyFxUvcApplnStart ())
    {
        uvc_stream_stop ();
        return;
    }
    uvc_start_ack_arm (written, restart);
}

static void
uvc_request_start (
        void)
{
    if (!sensor_is_streaming () && !uvc_stop_applied ())
    {
        glStartHeld = CyTrue;
        return;
    }
    glStartHeld = CyFalse;
    uvc_stream_start ();
}

static void
uvc_write_params (
        void)
{
    selected_img_mode->stream_enable =
        (glIsApplnActive ? STREAM_STATE_ON : STREAM_STATE_OFF) | STREAM_CMD_NONE;
    sensor_write_config ();
}

/*
 * Entry function for the UVC Application Thread
 */
void
UVCAppThread_Entry (
        uint32_t input)
{
    CyU3PUsbLinkPowerMode usb3mode;
    uint16_t              wakeReason;
    CyU3PReturnStatus_t   apiRetStatus;
    uint32_t              flag;
#ifdef DEBUG_PRINT_FRAME_COUNT
    uint32_t              lastCountMs = 0;
#endif

    (void)input;

    /* Initialize the Uart Debug Module */
    CyFxUVCApplnDebugInit ();

    CyU3PDebugPrint (4, "\r\n");
    CyU3PDebugPrint (4, "========================================\r\n");
    CyU3PDebugPrint (4, "FW %s %s%s rev %d build %d\r\n",
                     FW_VERSION_STRING, FW_GIT_SHA,
                     FW_GIT_DIRTY, FW_GIT_REV, FW_BUILD_NUM);
    CyU3PDebugPrint (4, "Built: %s\r\n", FW_BUILD_DATE);
    CyU3PDebugPrint (4, "========================================\r\n");

    /* Initialize the I2C interface */
    CyFxUVCApplnI2CInit ();

    /* Initialize the UVC Application */
    CyFxUVCApplnInit ();

    for (;;)
    {
        if (glPipeArmed && glIsApplnActive &&
            glDmaResetFlag == CY_FX_UVC_DMA_RESET_EVENT_NOT_ACTIVE &&
            (uint32_t)(CyU3PGetTime () - glLastProdTimeMs) >= CY_FX_UVC_FRAME_TIMEOUT_MS)
        {
            glDmaResetFlag = CY_FX_UVC_DMA_RESET_FRAME_TIMER_OVERFLOW;
            CyU3PEventSet (&glFxUVCEvent, CY_FX_UVC_DMA_RESET_EVENT, CYU3P_EVENT_OR);
        }

        apiRetStatus = CyU3PEventGet (&glFxUVCEvent,
                CY_FX_UVC_STREAM_ABORT_EVENT | CY_FX_UVC_DMA_RESET_EVENT |
                CY_FX_USB_SUSPEND_EVENT_HANDLER | CY_FX_UVC_SENSOR_WRITE_EVENT |
                CY_FX_UVC_FRAME_DONE_EVENT, CYU3P_EVENT_OR_CLEAR, &flag, CY_FX_UVC_LOOP_POLL_MS);

        if (apiRetStatus == CY_U3P_SUCCESS)
        {
            CyU3PDebugPrint (6, "[EVT] flags=0x%x active=%d\r\n", flag, glIsApplnActive);

            if ((flag & CY_FX_UVC_FRAME_DONE_EVENT) != 0)
                glCommitBufferFailureCount = 0;

            if ((flag & CY_FX_UVC_SENSOR_WRITE_EVENT) != 0)
            {
                uint32_t cmd;
                CyBool_t dirty       = CyFalse;
                CyBool_t saw_stop    = CyFalse;
                uint8_t  last_action = 0;
                uint8_t  cmd_count   = 0;

                while (cmd_count < SENSOR_CMD_QUEUE_SIZE &&
                       CyU3PQueueReceive (&glSensorCmdQueue, &cmd, CYU3P_NO_WAIT) == CY_U3P_SUCCESS)
                {
                    cmd_count++;
                    CyU3PDebugPrint (6, "[Q<] cmd[%d]=0x%x\r\n", cmd_count, cmd);

                    switch (SENSOR_CMD_ID(cmd))
                    {
                        case SENSOR_CMD_STREAM_CFG:
                            sensor_handle_uvc_control(SENSOR_CMD_VAL8(cmd), glPendingFrameInterval);
                            sensor_set_auto_flags(all_auto_flags());
                            sensor_set_gamma_x100(CHC5_GAMMA_CURVE_ADJ | glGammaX100);
                            dirty = CyTrue;
                            break;

                        case SENSOR_CMD_STREAM_START:
                            last_action = 1;
                            break;

                        case SENSOR_CMD_STREAM_STOP:
                            last_action = 2;
                            saw_stop    = CyTrue;
                            break;

                        case SENSOR_CMD_ABORT:
                            last_action = 3;
                            saw_stop    = CyTrue;
                            break;

                        case SENSOR_CMD_GAIN:
                            sensor_set_analog_gain(SENSOR_CMD_VAL16(cmd));
                            dirty = CyTrue;
                            break;

                        case SENSOR_CMD_EXPOSURE:
                            sensor_set_exposure(glPendingExposure);
                            dirty = CyTrue;
                            break;

                        case SENSOR_CMD_GAMMA:
                            sensor_set_gamma_x100(CHC5_GAMMA_CURVE_ADJ | SENSOR_CMD_VAL16(cmd));
                            dirty = CyTrue;
                            break;

                        case SENSOR_CMD_TEST_PATTERN:
                            sensor_set_test_pattern(SENSOR_CMD_VAL8(cmd));
                            dirty = CyTrue;
                            break;

                        case SENSOR_CMD_AUTO:
                            sensor_set_auto_flags(SENSOR_CMD_VAL8(cmd));
                            dirty = CyTrue;
                            break;

                        default:
                            CyU3PDebugPrint(6, "[Q<]  UNKNOWN 0x%x\r\n", cmd);
                            break;
                    }
                }

                CyU3PDebugPrint (6, "[WR] cmds=%d action=%d dirty=%d active=%d\r\n",
                                 cmd_count, last_action, dirty, glIsApplnActive);
                if (last_action == 1)
                {
                    if (saw_stop && (glIsApplnActive || glPipeArmed || sensor_is_streaming ()))
                    {
                        glLedHardStop = CyFalse;
                        uvc_stream_stop ();
                    }
                    uvc_request_start ();
                }
                else if (last_action >= 2)
                {
                    glStartHeld   = CyFalse;
                    glLedHardStop = CyFalse;
                    if (glIsApplnActive || glPipeArmed || sensor_is_streaming ())
                        uvc_stream_stop ();
                    else if (dirty || last_action == 2)
                        uvc_write_params ();
                }
                else if (dirty)
                {
                    uvc_write_params ();
                }
            }

            if ((flag & CY_FX_UVC_STREAM_ABORT_EVENT) != 0)
            {
                uint32_t discard;
                uint8_t  discarded = 0;

                while (CyU3PQueueReceive (&glSensorCmdQueue, &discard, CYU3P_NO_WAIT) == CY_U3P_SUCCESS)
                    discarded++;
                CyU3PDebugPrint (6, "[ABORT] active=%d armed=%d flushed=%d\r\n",
                                 glIsApplnActive, glPipeArmed, discarded);
                glStartHeld   = CyFalse;
                glLedHardStop = CyFalse;
                if (glIsApplnActive || glPipeArmed || sensor_is_streaming ())
                    uvc_stream_stop ();
            }

            if (((flag & CY_FX_UVC_DMA_RESET_EVENT) != 0) && glIsApplnActive && glPipeArmed)
            {
                uint8_t  cause = glDmaResetFlag;
                uint8_t  limit = (cause == CY_FX_UVC_DMA_RESET_FRAME_TIMER_OVERFLOW)
                                 ? CY_FX_UVC_TIMER_RESTART_LIMIT
                                 : CY_FX_UVC_MAX_COMMIT_BUF_FAILURE_CNT;
                CyBool_t giveUp = CyFalse;

                CyU3PDebugPrint (4, "DMA reset: %s\r\n",
                                 (cause == CY_FX_UVC_DMA_RESET_FRAME_TIMER_OVERFLOW)
                                 ? "frame timeout" : "commit failure");
                CyFxUvcApplnStop ();

                if (++glCommitBufferFailureCount < limit)
                {
                    CyU3PDebugPrint (4, "DMA restart (%d/%d)\r\n", glCommitBufferFailureCount, limit);
                    if (!CyFxUvcApplnStart ())
                        giveUp = CyTrue;
                }
                else
                {
                    giveUp = CyTrue;
                }

                if (giveUp)
                {
                    uint32_t discard;
                    while (CyU3PQueueReceive (&glSensorCmdQueue, &discard, CYU3P_NO_WAIT) == CY_U3P_SUCCESS) { }
                    glStartHeld = CyFalse;
                    CyU3PDebugPrint (4, "Giving up after %d DMA failures, stopping sensor\r\n",
                                     glCommitBufferFailureCount);
                    glCommitBufferFailureCount = 0;
                    uvc_stream_stop ();
                    CyU3PGpifDisable (CyTrue);
                    glLedHardStop = CyTrue;
#if LED_STATUS_ENABLED
                    led_status_poke();
#endif
                }
            }
            else if ((flag & CY_FX_UVC_DMA_RESET_EVENT) != 0)
            {
                glDmaResetFlag = CY_FX_UVC_DMA_RESET_EVENT_NOT_ACTIVE;
                CyU3PDebugPrint (6, "[DMA] reset ignored, active=0\r\n");
            }

            /* Handle USB suspend event by putting device into low power mode */
            if ((flag & CY_FX_USB_SUSPEND_EVENT_HANDLER) != 0)
            {
                /* Include your code here... to reset and/or shutdown power to the sensor/ISP
                 * This will help to reduce the overall power consumption by the kit */

                /* Place FX3 in Low Power Suspend mode */
                CyU3PDebugPrint(4, "Entering USB Suspend Mode\r\n");
                CyU3PThreadSleep(5);

                /* As per the USB3 specs, link layer takes 10ms (Max.) to leave U3 state. If the device sees some spurious (unwanted)
                 * USB activity it will leave suspend mode (even though USB is in U3 state). The device can wait for 10ms (U3 exit LFPS duration)
                 * and check the Link Layer State. If the link layer is not in U3 state, CX3 device will wake up else it will enter suspend mode */
                do
                {
                    apiRetStatus = CyU3PSysEnterSuspendMode(CY_U3P_SYS_USB_BUS_ACTVTY_WAKEUP_SRC, 0, &wakeReason);
                    if ((apiRetStatus != CY_U3P_SUCCESS) || (CyU3PUsbGetSpeed() != CY_U3P_SUPER_SPEED))
                        break;

                    /* Wait for the maximum U3 exit LFPS duration. */
                    CyU3PThreadSleep(10);

                    /* If the link is still in U3, we can continue to attempt Suspend mode entry. */
                    apiRetStatus = CyU3PUsbGetLinkPowerState(&usb3mode);
                    if ((apiRetStatus != CY_U3P_SUCCESS) || (usb3mode != CyU3PUsbLPM_U3))
                        break;
                }while(1);

                /* Leaving Low Power Suspend mode */
                CyU3PDebugPrint(4, "Leaving Suspend Mode\r\n");

                /* Include your code here... to bring sensor/ISP out of reset and/or switch ON the power to the sensor/ISP */
            }
        }

        if (glStopRetry && !glIsApplnActive &&
            (uint32_t)(CyU3PGetTime () - glStopRetryMs) >= CY_FX_UVC_STOP_RETRY_MS)
        {
            CyBool_t wrote = CyFalse;
            glStopRetryMs = CyU3PGetTime ();
            if (!sensor_is_streaming () || (wrote = sensor_stream_stop ()))
            {
                glStopRetry = CyFalse;
                if (wrote)
                    uvc_stop_ack_arm ();
                CyU3PDebugPrint (4, "STOP delivered on retry\r\n");
            }
        }

        if (glStartHeld)
            uvc_request_start ();

        uvc_start_ack_poll ();

#ifdef DEBUG_PRINT_FRAME_COUNT
        if ((uint32_t)(CyU3PGetTime () - lastCountMs) >= 1000u)
        {
            lastCountMs = CyU3PGetTime ();
            CyU3PDebugPrint (4, "UVC: Completed %d frames and %d buffers\r\n", glFrameCount,
                    (glDmaDone != 0) ? (glDmaDone - 1) : 0);
        }
#endif
    }
}

static void sensor_cmd_enqueue(uint32_t cmd)
{
    CyU3PReturnStatus_t status;
    CyU3PDebugPrint (6, "[Q+] cmd=0x%x\r\n", cmd);
    status = CyU3PQueueSend (&glSensorCmdQueue, &cmd, CYU3P_NO_WAIT);
    if (status != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "[Q!] full! cmd=0x%x\r\n", cmd);
    }
}

static void sensor_cmd_flush(void)
{
    CyU3PDebugPrint (6, "[Q>] flush\r\n");
    CyU3PEventSet (&glFxUVCEvent, CY_FX_UVC_SENSOR_WRITE_EVENT, CYU3P_EVENT_OR);
}

/*
 * Handler for control requests addressed to the Processing Unit.
 */
static void
UVCHandleProcessingUnitRqts (
        void)
{
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
    uint16_t readCount, gainVal;
    switch (wValue)
    {
        case CY_FX_UVC_PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL:
            switch (bRequest)
            {
                case CY_FX_USB_UVC_GET_CUR_REQ:
                    glEp0Buffer[0] = glWbAuto;
                    CyU3PUsbSendEP0Data (1, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_DEF_REQ:
                    glEp0Buffer[0] = 1;
                    CyU3PUsbSendEP0Data (1, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_INFO_REQ:
                    glEp0Buffer[0] = 3;
                    CyU3PUsbSendEP0Data (1, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_SET_CUR_REQ:
                    apiRetStatus = CyU3PUsbGetEP0Data (CY_FX_UVC_MAX_PROBE_SETTING_ALIGNED,
                            glEp0Buffer, &readCount);
                    if (apiRetStatus == CY_U3P_SUCCESS)
                    {
                        glWbAuto = glEp0Buffer[0] ? 1 : 0;
                        sensor_cmd_enqueue(SENSOR_CMD_AUTO | wb_auto_flags());
                        sensor_cmd_flush();
                    }
                    break;
                default:
                    glUvcVcErrorCode = CY_FX_UVC_VC_ERROR_CODE_INVALID_REQUEST;
                    CyU3PUsbStall (0, CyTrue, CyFalse);
                    break;
            }
            break;

        case CY_FX_UVC_PU_GAIN_CONTROL:
            switch (bRequest)
            {
                case CY_FX_USB_UVC_GET_LEN_REQ:
                    glEp0Buffer[0] = 2;
                    glEp0Buffer[1] = 0;
                    CyU3PUsbSendEP0Data (2, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_CUR_REQ:
                    gainVal = uvc_live_gain();
                    if (!gainVal) gainVal = sensor_get_analog_gain();
                    glEp0Buffer[0] = gainVal & 0xFF;
                    glEp0Buffer[1] = (gainVal >> 8) & 0xFF;
                    CyU3PUsbSendEP0Data (2, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_MIN_REQ:
                    glEp0Buffer[0] = 0;
                    glEp0Buffer[1] = 0;
                    CyU3PUsbSendEP0Data (2, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_MAX_REQ:
                    glEp0Buffer[0] = (uint8_t)(g_uvc_gain_max_db10 & 0xFF);
                    glEp0Buffer[1] = (uint8_t)(g_uvc_gain_max_db10 >> 8);
                    CyU3PUsbSendEP0Data (2, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_RES_REQ:
                    glEp0Buffer[0] = 1;
                    glEp0Buffer[1] = 0;
                    CyU3PUsbSendEP0Data (2, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_INFO_REQ:
                    glEp0Buffer[0] = 3;
                    glEp0Buffer[1] = 0;
                    CyU3PUsbSendEP0Data (2, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_DEF_REQ:
                    glEp0Buffer[0] = 0;
                    glEp0Buffer[1] = 0;
                    CyU3PUsbSendEP0Data (2, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_SET_CUR_REQ:
                    apiRetStatus = CyU3PUsbGetEP0Data (CY_FX_UVC_MAX_PROBE_SETTING_ALIGNED,
                            glEp0Buffer, &readCount);
                    if (apiRetStatus == CY_U3P_SUCCESS)
                    {
                        gainVal = CY_U3P_MAKEWORD(glEp0Buffer[1], glEp0Buffer[0]);
                        sensor_cmd_enqueue(SENSOR_CMD_GAIN | gainVal);
                        sensor_cmd_flush();
                    }
                    break;
                default:
                    glUvcVcErrorCode = CY_FX_UVC_VC_ERROR_CODE_INVALID_REQUEST;
                    CyU3PUsbStall (0, CyTrue, CyFalse);
                    break;
            }
            break;

        case CY_FX_UVC_PU_GAMMA_CONTROL:
        {
            uint16_t val = UVC_GAMMA_DEF;
            switch (bRequest)
            {
                case CY_FX_USB_UVC_GET_CUR_REQ: val = glGammaX100;    break;
                case CY_FX_USB_UVC_GET_MIN_REQ: val = UVC_GAMMA_MIN;  break;
                case CY_FX_USB_UVC_GET_MAX_REQ: val = UVC_GAMMA_MAX;  break;
                case CY_FX_USB_UVC_GET_RES_REQ: val = 1;              break;
                case CY_FX_USB_UVC_GET_DEF_REQ: val = UVC_GAMMA_DEF;  break;
                case CY_FX_USB_UVC_GET_INFO_REQ:
                    glEp0Buffer[0] = 3;
                    CyU3PUsbSendEP0Data (1, (uint8_t *)glEp0Buffer);
                    return;
                case CY_FX_USB_UVC_SET_CUR_REQ:
                    apiRetStatus = CyU3PUsbGetEP0Data (CY_FX_UVC_MAX_PROBE_SETTING_ALIGNED,
                            glEp0Buffer, &readCount);
                    if (apiRetStatus == CY_U3P_SUCCESS && readCount >= 2)
                    {
                        val = CY_U3P_MAKEWORD(glEp0Buffer[1], glEp0Buffer[0]);
                        if (val >= UVC_GAMMA_MIN && val <= UVC_GAMMA_MAX)
                        {
                            glGammaX100 = val;
                            sensor_cmd_enqueue(SENSOR_CMD_GAMMA | val);
                            sensor_cmd_flush();
                        }
                    }
                    return;
                default:
                    glUvcVcErrorCode = CY_FX_UVC_VC_ERROR_CODE_INVALID_REQUEST;
                    CyU3PUsbStall (0, CyTrue, CyFalse);
                    return;
            }
            glEp0Buffer[0] = (uint8_t)(val & 0xFF);
            glEp0Buffer[1] = (uint8_t)(val >> 8);
            CyU3PUsbSendEP0Data (2, (uint8_t *)glEp0Buffer);
            break;
        }

        default:
            glUvcVcErrorCode = CY_FX_UVC_VC_ERROR_CODE_INVALID_CONTROL;
            CyU3PUsbStall (0, CyTrue, CyFalse);
            break;
    }
}

/*
 * Handler for control requests addressed to the UVC Camera Terminal unit.
 */
static void
UVCHandleCameraTerminalRqts (
        void)
{
    uint16_t readCount;
#ifdef UVC_PTZ_SUPPORT
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
    uint16_t zoomVal;
    int32_t  panVal, tiltVal;
    CyBool_t sendData = CyFalse;
#endif

    switch (wValue)
    {
        case CY_FX_UVC_CT_AE_MODE_CONTROL:
            switch (bRequest)
            {
                case CY_FX_USB_UVC_GET_CUR_REQ:
                    glEp0Buffer[0] = glAeMode;
                    CyU3PUsbSendEP0Data (1, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_DEF_REQ:
                    glEp0Buffer[0] = 0x02;
                    CyU3PUsbSendEP0Data (1, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_RES_REQ:
                    glEp0Buffer[0] = 0x03;
                    CyU3PUsbSendEP0Data (1, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_INFO_REQ:
                    glEp0Buffer[0] = 3;
                    CyU3PUsbSendEP0Data (1, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_SET_CUR_REQ:
                    if (CyU3PUsbGetEP0Data (CY_FX_UVC_MAX_PROBE_SETTING_ALIGNED,
                                glEp0Buffer, &readCount) == CY_U3P_SUCCESS &&
                        (glEp0Buffer[0] == 0x01 || glEp0Buffer[0] == 0x02))
                    {
                        glAeMode = glEp0Buffer[0];
                        sensor_cmd_enqueue(SENSOR_CMD_AUTO | ae_auto_flags());
                        sensor_cmd_flush();
                    }
                    break;
                default:
                    glUvcVcErrorCode = CY_FX_UVC_VC_ERROR_CODE_INVALID_REQUEST;
                    CyU3PUsbStall (0, CyTrue, CyFalse);
                    break;
            }
            break;

        case CY_FX_UVC_CT_EXPOSURE_TIME_ABSOLUTE_CONTROL:
            switch (bRequest)
            {
                case CY_FX_USB_UVC_GET_LEN_REQ:
                    glEp0Buffer[0] = 4;
                    glEp0Buffer[1] = 0;
                    CyU3PUsbSendEP0Data (2, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_CUR_REQ:
                {
                    uint32_t us = uvc_live_exposure();
                    uint32_t val;
                    if (!us) us = sensor_get_exposure();
                    val = (us + UVC_EXPOSURE_UNIT_US / 2u) / UVC_EXPOSURE_UNIT_US;
                    if (val < 1u)
                        val = 1u;
                    glEp0Buffer[0] = (val)       & 0xFF;
                    glEp0Buffer[1] = (val >> 8)  & 0xFF;
                    glEp0Buffer[2] = (val >> 16) & 0xFF;
                    glEp0Buffer[3] = (val >> 24) & 0xFF;
                    CyU3PUsbSendEP0Data (4, (uint8_t *)glEp0Buffer);
                    break;
                }
                case CY_FX_USB_UVC_GET_MIN_REQ:
                    glEp0Buffer[0] = 1;
                    glEp0Buffer[1] = 0;
                    glEp0Buffer[2] = 0;
                    glEp0Buffer[3] = 0;
                    CyU3PUsbSendEP0Data (4, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_MAX_REQ:
                    glEp0Buffer[0] = (UVC_EXPOSURE_MAX_UNITS)       & 0xFF;
                    glEp0Buffer[1] = (UVC_EXPOSURE_MAX_UNITS >> 8)  & 0xFF;
                    glEp0Buffer[2] = (UVC_EXPOSURE_MAX_UNITS >> 16) & 0xFF;
                    glEp0Buffer[3] = (UVC_EXPOSURE_MAX_UNITS >> 24) & 0xFF;
                    CyU3PUsbSendEP0Data (4, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_RES_REQ:
                    glEp0Buffer[0] = 1;
                    glEp0Buffer[1] = 0;
                    glEp0Buffer[2] = 0;
                    glEp0Buffer[3] = 0;
                    CyU3PUsbSendEP0Data (4, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_INFO_REQ:
                    glEp0Buffer[0] = 3;
                    CyU3PUsbSendEP0Data (1, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_DEF_REQ:
                    glEp0Buffer[0] = (UVC_EXPOSURE_DEF_UNITS)       & 0xFF;
                    glEp0Buffer[1] = (UVC_EXPOSURE_DEF_UNITS >> 8)  & 0xFF;
                    glEp0Buffer[2] = (UVC_EXPOSURE_DEF_UNITS >> 16) & 0xFF;
                    glEp0Buffer[3] = (UVC_EXPOSURE_DEF_UNITS >> 24) & 0xFF;
                    CyU3PUsbSendEP0Data (4, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_SET_CUR_REQ:
                {
                    CyU3PReturnStatus_t status;
                    status = CyU3PUsbGetEP0Data (CY_FX_UVC_MAX_PROBE_SETTING_ALIGNED,
                            glEp0Buffer, &readCount);
                    if (status == CY_U3P_SUCCESS)
                    {
                        uint32_t val = glEp0Buffer[0]
                                     | (glEp0Buffer[1] << 8)
                                     | (glEp0Buffer[2] << 16)
                                     | ((uint32_t)glEp0Buffer[3] << 24);
                        if (val < 1u)
                            val = 1u;
                        if (val > UVC_EXPOSURE_MAX_UNITS)
                            val = UVC_EXPOSURE_MAX_UNITS;
                        glPendingExposure = val * UVC_EXPOSURE_UNIT_US;
                        sensor_cmd_enqueue(SENSOR_CMD_EXPOSURE);
                        sensor_cmd_flush();
                    }
                    break;
                }
                default:
                    glUvcVcErrorCode = CY_FX_UVC_VC_ERROR_CODE_INVALID_REQUEST;
                    CyU3PUsbStall (0, CyTrue, CyFalse);
                    break;
            }
            break;

#ifdef UVC_PTZ_SUPPORT
        case CY_FX_UVC_CT_ZOOM_ABSOLUTE_CONTROL:
            switch (bRequest)
            {
                case CY_FX_USB_UVC_GET_INFO_REQ:
                    glEp0Buffer[0] = 3;                /* Support GET/SET queries. */
                    CyU3PUsbSendEP0Data (1, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_CUR_REQ: /* Current zoom control value. */
                    zoomVal  = CyFxUvcAppGetCurrentZoom ();
                    sendData = CyTrue;
                    break;
                case CY_FX_USB_UVC_GET_MIN_REQ: /* Minimum zoom control value. */
                    zoomVal  = CyFxUvcAppGetMinimumZoom ();
                    sendData = CyTrue;
                    break;
                case CY_FX_USB_UVC_GET_MAX_REQ: /* Maximum zoom control value. */
                    zoomVal  = CyFxUvcAppGetMaximumZoom ();
                    sendData = CyTrue;
                    break;
                case CY_FX_USB_UVC_GET_RES_REQ: /* Resolution is one unit. */
                    zoomVal  = CyFxUvcAppGetZoomResolution ();
                    sendData = CyTrue;
                    break;
                case CY_FX_USB_UVC_GET_DEF_REQ: /* Default zoom setting. */
                    zoomVal  = CyFxUvcAppGetDefaultZoom ();
                    sendData = CyTrue;
                    break;
                case CY_FX_USB_UVC_SET_CUR_REQ:
                    apiRetStatus = CyU3PUsbGetEP0Data (CY_FX_UVC_MAX_PROBE_SETTING_ALIGNED,
                            glEp0Buffer, &readCount);
                    if (apiRetStatus == CY_U3P_SUCCESS)
                    {
                        zoomVal = (glEp0Buffer[0]) | (glEp0Buffer[1] << 8);
                        CyFxUvcAppModifyZoom (zoomVal);
                    }
                    break;
                default:
                    glUvcVcErrorCode = CY_FX_UVC_VC_ERROR_CODE_INVALID_REQUEST;
                    CyU3PUsbStall (0, CyTrue, CyFalse);
                    break;
            }

            if (sendData)
            {
                /* Send the 2-byte data in zoomVal back to the USB host. */
                glEp0Buffer[0] = CY_U3P_GET_LSB (zoomVal);
                glEp0Buffer[1] = CY_U3P_GET_MSB (zoomVal);
                CyU3PUsbSendEP0Data (wLength, (uint8_t *)glEp0Buffer);
            }
            break;

        case CY_FX_UVC_CT_PANTILT_ABSOLUTE_CONTROL:
            switch (bRequest)
            {
                case CY_FX_USB_UVC_GET_INFO_REQ:
                    glEp0Buffer[0] = 3;                /* GET/SET requests supported for this control */
                    CyU3PUsbSendEP0Data (1, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_CUR_REQ:
                    panVal   = CyFxUvcAppGetCurrentPan ();
                    tiltVal  = CyFxUvcAppGetCurrentTilt ();
                    sendData = CyTrue;
                    break;
                case CY_FX_USB_UVC_GET_MIN_REQ:
                    panVal   = CyFxUvcAppGetMinimumPan ();
                    tiltVal  = CyFxUvcAppGetMinimumTilt ();
                    sendData = CyTrue;
                    break;
                case CY_FX_USB_UVC_GET_MAX_REQ:
                    panVal   = CyFxUvcAppGetMaximumPan ();
                    tiltVal  = CyFxUvcAppGetMaximumTilt ();
                    sendData = CyTrue;
                    break;
                case CY_FX_USB_UVC_GET_RES_REQ:
                    panVal   = CyFxUvcAppGetPanResolution ();
                    tiltVal  = CyFxUvcAppGetTiltResolution ();
                    sendData = CyTrue;
                    break;
                case CY_FX_USB_UVC_GET_DEF_REQ:
                    panVal   = CyFxUvcAppGetDefaultPan ();
                    tiltVal  = CyFxUvcAppGetDefaultTilt ();
                    sendData = CyTrue;
                    break;
                case CY_FX_USB_UVC_SET_CUR_REQ:
                    apiRetStatus = CyU3PUsbGetEP0Data (CY_FX_UVC_MAX_PROBE_SETTING_ALIGNED,
                            glEp0Buffer, &readCount);
                    if (apiRetStatus == CY_U3P_SUCCESS)
                    {
                        panVal = (glEp0Buffer[0]) | (glEp0Buffer[1]<<8) |
                            (glEp0Buffer[2]<<16) | (glEp0Buffer[2]<<24);
                        tiltVal = (glEp0Buffer[4]) | (glEp0Buffer[5]<<8) |
                            (glEp0Buffer[6]<<16) | (glEp0Buffer[7]<<24);

                        CyFxUvcAppModifyPan (panVal);
                        CyFxUvcAppModifyTilt (tiltVal);
                    }
                    break;
                default:
                    CyU3PUsbStall (0, CyTrue, CyFalse);
                    glUvcVcErrorCode = CY_FX_UVC_VC_ERROR_CODE_INVALID_REQUEST;
                    break;
            }

            if (sendData)
            {
                /* Send the 8-byte PAN and TILT values back to the USB host. */
                glEp0Buffer[0] = CY_U3P_DWORD_GET_BYTE0 (panVal);
                glEp0Buffer[1] = CY_U3P_DWORD_GET_BYTE1 (panVal);
                glEp0Buffer[2] = CY_U3P_DWORD_GET_BYTE2 (panVal);
                glEp0Buffer[3] = CY_U3P_DWORD_GET_BYTE3 (panVal);
                glEp0Buffer[4] = CY_U3P_DWORD_GET_BYTE0 (tiltVal);
                glEp0Buffer[5] = CY_U3P_DWORD_GET_BYTE1 (tiltVal);
                glEp0Buffer[6] = CY_U3P_DWORD_GET_BYTE2 (tiltVal);
                glEp0Buffer[7] = CY_U3P_DWORD_GET_BYTE3 (tiltVal);
                CyU3PUsbSendEP0Data (wLength, (uint8_t *)glEp0Buffer);
            }
            break;
#endif

        default:
            glUvcVcErrorCode = CY_FX_UVC_VC_ERROR_CODE_INVALID_CONTROL;
            CyU3PUsbStall (0, CyTrue, CyFalse);
            break;
    }
}

/*
 * Handler for UVC Interface control requests.
 */
static void
UVCHandleInterfaceCtrlRqts (
        void)
{
    switch (wValue)
    {
        /* Control to send video control errors to the Host. When device stalls a video
         * control request, Windows host gets the error through this control */
        case CY_FX_UVC_VC_REQUEST_ERROR_CODE_CONTROL:
            switch (bRequest)
            {
                case CY_FX_USB_UVC_GET_CUR_REQ:
                    CyU3PUsbSendEP0Data(1, &glUvcVcErrorCode);
                    glUvcVcErrorCode = CY_FX_UVC_VC_ERROR_CODE_NO_ERROR;
                    break;
            }
            break;
    }
}

static void
UVCHandleExtensionUnitRqts (
        void)
{
    uint16_t readCount;

    switch (wValue)
    {
        case CY_FX_UVC_XU_TEST_PATTERN_CONTROL:
            switch (bRequest)
            {
                case CY_FX_USB_UVC_GET_INFO_REQ:
                    glEp0Buffer[0] = 3;
                    CyU3PUsbSendEP0Data (1, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_LEN_REQ:
                    glEp0Buffer[0] = 1;
                    glEp0Buffer[1] = 0;
                    CyU3PUsbSendEP0Data (2, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_CUR_REQ:
                    glEp0Buffer[0] = sensor_get_test_pattern ();
                    CyU3PUsbSendEP0Data (1, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_MIN_REQ:
                case CY_FX_USB_UVC_GET_DEF_REQ:
                    glEp0Buffer[0] = 0;
                    CyU3PUsbSendEP0Data (1, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_MAX_REQ:
                    glEp0Buffer[0] = g_uvc_tp_max;
                    CyU3PUsbSendEP0Data (1, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_RES_REQ:
                    glEp0Buffer[0] = 1;
                    CyU3PUsbSendEP0Data (1, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_SET_CUR_REQ:
                    if (CyU3PUsbGetEP0Data (CY_FX_UVC_MAX_PROBE_SETTING_ALIGNED,
                                glEp0Buffer, &readCount) != CY_U3P_SUCCESS || readCount < 1)
                        break;
#if UVC_TEST_HOOKS
                    if (glEp0Buffer[0] & 0x80)
                    {
                        g_test_start_hook = glEp0Buffer[0] & 0x03;
                        break;
                    }
#endif
                    if (glEp0Buffer[0] <= g_uvc_tp_max)
                    {
                        sensor_cmd_enqueue (SENSOR_CMD_TEST_PATTERN | glEp0Buffer[0]);
                        sensor_cmd_flush ();
                    }
                    break;
                default:
                    glUvcVcErrorCode = CY_FX_UVC_VC_ERROR_CODE_INVALID_REQUEST;
                    CyU3PUsbStall (0, CyTrue, CyFalse);
                    break;
            }
            break;

        default:
            glUvcVcErrorCode = CY_FX_UVC_VC_ERROR_CODE_INVALID_CONTROL;
            CyU3PUsbStall (0, CyTrue, CyFalse);
            break;
    }
}

/*
 * Handler for the video streaming control requests.
 */
static void UVCHandleVideoStreamingRqts (void)
{
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
    uint16_t readCount;
    uint8_t *activeProbe = (usbSpeed == CY_U3P_SUPER_SPEED) ? glProbeCtrl : glProbeCtrl20;

    switch (wValue)
    {
        case CY_FX_UVC_PROBE_CTRL:
            switch (bRequest)
            {
                case CY_FX_USB_UVC_GET_INFO_REQ:
                    glEp0Buffer[0] = 3;                /* GET/SET requests are supported. */
                    CyU3PUsbSendEP0Data (1, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_LEN_REQ:
                    glEp0Buffer[0] = CY_FX_UVC_MAX_PROBE_SETTING;
                    CyU3PUsbSendEP0Data (1, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_CUR_REQ:
                case CY_FX_USB_UVC_GET_MIN_REQ:
                case CY_FX_USB_UVC_GET_MAX_REQ:
                case CY_FX_USB_UVC_GET_DEF_REQ: /* There is only one setting per USB speed. */
                    CyU3PUsbSendEP0Data (CY_FX_UVC_MAX_PROBE_SETTING, activeProbe);
                    break;
                case CY_FX_USB_UVC_SET_CUR_REQ:
                    apiRetStatus = CyU3PUsbGetEP0Data (CY_FX_UVC_MAX_PROBE_SETTING_ALIGNED, glCommitCtrl, &readCount);
                    if (apiRetStatus == CY_U3P_SUCCESS)
                    {
                        activeProbe[2] = glCommitCtrl[2];
                        activeProbe[3] = glCommitCtrl[3];
                        activeProbe[4] = glCommitCtrl[4];
                        activeProbe[5] = glCommitCtrl[5];
                        activeProbe[6] = glCommitCtrl[6];
                        activeProbe[7] = glCommitCtrl[7];
                        uvc_negotiate (activeProbe);
                    }
                    break;
                default:
                    CyU3PUsbStall (0, CyTrue, CyFalse);
                    break;
            }
            break;

        case CY_FX_UVC_COMMIT_CTRL:
            switch (bRequest)
            {
                case CY_FX_USB_UVC_GET_INFO_REQ:
                    glEp0Buffer[0] = 3;                        /* GET/SET requests are supported. */
                    CyU3PUsbSendEP0Data (1, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_LEN_REQ:
                    glEp0Buffer[0] = CY_FX_UVC_MAX_PROBE_SETTING;
                    CyU3PUsbSendEP0Data (1, (uint8_t *)glEp0Buffer);
                    break;
                case CY_FX_USB_UVC_GET_CUR_REQ:
                    CyU3PUsbSendEP0Data (CY_FX_UVC_MAX_PROBE_SETTING, activeProbe);
                    break;
                case CY_FX_USB_UVC_SET_CUR_REQ:
                    apiRetStatus = CyU3PUsbGetEP0Data (CY_FX_UVC_MAX_PROBE_SETTING_ALIGNED, glCommitCtrl, &readCount);
                    if (apiRetStatus == CY_U3P_SUCCESS)
                    {
                        if (glCommitCtrl[3] == 0)
                        {
                            sensor_cmd_enqueue(SENSOR_CMD_STREAM_STOP);
                            sensor_cmd_flush();
                        }
                        else
                        {
                            uint8_t fi = uvc_negotiate (glCommitCtrl);

                            CyU3PMemCopy (activeProbe, glCommitCtrl, CY_FX_UVC_MAX_PROBE_SETTING);
                            glPendingFrameInterval = (uint32_t)glCommitCtrl[4] |
                                                     ((uint32_t)glCommitCtrl[5] << 8) |
                                                     ((uint32_t)glCommitCtrl[6] << 16) |
                                                     ((uint32_t)glCommitCtrl[7] << 24);

                            sensor_cmd_enqueue(SENSOR_CMD_STREAM_CFG | fi);
                            sensor_cmd_enqueue(SENSOR_CMD_STREAM_START);
                            sensor_cmd_flush();
                        }
                    }
                    break;

                default:
                    CyU3PUsbStall (0, CyTrue, CyFalse);
                    break;
            }
            break;

        default:
            CyU3PUsbStall (0, CyTrue, CyFalse);
            break;
    }
}

/*
 * Entry function for the UVC control request processing thread.
 */
void
UVCAppEP0Thread_Entry (
        uint32_t input)
{
    uint32_t eventMask = (CY_FX_UVC_VIDEO_CONTROL_REQUEST_EVENT | CY_FX_UVC_VIDEO_STREAM_REQUEST_EVENT);
    uint32_t eventFlag;

#ifdef USB_DEBUG_INTERFACE
    CyU3PReturnStatus_t apiRetStatus;
    CyU3PDmaBuffer_t    dmaInfo;

    eventMask |= CY_FX_USB_DEBUG_CMD_EVENT;
#endif

    for (;;)
    {
        /* Wait for a Video control or streaming related request on the control endpoint. */
        if (CyU3PEventGet (&glFxUVCEvent, eventMask, CYU3P_EVENT_OR_CLEAR, &eventFlag,
                    CYU3P_WAIT_FOREVER) == CY_U3P_SUCCESS)
        {
            usbSpeed = CyU3PUsbGetSpeed ();

            if (eventFlag & CY_FX_UVC_VIDEO_CONTROL_REQUEST_EVENT)
            {
                if (bRequest == CY_FX_USB_UVC_SET_CUR_REQ)
                    uvc_readback_touch();
                switch ((wIndex >> 8))
                {
                    case CY_FX_UVC_PROCESSING_UNIT_ID:
                        UVCHandleProcessingUnitRqts ();
                        break;

                    case CY_FX_UVC_CAMERA_TERMINAL_ID:
                        UVCHandleCameraTerminalRqts ();
                        break;

                    case CY_FX_UVC_INTERFACE_CTRL:
                        UVCHandleInterfaceCtrlRqts ();
                        break;

                    case CY_FX_UVC_EXTENSION_UNIT_ID:
                        UVCHandleExtensionUnitRqts ();
                        break;

                    default:
                        /* Unsupported request. Fail by stalling the control endpoint. */
                        CyU3PUsbStall (0, CyTrue, CyFalse);
                        break;
                }
            }

            if (eventFlag & CY_FX_UVC_VIDEO_STREAM_REQUEST_EVENT)
            {
                if (wIndex != CY_FX_UVC_STREAM_INTERFACE)
                {
                    CyU3PUsbStall (0, CyTrue, CyFalse);
                }
                else
                {
                    if (bRequest == CY_FX_USB_UVC_SET_CUR_REQ)
                        uvc_readback_touch();
                    UVCHandleVideoStreamingRqts ();
                }
            }

#ifdef USB_DEBUG_INTERFACE
            if (eventFlag & CY_FX_USB_DEBUG_CMD_EVENT)
            {
                /* Get the command buffer */
                apiRetStatus = CyU3PDmaChannelGetBuffer (&glDebugCmdChannel, &dmaInfo, CYU3P_WAIT_FOREVER);
                if (apiRetStatus != CY_U3P_SUCCESS)
                {
                    CyU3PDebugPrint (4, "Failed to receive debug command, Error code = %d\r\n", apiRetStatus);
                    CyFxAppErrorHandler (apiRetStatus);
                }

                /* Decode the command from the command buffer, error checking is not implemented,
                 * so the command is expected to be correctly sent from the host application. First byte indicates
                 * read (0x00) or write (0x01) command. Second and third bytes are register address high byte and
                 * register address low byte. For read commands the fourth byte (optional) can be N>0, to read N
                 * registers in sequence. Response first byte is status (0=Pass, !0=Fail) followed by N pairs of
                 * register value high byte and register value low byte.
                 */
                if (dmaInfo.buffer[0] == 0)
                {
                    if (dmaInfo.count == 3)
                    {
                        glDebugRspBuffer[0] = SensorRead2B (SENSOR_ADDR_RD, dmaInfo.buffer[1], dmaInfo.buffer[2],
                        		(glDebugRspBuffer+1));
                        dmaInfo.count = 3;
                    }
                    else if (dmaInfo.count == 4)
                    {
                        if (dmaInfo.buffer[3] > 0)
                        {
                                glDebugRspBuffer[0] = SensorRead (SENSOR_ADDR_RD, dmaInfo.buffer[1], dmaInfo.buffer[2],
                                		(dmaInfo.buffer[3]*2), (glDebugRspBuffer+1));
                        }
                        dmaInfo.count = dmaInfo.buffer[3]*2+1;
                    }
                }
                /*  For write commands, the register address is followed by N pairs (N>0) of register value high byte
                 *  and register value low byte to write in sequence. Response first byte is status (0=Pass, !0=Fail)
                 *  followed by N pairs of register value high byte and register value low byte after modification.
                 */
                else if (dmaInfo.buffer[0] == 1)
                {
                        glDebugRspBuffer[0] = SensorWrite (SENSOR_ADDR_WR, dmaInfo.buffer[1], dmaInfo.buffer[2],
                        		(dmaInfo.count-3), (dmaInfo.buffer+3));
                        if (glDebugRspBuffer[0] != CY_U3P_SUCCESS)
                        	break;
                        glDebugRspBuffer[0] = SensorRead (SENSOR_ADDR_RD, dmaInfo.buffer[1], dmaInfo.buffer[2],
                        		(dmaInfo.count-3), (glDebugRspBuffer+1));
                        if (glDebugRspBuffer[0] != CY_U3P_SUCCESS)
                        	break;
                    dmaInfo.count -= 2;
                }
                /* Default case, prepare buffer for loop back command in response */
                else
                {
                   /* For now, we just copy the command into the response buffer; and send it back to the
                      USB host. This can be expanded to include I2C transfers. */
                    CyU3PMemCopy (glDebugRspBuffer, dmaInfo.buffer, dmaInfo.count);
                }

                dmaInfo.buffer = glDebugRspBuffer;
                dmaInfo.size   = 1024;
                dmaInfo.status = 0;

                /* Free the command buffer to receive the next command. */
                apiRetStatus = CyU3PDmaChannelDiscardBuffer (&glDebugCmdChannel);
                if (apiRetStatus != CY_U3P_SUCCESS)
                {
                    CyU3PDebugPrint (4, "Failed to free up command OUT EP buffer, Error code = %d\r\n", apiRetStatus);
                    CyFxAppErrorHandler (apiRetStatus);
                }

                /* Wait until the response has gone out. */
                CyU3PDmaChannelWaitForCompletion (&glDebugRspChannel, CYU3P_WAIT_FOREVER);

                apiRetStatus = CyU3PDmaChannelSetupSendBuffer (&glDebugRspChannel, &dmaInfo);
                if (apiRetStatus != CY_U3P_SUCCESS)
                {
                    CyU3PDebugPrint (4, "Failed to send debug response, Error code = %d\r\n", apiRetStatus);
                    CyFxAppErrorHandler (apiRetStatus);
                }
            }
#endif
        }

        /* Allow other ready threads to run. */
        CyU3PThreadRelinquish ();
    }
}

/*
 * This function is called by the FX3 framework once the ThreadX RTOS has started up.
 * The application specific threads and other OS resources are created and initialized here.
 */
void
CyFxApplicationDefine (
        void)
{
    void *ptr1, *ptr2;
    uint32_t retThrdCreate;

    /* Allocate the memory for the thread stacks. */
    ptr1 = CyU3PMemAlloc (UVC_APP_THREAD_STACK);
    ptr2 = CyU3PMemAlloc (UVC_APP_EP0_THREAD_STACK);
    if ((ptr1 == 0) || (ptr2 == 0))
        goto fatalErrorHandler;

    /* Create the UVC application thread. */
    retThrdCreate = CyU3PThreadCreate (&uvcAppThread,   /* UVC Thread structure */
            "30:UVC App Thread",                        /* Thread Id and name */
            UVCAppThread_Entry,                         /* UVC Application Thread Entry function */
            0,                                          /* No input parameter to thread */
            ptr1,                                       /* Pointer to the allocated thread stack */
            UVC_APP_THREAD_STACK,                       /* UVC Application Thread stack size */
            UVC_APP_THREAD_PRIORITY,                    /* UVC Application Thread priority */
            UVC_APP_THREAD_PRIORITY,                    /* Threshold value for thread pre-emption. */
            CYU3P_NO_TIME_SLICE,                        /* No time slice for the application thread */
            CYU3P_AUTO_START                            /* Start the Thread immediately */
            );
    if (retThrdCreate != 0)
    {
        goto fatalErrorHandler;
    }

#if LED_STATUS_ENABLED
    {
        void *ptrLed = CyU3PMemAlloc (LED_THREAD_STACK);
        if (ptrLed)
            CyU3PThreadCreate (&glLedThread, "32:LED", led_status_thread_entry, 0,
                    ptrLed, LED_THREAD_STACK, LED_THREAD_PRIORITY,
                    LED_THREAD_PRIORITY, CYU3P_NO_TIME_SLICE, CYU3P_AUTO_START);
    }
#endif

    /* Create the control request handling thread. */
    retThrdCreate = CyU3PThreadCreate (&uvcAppEP0Thread,        /* UVC Thread structure */
            "31:UVC App EP0 Thread",                            /* Thread Id and name */
            UVCAppEP0Thread_Entry,                              /* UVC Application EP0 Thread Entry function */
            0,                                                  /* No input parameter to thread */
            ptr2,                                               /* Pointer to the allocated thread stack */
            UVC_APP_EP0_THREAD_STACK,                           /* UVC Application Thread stack size */
            UVC_APP_EP0_THREAD_PRIORITY,                        /* UVC Application Thread priority */
            UVC_APP_EP0_THREAD_PRIORITY,                        /* Threshold value for thread pre-emption. */
            CYU3P_NO_TIME_SLICE,                                /* No time slice for the application thread */
            CYU3P_AUTO_START                                    /* Start the Thread immediately */
            );
    if (retThrdCreate != 0)
    {
        goto fatalErrorHandler;
    }

    return;

fatalErrorHandler:
    /* Add custom recovery or debug actions here */
    /* Loop indefinitely */
    while (1);
}

/* Main entry point for the C code. We perform device initialization and start
 * the ThreadX RTOS here.
 */
int
main (
        void)
{
    CyU3PReturnStatus_t apiRetStatus;
    CyU3PIoMatrixConfig_t io_cfg;

    CyU3PSysClockConfig_t clk_cfg;
    /* Initialize the device */
    clk_cfg.setSysClk400  = CyTrue;
    clk_cfg.cpuClkDiv     = 2;
    clk_cfg.dmaClkDiv     = 2;
    clk_cfg.mmioClkDiv    = 2;
    clk_cfg.useStandbyClk = CyFalse;
    clk_cfg.clkSrc        = CY_U3P_SYS_CLK;
    apiRetStatus = CyU3PDeviceInit (&clk_cfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        goto handle_fatal_error;
    }

    /* Turn on instruction cache to improve firmware performance. Use Release build to improve it further */
    apiRetStatus = CyU3PDeviceCacheControl (CyTrue, CyFalse, CyFalse);

    /* Configure the IO matrix for the device. */
    io_cfg.isDQ32Bit        = CyTrue;
    io_cfg.s0Mode       	= CyFalse;
    io_cfg.s1Mode	        = CyFalse;
    io_cfg.lppMode          = CY_U3P_IO_MATRIX_LPP_DEFAULT;
    io_cfg.gpioSimpleEn[0]  = 0;
    io_cfg.gpioSimpleEn[1]  = (1u << (LED_GPIO_RED - 32));
    io_cfg.gpioComplexEn[0] = 0;
    io_cfg.gpioComplexEn[1] = 0;
    io_cfg.useUart          = CyTrue;   /* Uart is enabled for logging. */
    io_cfg.useI2C           = CyTrue;   /* I2C is used for the sensor interface. */
    io_cfg.useI2S           = CyFalse;
    io_cfg.useSpi           = CyFalse;

    apiRetStatus = CyU3PDeviceConfigureIOMatrix (&io_cfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        goto handle_fatal_error;
    }

    /* This is a non returnable call for initializing the RTOS kernel */
    CyU3PKernelEntry ();

    /* Dummy return to make the compiler happy */
    return 0;

handle_fatal_error:
    /* Cannot recover from this error. */
    while (1);
}

#if LED_STATUS_ENABLED
led_state_t led_state_source(CyBool_t *link_degraded_out)
{
    *link_degraded_out = glLedLinkDegraded;

    if (glLedFatal)
        return LED_ST_FATAL;

    if (glLedHardStop)
        return LED_ST_FAULT;

    if (glIsApplnActive) {
        if (glCommitBufferFailureCount)
            return LED_ST_NO_FRAMES;
        if (glPipeArmed &&
            (uint32_t)(CyU3PGetTime () - glLastProdTimeMs) >= CY_FX_UVC_FRAME_TIMEOUT_MS)
            return LED_ST_NO_FRAMES;
        return LED_ST_STREAMING;
    }

    if (glIsConfigured)
        return LED_ST_READY;

    return glLedConnected ? LED_ST_NOHOST : LED_ST_BOOT;
}
#endif
