// SPDX-License-Identifier: GPL-2.0-only
/*
 * CHC5 VDMA triple buffer
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 */
#include <linux/dma/xilinx_dma.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/mutex.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <uapi/linux/chc5-v4l2-controls.h>

#define CHC5_VDMA_BPP			2

#define CHC5_DDR_BLACK			0x8000

#define CHC5_TX_WIDTH			1920
#define CHC5_TX_HEIGHT			1080
#define CHC5_VDMA_MAX_FSTORES		32
#define CHC5_VDMA_DEF_FSTORES		3

#define CHC5_PAD_SINK			0
#define CHC5_PAD_SOURCE			1
#define CHC5_NUM_PADS			2

#define CHC5_DEF_WIDTH			1920
#define CHC5_DEF_HEIGHT			1080
#define CHC5_MIN_WIDTH			64
#define CHC5_MIN_HEIGHT			64
#define CHC5_DEF_MBUS_CODE		MEDIA_BUS_FMT_YUYV8_1X16

#define VDMA_MM2S_START_ADDR(n)		(0x005C + 4 * (n))
#define VDMA_S2MM_START_ADDR(n)		(0x00AC + 4 * (n))

struct chc5_vdma_dev {
	struct device		*dev;

	struct v4l2_subdev	subdev;
	struct media_pad	pads[CHC5_NUM_PADS];
	struct v4l2_mbus_framefmt formats[CHC5_NUM_PADS];

	struct dma_chan		*tx_chan;
	struct dma_chan		*rx_chan;
	unsigned int		num_fstores;

	void __iomem		*vdma_regs;

	void			*fbuf_vaddr;
	dma_addr_t		fbuf_paddr;
	size_t			fbuf_total;
	struct dma_interleaved_template *xt;

	bool			dma_running;
	u32			active_width;
	u32			active_height;

	struct v4l2_ctrl_handler ctrl_handler;
	bool			hdmi_enabled;
	bool			streaming;

	struct mutex		lock;
};

static inline struct chc5_vdma_dev *to_chc5(struct v4l2_subdev *sd)
{
	return container_of(sd, struct chc5_vdma_dev, subdev);
}

static void chc5_init_format(struct v4l2_mbus_framefmt *fmt)
{
	fmt->width	= CHC5_DEF_WIDTH;
	fmt->height	= CHC5_DEF_HEIGHT;
	fmt->code	= CHC5_DEF_MBUS_CODE;
	fmt->field	= V4L2_FIELD_NONE;
	fmt->colorspace	= V4L2_COLORSPACE_SRGB;
	fmt->xfer_func	= V4L2_XFER_FUNC_DEFAULT;
	fmt->ycbcr_enc	= V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_DEFAULT;
}

static void chc5_clamp_format(struct v4l2_mbus_framefmt *fmt,
			      u32 max_w, u32 max_h)
{
	if (fmt->width < CHC5_MIN_WIDTH)
		fmt->width = CHC5_MIN_WIDTH;
	if (max_w && fmt->width > max_w)
		fmt->width = max_w;

	if (fmt->height < CHC5_MIN_HEIGHT)
		fmt->height = CHC5_MIN_HEIGHT;
	if (max_h && fmt->height > max_h)
		fmt->height = max_h;

	if (fmt->code == 0)
		fmt->code = CHC5_DEF_MBUS_CODE;

	fmt->field = V4L2_FIELD_NONE;
}

static void chc5_fbuf_clear(struct chc5_vdma_dev *chc5)
{
	if (!chc5->fbuf_vaddr)
		return;

	memset16(chc5->fbuf_vaddr, CHC5_DDR_BLACK,
		 chc5->fbuf_total / sizeof(u16));
}

static int chc5_ensure_buffers(struct chc5_vdma_dev *chc5,
			       u32 rx_width, u32 rx_height)
{
	u32 stride, frame_h;
	size_t frame_sz, total;

	stride  = max_t(u32, rx_width, CHC5_TX_WIDTH) * CHC5_VDMA_BPP;
	frame_h = max_t(u32, rx_height, CHC5_TX_HEIGHT);
	frame_sz = (size_t)stride * frame_h;
	total    = frame_sz * chc5->num_fstores;

	if (total <= chc5->fbuf_total) {
		chc5_fbuf_clear(chc5);
		return 0;
	}

	if (chc5->fbuf_vaddr) {
		dma_free_coherent(chc5->dev, chc5->fbuf_total,
				  chc5->fbuf_vaddr, chc5->fbuf_paddr);
		chc5->fbuf_vaddr = NULL;
		chc5->fbuf_total = 0;
	}

	chc5->fbuf_vaddr = dma_alloc_coherent(chc5->dev, total,
					      &chc5->fbuf_paddr,
					      GFP_KERNEL);
	if (!chc5->fbuf_vaddr) {
		dev_err(chc5->dev,
			"dma_alloc_coherent %zu B failed for %ux%u\n",
			total, rx_width, rx_height);
		return -ENOMEM;
	}
	chc5->fbuf_total = total;

	chc5_fbuf_clear(chc5);

	dev_dbg(chc5->dev, "Allocated %zu B (%ux%u stride=%u fstores=%u)\n",
		 total, rx_width, rx_height, stride, chc5->num_fstores);

	return 0;
}

static int chc5_dma_submit(struct chc5_vdma_dev *chc5,
			   u32 rx_width, u32 rx_height, bool genlock)
{
	struct xilinx_vdma_config config;
	struct dma_interleaved_template *xt = chc5->xt;
	struct dma_device *tx_dev, *rx_dev;
	struct dma_async_tx_descriptor *txd, *rxd;
	dma_cookie_t cookie;
	enum dma_ctrl_flags flags;
	u32 tx_hsize, rx_hsize, stride, frame_sz;
	dma_addr_t tx_crop_off;
	int i;

	tx_hsize = CHC5_TX_WIDTH * CHC5_VDMA_BPP;
	rx_hsize = rx_width * CHC5_VDMA_BPP;
	stride   = max(tx_hsize, rx_hsize);
	frame_sz = stride * max_t(u32, rx_height, CHC5_TX_HEIGHT);

	{
		u32 x_off = (rx_width  > CHC5_TX_WIDTH)  ?
			    (rx_width  - CHC5_TX_WIDTH)  / 2 : 0;
		u32 y_off = (rx_height > CHC5_TX_HEIGHT) ?
			    (rx_height - CHC5_TX_HEIGHT) / 2 : 0;
		tx_crop_off = (dma_addr_t)y_off * stride +
			      (dma_addr_t)x_off * CHC5_VDMA_BPP;
	}

	memset(&config, 0, sizeof(config));
	config.frm_cnt_en = 0;
	config.coalesc    = 0;
	config.park       = 0;
	config.gen_lock   = genlock ? 1 : 0;

	if (xilinx_vdma_channel_set_config(chc5->tx_chan, &config)) {
		dev_err(chc5->dev, "vdma0 config failed\n");
		return -EIO;
	}
	if (xilinx_vdma_channel_set_config(chc5->rx_chan, &config)) {
		dev_err(chc5->dev, "vdma1 config failed\n");
		return -EIO;
	}

	tx_dev = chc5->tx_chan->device;
	rx_dev = chc5->rx_chan->device;
	flags  = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;

	xt->src_start   = chc5->fbuf_paddr + tx_crop_off;
	xt->dst_start   = 0;
	xt->dir         = DMA_MEM_TO_DEV;
	xt->numf        = CHC5_TX_HEIGHT;
	xt->sgl[0].size = tx_hsize;
	xt->sgl[0].icg  = stride - tx_hsize;
	xt->frame_size  = 1;

	txd = tx_dev->device_prep_interleaved_dma(chc5->tx_chan, xt, flags);
	if (!txd) {
		dev_err(chc5->dev, "TX prep failed\n");
		return -EIO;
	}
	cookie = txd->tx_submit(txd);
	if (dma_submit_error(cookie)) {
		dev_err(chc5->dev, "TX submit failed\n");
		return -EIO;
	}

	xt->src_start   = 0;
	xt->dst_start   = chc5->fbuf_paddr;
	xt->dir         = DMA_DEV_TO_MEM;
	xt->numf        = rx_height;
	xt->sgl[0].size = rx_hsize;
	xt->sgl[0].icg  = stride - rx_hsize;
	xt->frame_size  = 1;

	rxd = rx_dev->device_prep_interleaved_dma(chc5->rx_chan, xt, flags);
	if (!rxd) {
		dev_err(chc5->dev, "RX prep failed\n");
		return -EIO;
	}
	cookie = rxd->tx_submit(rxd);
	if (dma_submit_error(cookie)) {
		dev_err(chc5->dev, "RX submit failed\n");
		return -EIO;
	}

	if (chc5->vdma_regs) {
		for (i = 0; i < chc5->num_fstores; i++) {
			dma_addr_t base = chc5->fbuf_paddr +
					  (dma_addr_t)frame_sz * i;
			writel(base + tx_crop_off, chc5->vdma_regs +
			       VDMA_MM2S_START_ADDR(i));
			writel(base, chc5->vdma_regs +
			       VDMA_S2MM_START_ADDR(i));
		}
	}

	dma_async_issue_pending(chc5->rx_chan);
	dma_async_issue_pending(chc5->tx_chan);

	return 0;
}

static int chc5_dma_start(struct chc5_vdma_dev *chc5,
			  u32 width, u32 height)
{
	bool genlock = chc5->num_fstores >= 3;
	int ret;

	if (!chc5->fbuf_vaddr) {
		dev_err(chc5->dev,
			"No DMA buffers -- set_fmt not called?\n");
		return -ENOMEM;
	}

	ret = chc5_dma_submit(chc5, width, height, genlock);
	if (ret)
		return ret;

	chc5->active_width  = width;
	chc5->active_height = height;
	chc5->dma_running   = true;

	dev_dbg(chc5->dev, "VDMA running  TX=%ux%u  RX=%ux%u  genlock=%s\n",
		 CHC5_TX_WIDTH, CHC5_TX_HEIGHT, width, height,
		 genlock ? "on" : "off");

	return 0;
}

static void chc5_dma_stop(struct chc5_vdma_dev *chc5)
{
	if (!chc5->dma_running)
		return;

	dmaengine_terminate_sync(chc5->tx_chan);
	dmaengine_terminate_sync(chc5->rx_chan);

	chc5->dma_running = false;

	dev_dbg(chc5->dev, "VDMA stopped\n");
}

static int chc5_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct chc5_vdma_dev *chc5 =
		container_of(ctrl->handler, struct chc5_vdma_dev, ctrl_handler);
	u32 width, height;

	if (ctrl->id != CHC5_VDMA_CID_ENABLE)
		return -EINVAL;

	chc5->hdmi_enabled = !!ctrl->val;

	if (!chc5->streaming)
		return 0;

	if (chc5->hdmi_enabled) {
		if (chc5->dma_running)
			return 0;
		mutex_lock(&chc5->lock);
		width  = chc5->formats[CHC5_PAD_SINK].width;
		height = chc5->formats[CHC5_PAD_SINK].height;
		mutex_unlock(&chc5->lock);
		dev_dbg(chc5->dev, "ctrl: HDMI enable -> VDMA start %ux%u\n",
			width, height);
		return chc5_dma_start(chc5, width, height);
	}

	dev_dbg(chc5->dev, "ctrl: HDMI disable -> VDMA stop\n");
	chc5_dma_stop(chc5);
	return 0;
}

static const struct v4l2_ctrl_ops chc5_ctrl_ops = {
	.s_ctrl = chc5_s_ctrl,
};

static const struct v4l2_ctrl_config chc5_ctrl_hdmi_enable = {
	.ops	= &chc5_ctrl_ops,
	.id	= CHC5_VDMA_CID_ENABLE,
	.name	= "HDMI Output Enable",
	.type	= V4L2_CTRL_TYPE_BOOLEAN,
	.min	= 0,
	.max	= 1,
	.step	= 1,
	.def	= 1,
};

static int chc5_init_controls(struct chc5_vdma_dev *chc5)
{
	struct v4l2_ctrl_handler *hdl = &chc5->ctrl_handler;
	int ret;

	ret = v4l2_ctrl_handler_init(hdl, 1);
	if (ret)
		return ret;

	v4l2_ctrl_new_custom(hdl, &chc5_ctrl_hdmi_enable, NULL);
	if (hdl->error) {
		ret = hdl->error;
		v4l2_ctrl_handler_free(hdl);
		return ret;
	}

	chc5->subdev.ctrl_handler = hdl;
	return 0;
}

static int chc5_enum_mbus_code(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state,
			       struct v4l2_subdev_mbus_code_enum *code)
{
	struct chc5_vdma_dev *chc5 = to_chc5(sd);

	if (code->index > 0)
		return -EINVAL;

	mutex_lock(&chc5->lock);
	code->code = chc5->formats[code->pad].code;
	mutex_unlock(&chc5->lock);

	return 0;
}

static int chc5_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index > 0)
		return -EINVAL;

	fse->min_width  = CHC5_MIN_WIDTH;
	fse->min_height = CHC5_MIN_HEIGHT;

	if (fse->pad == CHC5_PAD_SINK) {
		fse->max_width  = U16_MAX;
		fse->max_height = U16_MAX;
	} else {
		fse->max_width  = CHC5_TX_WIDTH;
		fse->max_height = CHC5_TX_HEIGHT;
	}
	return 0;
}

static int chc5_get_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *fmt)
{
	struct chc5_vdma_dev *chc5 = to_chc5(sd);
	struct v4l2_mbus_framefmt *mbus_fmt;

	if (fmt->pad >= CHC5_NUM_PADS)
		return -EINVAL;

	mutex_lock(&chc5->lock);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mbus_fmt = v4l2_subdev_get_try_format(sd, state, fmt->pad);
		if (!mbus_fmt) {
			mutex_unlock(&chc5->lock);
			return -EINVAL;
		}
		fmt->format = *mbus_fmt;
	} else {
		fmt->format = chc5->formats[fmt->pad];
	}
	mutex_unlock(&chc5->lock);
	return 0;
}

static int chc5_set_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *fmt)
{
	struct chc5_vdma_dev *chc5 = to_chc5(sd);
	struct v4l2_mbus_framefmt *mbus_fmt;
	bool need_restart = false;

	if (fmt->pad >= CHC5_NUM_PADS)
		return -EINVAL;

	if (fmt->pad == CHC5_PAD_SINK)
		chc5_clamp_format(&fmt->format, 0, 0);
	else
		chc5_clamp_format(&fmt->format,
				  CHC5_TX_WIDTH, CHC5_TX_HEIGHT);

	mutex_lock(&chc5->lock);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mbus_fmt = v4l2_subdev_get_try_format(sd, state, fmt->pad);
		if (mbus_fmt)
			*mbus_fmt = fmt->format;
		mutex_unlock(&chc5->lock);
		return 0;
	}

	chc5->formats[fmt->pad] = fmt->format;

	if (fmt->pad == CHC5_PAD_SINK) {
		int alloc_ret;

		mutex_unlock(&chc5->lock);
		alloc_ret = chc5_ensure_buffers(chc5,
						fmt->format.width,
						fmt->format.height);
		if (alloc_ret)
			return alloc_ret;
		mutex_lock(&chc5->lock);
	}

	if (fmt->pad == CHC5_PAD_SINK &&
	    chc5->dma_running &&
	    (fmt->format.width  != chc5->active_width ||
	     fmt->format.height != chc5->active_height))
		need_restart = true;

	mutex_unlock(&chc5->lock);

	if (need_restart) {
		dev_dbg(chc5->dev, "resolution %ux%u -> %ux%u, restarting\n",
			 chc5->active_width, chc5->active_height,
			 fmt->format.width, fmt->format.height);
		chc5_dma_stop(chc5);
		return chc5_dma_start(chc5, fmt->format.width,
				      fmt->format.height);
	}

	return 0;
}

static int chc5_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *fmt;

	fmt = v4l2_subdev_get_try_format(sd, fh->state, CHC5_PAD_SINK);
	chc5_init_format(fmt);
	fmt = v4l2_subdev_get_try_format(sd, fh->state, CHC5_PAD_SOURCE);
	chc5_init_format(fmt);
	return 0;
}

static int chc5_enable_streams(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state,
			       u32 pad, u64 streams_mask)
{
	struct chc5_vdma_dev *chc5 = to_chc5(sd);
	u32 width, height;

	if (chc5->dma_running)
		return 0;

	mutex_lock(&chc5->lock);
	width  = chc5->formats[CHC5_PAD_SINK].width;
	height = chc5->formats[CHC5_PAD_SINK].height;
	mutex_unlock(&chc5->lock);

	dev_dbg(chc5->dev, "enable_streams: pad=%u mask=0x%llx RX=%ux%u\n",
		 pad, streams_mask, width, height);

	return chc5_dma_start(chc5, width, height);
}

static int chc5_disable_streams(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				u32 pad, u64 streams_mask)
{
	struct chc5_vdma_dev *chc5 = to_chc5(sd);

	if (!chc5->dma_running)
		return 0;

	chc5_dma_stop(chc5);
	dev_dbg(chc5->dev, "disable_streams: pad=%u mask=0x%llx\n",
		 pad, streams_mask);

	return 0;
}

static int chc5_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct chc5_vdma_dev *chc5 = to_chc5(sd);
	u32 width, height;

	if (enable) {
		chc5->streaming = true;

		if (!chc5->hdmi_enabled) {
			dev_dbg(chc5->dev,
				"s_stream(1): HDMI disabled - VDMA left stopped\n");
			return 0;
		}

		if (chc5->dma_running)
			return 0;

		mutex_lock(&chc5->lock);
		width  = chc5->formats[CHC5_PAD_SINK].width;
		height = chc5->formats[CHC5_PAD_SINK].height;
		mutex_unlock(&chc5->lock);

		dev_dbg(chc5->dev, "s_stream(1): RX=%ux%u  TX=%ux%u  fstores=%u\n",
			 width, height, CHC5_TX_WIDTH, CHC5_TX_HEIGHT,
			 chc5->num_fstores);

		return chc5_dma_start(chc5, width, height);
	} else {
		chc5->streaming = false;

		if (!chc5->dma_running)
			return 0;

		chc5_dma_stop(chc5);
		dev_dbg(chc5->dev, "s_stream(0)\n");
		return 0;
	}
}

static int chc5_log_status(struct v4l2_subdev *sd)
{
	struct chc5_vdma_dev *chc5 = to_chc5(sd);

	mutex_lock(&chc5->lock);
	dev_dbg(chc5->dev, "DMA: %s  active=%ux%u  fstores=%u\n",
		 chc5->dma_running ? "running" : "stopped",
		 chc5->active_width, chc5->active_height,
		 chc5->num_fstores);
	dev_dbg(chc5->dev, "Sink:   %ux%u code=0x%04x\n",
		 chc5->formats[CHC5_PAD_SINK].width,
		 chc5->formats[CHC5_PAD_SINK].height,
		 chc5->formats[CHC5_PAD_SINK].code);
	dev_dbg(chc5->dev, "Source: %ux%u code=0x%04x\n",
		 chc5->formats[CHC5_PAD_SOURCE].width,
		 chc5->formats[CHC5_PAD_SOURCE].height,
		 chc5->formats[CHC5_PAD_SOURCE].code);
	mutex_unlock(&chc5->lock);
	return 0;
}

static const struct v4l2_subdev_core_ops chc5_core_ops = {
	.log_status = chc5_log_status,
};

static const struct v4l2_subdev_video_ops chc5_video_ops = {
	.s_stream = chc5_s_stream,
};

static const struct v4l2_subdev_pad_ops chc5_pad_ops = {
	.enum_mbus_code   = chc5_enum_mbus_code,
	.enum_frame_size  = chc5_enum_frame_size,
	.get_fmt          = chc5_get_fmt,
	.set_fmt          = chc5_set_fmt,
	.enable_streams   = chc5_enable_streams,
	.disable_streams  = chc5_disable_streams,
};

static const struct v4l2_subdev_ops chc5_subdev_ops = {
	.core  = &chc5_core_ops,
	.video = &chc5_video_ops,
	.pad   = &chc5_pad_ops,
};

static const struct v4l2_subdev_internal_ops chc5_internal_ops = {
	.open = chc5_open,
};

static const struct media_entity_operations chc5_vdma_triple_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int chc5_vdma_triple_probe(struct platform_device *pdev)
{
	struct chc5_vdma_dev *chc5;
	struct v4l2_subdev *sd;
	int ret;

	chc5 = devm_kzalloc(&pdev->dev, sizeof(*chc5), GFP_KERNEL);
	if (!chc5)
		return -ENOMEM;

	chc5->dev = &pdev->dev;
	mutex_init(&chc5->lock);

	ret = of_property_read_u32(pdev->dev.of_node, "xlnx,num-fstores",
				   &chc5->num_fstores);
	if (ret || chc5->num_fstores == 0 ||
	    chc5->num_fstores > CHC5_VDMA_MAX_FSTORES)
		chc5->num_fstores = CHC5_VDMA_DEF_FSTORES;

	if (chc5->num_fstores < 3)
		dev_warn(&pdev->dev,
			 "fstores=%u < 3: genlock disabled\n",
			 chc5->num_fstores);

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	chc5->tx_chan = dma_request_chan(&pdev->dev, "vdma0");
	if (IS_ERR(chc5->tx_chan)) {
		ret = PTR_ERR(chc5->tx_chan);
		chc5->tx_chan = NULL;
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "no vdma0 (MM2S): %d\n", ret);
		return ret;
	}

	chc5->rx_chan = dma_request_chan(&pdev->dev, "vdma1");
	if (IS_ERR(chc5->rx_chan)) {
		ret = PTR_ERR(chc5->rx_chan);
		chc5->rx_chan = NULL;
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "no vdma1 (S2MM): %d\n", ret);
		goto err_free_tx;
	}

	{
		struct device_node *vdma_np;

		vdma_np = of_parse_phandle(pdev->dev.of_node, "dmas", 0);
		if (vdma_np) {
			struct resource res;

			if (of_address_to_resource(vdma_np, 0, &res) == 0) {
				chc5->vdma_regs = devm_ioremap(&pdev->dev,
							       res.start,
							       resource_size(&res));
				if (!chc5->vdma_regs)
					dev_warn(&pdev->dev,
						 "ioremap VDMA 0x%llx failed -- single frame store only\n",
						 (u64)res.start);
				else
					dev_dbg(&pdev->dev,
						"VDMA regs mapped at 0x%llx\n",
						(u64)res.start);
			}
			of_node_put(vdma_np);
		}
	}

	chc5->xt = kzalloc(sizeof(*chc5->xt) + sizeof(struct data_chunk),
			   GFP_KERNEL);
	if (!chc5->xt) {
		ret = -ENOMEM;
		goto err_free_rx;
	}

	dev_dbg(&pdev->dev,
		"tx(MM2S)=%s rx(S2MM)=%s fstores=%u (buffers deferred)\n",
		dma_chan_name(chc5->tx_chan),
		dma_chan_name(chc5->rx_chan),
		chc5->num_fstores);

	chc5_init_format(&chc5->formats[CHC5_PAD_SINK]);
	chc5_init_format(&chc5->formats[CHC5_PAD_SOURCE]);

	sd = &chc5->subdev;
	v4l2_subdev_init(sd, &chc5_subdev_ops);
	sd->internal_ops = &chc5_internal_ops;
	sd->dev = &pdev->dev;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "%s", dev_name(&pdev->dev));

	chc5->hdmi_enabled = true;
	ret = chc5_init_controls(chc5);
	if (ret) {
		dev_err(&pdev->dev, "control init failed: %d\n", ret);
		goto err_free_xt;
	}

	chc5->pads[CHC5_PAD_SINK].flags   = MEDIA_PAD_FL_SINK;
	chc5->pads[CHC5_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_ENC_CONV;
	sd->entity.ops = &chc5_vdma_triple_media_ops;

	ret = media_entity_pads_init(&sd->entity, CHC5_NUM_PADS, chc5->pads);
	if (ret)
		goto err_ctrl;

	ret = v4l2_async_register_subdev(sd);
	if (ret)
		goto err_entity;

	platform_set_drvdata(pdev, chc5);

	dev_info(&pdev->dev, "probed: %ux%u  fstores=%u\n",
		 CHC5_DEF_WIDTH, CHC5_DEF_HEIGHT, chc5->num_fstores);

	return 0;

err_entity:
	media_entity_cleanup(&sd->entity);
err_ctrl:
	v4l2_ctrl_handler_free(&chc5->ctrl_handler);
err_free_xt:
	kfree(chc5->xt);
err_free_rx:
	dma_release_channel(chc5->rx_chan);
err_free_tx:
	dma_release_channel(chc5->tx_chan);
	return ret;
}

static int chc5_vdma_triple_remove(struct platform_device *pdev)
{
	struct chc5_vdma_dev *chc5 = platform_get_drvdata(pdev);

	chc5_dma_stop(chc5);

	v4l2_async_unregister_subdev(&chc5->subdev);
	v4l2_ctrl_handler_free(&chc5->ctrl_handler);
	media_entity_cleanup(&chc5->subdev.entity);

	kfree(chc5->xt);
	if (chc5->fbuf_vaddr)
		dma_free_coherent(&pdev->dev, chc5->fbuf_total,
				  chc5->fbuf_vaddr, chc5->fbuf_paddr);

	dma_release_channel(chc5->rx_chan);
	dma_release_channel(chc5->tx_chan);
	mutex_destroy(&chc5->lock);

	dev_dbg(&pdev->dev, "removed\n");
	return 0;
}

static const struct of_device_id chc5_vdma_triple_of_ids[] = {
	{ .compatible = "circuitvalley,chc5-vdma-triple-1.0" },
	{}
};
MODULE_DEVICE_TABLE(of, chc5_vdma_triple_of_ids);

static struct platform_driver chc5_vdma_triple_driver = {
	.probe  = chc5_vdma_triple_probe,
	.remove = chc5_vdma_triple_remove,
	.driver = {
		.name           = "chc5-vdma-triple",
		.of_match_table = chc5_vdma_triple_of_ids,
	},
};
module_platform_driver(chc5_vdma_triple_driver);

MODULE_AUTHOR("Gaurav Singh <gauravsingh@circuitvalley.com>");
MODULE_DESCRIPTION("CHC5 VDMA triple buffer");
MODULE_LICENSE("GPL");
