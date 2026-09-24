/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * CHC5 GigE Vision stream transmitter interface
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 */
#ifndef _GVSP_TX_H
#define _GVSP_TX_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define GVSP_MAX_FRAMES   5

#define GVSP_MAX_PAYLOAD  1464u

#define GVSP_PKT_LEADER   0x01
#define GVSP_PKT_TRAILER  0x02
#define GVSP_PKT_PAYLOAD  0x03

#define GVSP_PAYLOAD_IMAGE  0x0001

#define GVSP_PIX_MONO8          0x01080001
#define GVSP_PIX_MONO12         0x01100005
#define GVSP_PIX_MONO12P        0x010C0047
#define GVSP_PIX_BAYERRG8       0x01080009
#define GVSP_PIX_BAYERRG10      0x0110000D
#define GVSP_PIX_BAYERRG10P     0x010A0058
#define GVSP_PIX_BAYERRG12      0x01100011
#define GVSP_PIX_BAYERRG12P     0x010C0059
#define GVSP_PIX_BAYERRG14      0x0110010A
#define GVSP_PIX_BAYERRG14P     0x010E0106

#define GVSP_PIX_BAYERBG8       0x0108000B
#define GVSP_PIX_BAYERBG10      0x0110000F
#define GVSP_PIX_BAYERBG10P     0x010A0052
#define GVSP_PIX_BAYERBG12      0x01100013
#define GVSP_PIX_BAYERBG12P     0x010C0053
#define GVSP_PIX_BAYERBG14      0x0110010C
#define GVSP_PIX_BAYERBG14P     0x010E0108

#define GVSP_PIX_BAYERGR8       0x01080008
#define GVSP_PIX_BAYERGR10      0x0110000C
#define GVSP_PIX_BAYERGR10P     0x010A0056
#define GVSP_PIX_BAYERGR12      0x01100010
#define GVSP_PIX_BAYERGR12P     0x010C0057
#define GVSP_PIX_BAYERGR14      0x01100109
#define GVSP_PIX_BAYERGR14P     0x010E0105

#define GVSP_PIX_BAYERGB8       0x0108000A
#define GVSP_PIX_BAYERGB10      0x0110000E
#define GVSP_PIX_BAYERGB10P     0x010A0054
#define GVSP_PIX_BAYERGB12      0x01100012
#define GVSP_PIX_BAYERGB12P     0x010C0055
#define GVSP_PIX_BAYERGB14      0x0110010B
#define GVSP_PIX_BAYERGB14P     0x010E0107

#define GVSP_PIX_BAYERGR16      0x0110002E
#define GVSP_PIX_BAYERRG16      0x0110002F
#define GVSP_PIX_BAYERGB16      0x01100030
#define GVSP_PIX_BAYERBG16      0x01100031
#define GVSP_PIX_YUV422_8       0x02100032
#define GVSP_PIX_YCBCR422_8     0x0210003B
#define GVSP_PIX_RGB565P        0x02100035
#define GVSP_PIX_BGR565P        0x02100036
#define GVSP_PIX_RGB8           0x02180014
#define GVSP_PIX_BGR8           0x02180015
#define GVSP_PIX_BGRA8          0x02200017

#define GVSP_TS_CLOCK_MONOTONIC  0
#define GVSP_TS_CLOCK_PHC        1

struct gvsp_stream_cfg {
	__u32  dst_ip;
	__u16  dst_port;
	__u16  src_port;
	__u32  width;
	__u32  height;
	__u32  pixel_format;
	__u16  payload_size;
	char   ifname[16];
	__u16  startup_delay_ms;
	__u8   ts_clock;
	__u8   _pad[3];
};

struct gvsp_stats {
	__u64  frames_sent;
	__u64  frames_dropped;
	__u64  frames_skipped;
	__u64  packets_sent;
	__u64  bytes_sent;
	__u16  last_block_id;
	__u8   _pad[6];
};

#define GVSP_IOC_MAGIC      0x47
#define GVSP_IOC_START      _IOW(GVSP_IOC_MAGIC, 1, struct gvsp_stream_cfg)
#define GVSP_IOC_STOP       _IO (GVSP_IOC_MAGIC, 2)
#define GVSP_IOC_GET_STATS  _IOR(GVSP_IOC_MAGIC, 3, struct gvsp_stats)
#define GVSP_IOC_RESET      _IO (GVSP_IOC_MAGIC, 4)
#define GVSP_IOC_SET_TS_ZERO _IOW(GVSP_IOC_MAGIC, 5, __u64)

#endif
