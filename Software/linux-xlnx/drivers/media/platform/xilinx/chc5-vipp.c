// SPDX-License-Identifier: GPL-2.0-only
/*
 * CHC5 video pipeline
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>
#include <linux/delay.h>
#include <linux/of_graph.h>
#include <linux/fwnode.h>
#include <linux/property.h>
#include <media/v4l2-async.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>

#define CHC5_VIPP_MAX_PADS 16
#define CHC5_VIPP_MAX_PIPELINE_LEN 32
#define CHC5_VIPP_MAX_WALK_STACK 32
#define CHC5_VIPP_MAX_WALK_VISITS 512

#define CHC5_VIPP_MIN_WIDTH	32
#define CHC5_VIPP_MIN_HEIGHT	32
#define CHC5_VIPP_MAX_WIDTH	8192
#define CHC5_VIPP_MAX_HEIGHT	8192

struct chc5_vipp_fmt_info {
	u32 fourcc;
	u32 mbus_code;
	u8  bpp;
	u32 colorspace;
};

static const struct chc5_vipp_fmt_info chc5_vipp_formats[] = {
	{ V4L2_PIX_FMT_SRGGB8,  MEDIA_BUS_FMT_SRGGB8_1X8,   8,  V4L2_COLORSPACE_RAW },
	{ V4L2_PIX_FMT_SRGGB12, MEDIA_BUS_FMT_SRGGB12_1X12, 16, V4L2_COLORSPACE_RAW },
	{ V4L2_PIX_FMT_SGBRG12, MEDIA_BUS_FMT_SGBRG12_1X12, 16, V4L2_COLORSPACE_RAW },
	{ V4L2_PIX_FMT_SRGGB14, MEDIA_BUS_FMT_SRGGB14_1X14, 16, V4L2_COLORSPACE_RAW },
	{ V4L2_PIX_FMT_SGBRG14, MEDIA_BUS_FMT_SGBRG14_1X14, 16, V4L2_COLORSPACE_RAW },
	{ V4L2_PIX_FMT_SBGGR8,  MEDIA_BUS_FMT_SBGGR8_1X8,   8,  V4L2_COLORSPACE_RAW },
	{ V4L2_PIX_FMT_SGRBG8,  MEDIA_BUS_FMT_SGRBG8_1X8,   8,  V4L2_COLORSPACE_RAW },
	{ V4L2_PIX_FMT_SGBRG8,  MEDIA_BUS_FMT_SGBRG8_1X8,   8,  V4L2_COLORSPACE_RAW },
	{ V4L2_PIX_FMT_SBGGR12, MEDIA_BUS_FMT_SBGGR12_1X12, 16, V4L2_COLORSPACE_RAW },
	{ V4L2_PIX_FMT_SGRBG12, MEDIA_BUS_FMT_SGRBG12_1X12, 16, V4L2_COLORSPACE_RAW },
	{ V4L2_PIX_FMT_SBGGR14, MEDIA_BUS_FMT_SBGGR14_1X14, 16, V4L2_COLORSPACE_RAW },
	{ V4L2_PIX_FMT_SGRBG14, MEDIA_BUS_FMT_SGRBG14_1X14, 16, V4L2_COLORSPACE_RAW },
	{ V4L2_PIX_FMT_RGB565,  MEDIA_BUS_FMT_RGB565_1X16,  16, V4L2_COLORSPACE_SRGB },
	{ V4L2_PIX_FMT_YUYV,    MEDIA_BUS_FMT_YUYV8_1X16,   16, V4L2_COLORSPACE_SMPTE170M },
	{ V4L2_PIX_FMT_GREY,    MEDIA_BUS_FMT_Y8_1X8,        8,  V4L2_COLORSPACE_RAW },
	{ V4L2_PIX_FMT_Y012,    MEDIA_BUS_FMT_Y12_1X12,     12, V4L2_COLORSPACE_RAW },
	{ V4L2_PIX_FMT_Y12,     MEDIA_BUS_FMT_Y16_1X16,     16, V4L2_COLORSPACE_RAW },
};

#define CHC5_VIPP_NUM_FMTS ARRAY_SIZE(chc5_vipp_formats)

static const struct chc5_vipp_fmt_info *chc5_vipp_fmt_find(u32 fourcc)
{
	unsigned int i;
	for (i = 0; i < CHC5_VIPP_NUM_FMTS; i++)
		if (chc5_vipp_formats[i].fourcc == fourcc)
			return &chc5_vipp_formats[i];
	return NULL;
}

static const struct chc5_vipp_fmt_info *chc5_vipp_fmt_find_by_mbus(u32 mbus_code)
{
	unsigned int i;
	for (i = 0; i < CHC5_VIPP_NUM_FMTS; i++)
		if (chc5_vipp_formats[i].mbus_code == mbus_code)
			return &chc5_vipp_formats[i];
	return NULL;
}

struct chc5_vipp_dev {
	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	struct vb2_queue vb_vidq;
	struct v4l2_pix_format fmt;
	struct media_pad pads[CHC5_VIPP_MAX_PADS];
	unsigned int num_pads;
	struct media_device mdev;
	struct mutex lock;
	struct v4l2_fract interval;
	struct v4l2_async_notifier notifier;
};

struct chc5_vipp_pipeline_entry {
	struct v4l2_subdev *sd;
	int depth;
};

struct chc5_vipp_walk_item {
	struct media_entity *ent;
	unsigned int sink_pad;
	int depth;
};

static int chc5_vipp_collect_pipeline(struct chc5_vipp_dev *dev,
				      struct v4l2_subdev **out,
				      int *out_count)
{
	struct chc5_vipp_pipeline_entry entries[CHC5_VIPP_MAX_PIPELINE_LEN];
	struct chc5_vipp_walk_item stack[CHC5_VIPP_MAX_WALK_STACK];
	int num_entries = 0;
	int sp = 0;
	int visits = 0;
	unsigned int pad_idx;

	for (pad_idx = 0; pad_idx < dev->num_pads; pad_idx++) {
		if (sp >= CHC5_VIPP_MAX_WALK_STACK)
			break;
		stack[sp].ent      = &dev->vdev.entity;
		stack[sp].sink_pad = pad_idx;
		stack[sp].depth    = 0;
		sp++;
	}

	while (sp > 0) {
		struct chc5_vipp_walk_item item = stack[--sp];
		struct media_entity *ent = item.ent;
		struct media_link *link;
		struct media_entity *upstream = NULL;
		struct v4l2_subdev *sd;
		unsigned int p;
		int existing = -1;
		int i;

		if (++visits > CHC5_VIPP_MAX_WALK_VISITS) {
			dev_warn(dev->v4l2_dev.dev,
				 "pipeline walk truncated after %d steps -- graph loop?\n",
				 visits);
			break;
		}

		if (item.depth >= CHC5_VIPP_MAX_PIPELINE_LEN)
			continue;

		list_for_each_entry(link, &ent->links, list) {
			if (link->sink->entity == ent &&
			    link->sink->index == item.sink_pad &&
			    (link->flags & MEDIA_LNK_FL_ENABLED)) {
				upstream = link->source->entity;
				break;
			}
		}

		if (!upstream)
			continue;

		if (!is_media_entity_v4l2_subdev(upstream))
			continue;

		sd = media_entity_to_v4l2_subdev(upstream);

		for (i = 0; i < num_entries; i++) {
			if (entries[i].sd == sd) {
				existing = i;
				break;
			}
		}

		if (existing >= 0) {
			if (item.depth <= entries[existing].depth)
				continue;
			entries[existing].depth = item.depth;
		} else if (num_entries < CHC5_VIPP_MAX_PIPELINE_LEN) {
			entries[num_entries].sd    = sd;
			entries[num_entries].depth = item.depth;
			num_entries++;
		} else {
			dev_warn(dev->v4l2_dev.dev,
				 "pipeline longer than %d subdevs, '%s' dropped\n",
				 CHC5_VIPP_MAX_PIPELINE_LEN, sd->name);
			continue;
		}

		for (p = 0; p < upstream->num_pads; p++) {
			if (!(upstream->pads[p].flags & MEDIA_PAD_FL_SINK))
				continue;
			if (sp >= CHC5_VIPP_MAX_WALK_STACK) {
				dev_warn(dev->v4l2_dev.dev,
					 "pipeline walk stack full at '%s'\n",
					 sd->name);
				break;
			}
			stack[sp].ent      = upstream;
			stack[sp].sink_pad = p;
			stack[sp].depth    = item.depth + 1;
			sp++;
		}
	}

	{
		int i, j;

		for (i = 1; i < num_entries; i++) {
			struct chc5_vipp_pipeline_entry tmp = entries[i];

			j = i - 1;
			while (j >= 0 && entries[j].depth > tmp.depth) {
				entries[j + 1] = entries[j];
				j--;
			}
			entries[j + 1] = tmp;
		}
	}

	{
		int i;

		for (i = 0; i < num_entries; i++)
			out[i] = entries[i].sd;
	}

	*out_count = num_entries;
	return 0;
}

static int chc5_vipp_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
				 unsigned int *nplanes, unsigned int sizes[],
				 struct device *alloc_devs[])
{
	struct chc5_vipp_dev *dev = vb2_get_drv_priv(vq);
	unsigned int size = dev->fmt.sizeimage;

	if (*nplanes) {
		if (sizes[0] < size)
			return -EINVAL;
		return 0;
	}

	*nplanes = 1;
	sizes[0] = size;
	return 0;
}

static void chc5_vipp_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct chc5_vipp_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	void *buf = vb2_plane_vaddr(vb, 0);

	memset(buf, 0x00, dev->fmt.sizeimage);
	vbuf->vb2_buf.timestamp = ktime_get_ns();
	msleep(100);
	vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
}

static int chc5_vipp_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct chc5_vipp_dev *dev = vb2_get_drv_priv(vq);
	struct v4l2_subdev *pipeline[CHC5_VIPP_MAX_PIPELINE_LEN];
	int pipeline_len = 0;
	int i, ret;

	chc5_vipp_collect_pipeline(dev, pipeline, &pipeline_len);

	for (i = 0; i < pipeline_len; i++) {
		struct v4l2_subdev *sd = pipeline[i];

		dev_dbg(dev->v4l2_dev.dev, "s_power(1) + s_stream(1): [%d/%d] %s\n",
			i + 1, pipeline_len, sd->name);

		ret = v4l2_subdev_call(sd, core, s_power, 1);
		if (ret && ret != -ENOIOCTLCMD) {
			dev_err(dev->v4l2_dev.dev,
				"Failed to power on %s: %d\n", sd->name, ret);
			goto err_rollback;
		}

		ret = v4l2_subdev_call(sd, video, s_stream, 1);
		if (ret && ret != -ENOIOCTLCMD) {
			dev_err(dev->v4l2_dev.dev,
				"Failed to start %s: %d\n", sd->name, ret);
			v4l2_subdev_call(sd, core, s_power, 0);
			goto err_rollback;
		}
	}

	dev_dbg(dev->v4l2_dev.dev,
		 "Streaming started (%d subdevs, %u pads)\n",
		 pipeline_len, dev->num_pads);
	return 0;

err_rollback:
	for (i = i - 1; i >= 0; i--) {
		dev_dbg(dev->v4l2_dev.dev, "rollback s_stream(0) + s_power(0): %s\n",
			 pipeline[i]->name);
		v4l2_subdev_call(pipeline[i], video, s_stream, 0);
		v4l2_subdev_call(pipeline[i], core, s_power, 0);
	}
	{
		struct vb2_buffer *vb;
		list_for_each_entry(vb, &vq->queued_list, queued_entry)
			vb2_buffer_done(vb, VB2_BUF_STATE_QUEUED);
	}
	return ret;
}

static void chc5_vipp_stop_streaming(struct vb2_queue *vq)
{
	struct chc5_vipp_dev *dev = vb2_get_drv_priv(vq);
	struct v4l2_subdev *pipeline[CHC5_VIPP_MAX_PIPELINE_LEN];
	int pipeline_len = 0;
	int i;

	chc5_vipp_collect_pipeline(dev, pipeline, &pipeline_len);

	for (i = pipeline_len - 1; i >= 0; i--) {
		dev_dbg(dev->v4l2_dev.dev, "s_stream(0) + s_power(0): [%d/%d] %s\n",
			 pipeline_len - i, pipeline_len, pipeline[i]->name);

		v4l2_subdev_call(pipeline[i], video, s_stream, 0);
		v4l2_subdev_call(pipeline[i], core, s_power, 0);
	}

	dev_dbg(dev->v4l2_dev.dev, "Streaming stopped (%d subdevs).\n",
		 pipeline_len);
}

static int chc5_vipp_enum_fmt_vid_cap(struct file *file, void *priv,
				      struct v4l2_fmtdesc *f)
{
	if (f->index >= CHC5_VIPP_NUM_FMTS)
		return -EINVAL;
	f->pixelformat = chc5_vipp_formats[f->index].fourcc;
	return 0;
}

static int chc5_vipp_g_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_format *f)
{
	struct chc5_vipp_dev *dev = video_drvdata(file);
	f->fmt.pix = dev->fmt;
	return 0;
}

static int chc5_vipp_try_fmt_vid_cap(struct file *file, void *priv,
				     struct v4l2_format *f)
{
	struct v4l2_pix_format *pix = &f->fmt.pix;
	const struct chc5_vipp_fmt_info *info;

	info = chc5_vipp_fmt_find(pix->pixelformat);
	if (!info) {
		info = &chc5_vipp_formats[0];
		pix->pixelformat = info->fourcc;
	}
	pix->width  = clamp_t(u32, pix->width,  CHC5_VIPP_MIN_WIDTH,
			      CHC5_VIPP_MAX_WIDTH);
	pix->height = clamp_t(u32, pix->height, CHC5_VIPP_MIN_HEIGHT,
			      CHC5_VIPP_MAX_HEIGHT);

	pix->bytesperline = pix->width * info->bpp / 8;
	pix->sizeimage    = pix->bytesperline * pix->height;
	pix->field        = V4L2_FIELD_NONE;
	pix->colorspace   = info->colorspace;
	return 0;
}

static int chc5_vipp_s_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_format *f)
{
	struct chc5_vipp_dev *dev = video_drvdata(file);
	int ret;

	if (vb2_is_busy(&dev->vb_vidq))
		return -EBUSY;
	ret = chc5_vipp_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;
	dev->fmt = f->fmt.pix;
	return 0;
}

static int chc5_vipp_enum_framesizes(struct file *file, void *priv,
				     struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index > 0)
		return -EINVAL;
	if (!chc5_vipp_fmt_find(fsize->pixel_format))
		return -EINVAL;
	fsize->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;
	fsize->stepwise.min_width   = CHC5_VIPP_MIN_WIDTH;
	fsize->stepwise.max_width   = CHC5_VIPP_MAX_WIDTH;
	fsize->stepwise.step_width  = 1;
	fsize->stepwise.min_height  = CHC5_VIPP_MIN_HEIGHT;
	fsize->stepwise.max_height  = CHC5_VIPP_MAX_HEIGHT;
	fsize->stepwise.step_height = 1;
	return 0;
}

static const struct vb2_ops chc5_vipp_qops = {
	.queue_setup     = chc5_vipp_queue_setup,
	.buf_queue       = chc5_vipp_buf_queue,
	.wait_prepare    = vb2_ops_wait_prepare,
	.wait_finish     = vb2_ops_wait_finish,
	.start_streaming = chc5_vipp_start_streaming,
	.stop_streaming  = chc5_vipp_stop_streaming,
};

static int chc5_vipp_enum_frameintervals(struct file *file, void *priv,
					 struct v4l2_frmivalenum *fival)
{
	if (fival->index > 0) return -EINVAL;
	fival->type = V4L2_FRMIVAL_TYPE_CONTINUOUS;
	fival->stepwise.min.numerator   = 1;
	fival->stepwise.min.denominator = 2000;
	fival->stepwise.max.numerator   = 1;
	fival->stepwise.max.denominator = 1;
	fival->stepwise.step.numerator  = 1;
	fival->stepwise.step.denominator = 2000;
	return 0;
}

static int chc5_vipp_g_parm(struct file *file, void *priv,
			    struct v4l2_streamparm *parm)
{
	struct chc5_vipp_dev *dev = video_drvdata(file);
	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) return -EINVAL;
	parm->parm.capture.capability   = V4L2_CAP_TIMEPERFRAME;
	parm->parm.capture.timeperframe = dev->interval;
	return 0;
}

static int chc5_vipp_s_parm(struct file *file, void *priv,
			    struct v4l2_streamparm *parm)
{
	struct chc5_vipp_dev *dev = video_drvdata(file);
	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) return -EINVAL;
	if (parm->parm.capture.timeperframe.denominator > 2000)
		parm->parm.capture.timeperframe.denominator = 2000;
	dev->interval = parm->parm.capture.timeperframe;
	return 0;
}

struct chc5_vipp_graph_entity {
	struct v4l2_async_connection asd;
	struct media_entity *entity;
	struct v4l2_subdev *subdev;
};

#define CHC5_VIPP_MAX_PORTS_PER_NODE 8

static struct chc5_vipp_graph_entity *
chc5_vipp_graph_entity_find(struct chc5_vipp_dev *dev,
			    struct fwnode_handle *fwnode)
{
	struct v4l2_async_connection *asd;

	list_for_each_entry(asd, &dev->notifier.waiting_list, asc_entry) {
		struct chc5_vipp_graph_entity *entity =
			container_of(asd, struct chc5_vipp_graph_entity, asd);
		if (entity->asd.match.fwnode == fwnode)
			return entity;
	}
	list_for_each_entry(asd, &dev->notifier.done_list, asc_entry) {
		struct chc5_vipp_graph_entity *entity =
			container_of(asd, struct chc5_vipp_graph_entity, asd);
		if (entity->asd.match.fwnode == fwnode)
			return entity;
	}
	return NULL;
}

static bool chc5_vipp_entity_ok(struct device *dev,
				struct media_entity *ent,
				const char *label)
{
	if (!ent) {
		dev_err(dev, "link: %s entity is NULL\n", label);
		return false;
	}
	if (!ent->num_pads || !ent->pads) {
		dev_err(dev, "link: %s entity '%s' has no pads\n",
			label, ent->name);
		return false;
	}
	if (!ent->links.next || !ent->links.prev) {
		dev_err(dev,
			"link: %s entity '%s' links list uninitialised "
			"(next=%p prev=%p)\n",
			label, ent->name, ent->links.next, ent->links.prev);
		return false;
	}
	return true;
}

static int chc5_vipp_graph_build_one(struct chc5_vipp_dev *dev,
				     struct chc5_vipp_graph_entity *entity)
{
	struct media_entity *local = entity->entity;
	unsigned int port_idx;
	int ret;

	for (port_idx = 0; port_idx < CHC5_VIPP_MAX_PORTS_PER_NODE; port_idx++) {
		struct fwnode_handle *ep, *remote_ep, *remote_fwnode;
		struct fwnode_endpoint remote_fwe;
		u32 remote_port;

		ep = fwnode_graph_get_endpoint_by_id(entity->asd.match.fwnode,
						     port_idx, 0, 0);
		if (!ep)
			continue;

		if (port_idx >= local->num_pads ||
		    !(local->pads[port_idx].flags & MEDIA_PAD_FL_SOURCE)) {
			fwnode_handle_put(ep);
			continue;
		}

		remote_ep = fwnode_graph_get_remote_endpoint(ep);
		fwnode_handle_put(ep);
		if (!remote_ep)
			continue;

		ret = fwnode_graph_parse_endpoint(remote_ep, &remote_fwe);
		if (ret) {
			fwnode_handle_put(remote_ep);
			continue;
		}
		remote_port = remote_fwe.port;

		remote_fwnode = fwnode_graph_get_port_parent(remote_ep);
		fwnode_handle_put(remote_ep);
		if (!remote_fwnode)
			continue;

		if (remote_fwnode == dev_fwnode(dev->v4l2_dev.dev)) {
			fwnode_handle_put(remote_fwnode);

			if (remote_port >= dev->num_pads)
				continue;
			if (!chc5_vipp_entity_ok(dev->v4l2_dev.dev,
						 local, "source") ||
			    !chc5_vipp_entity_ok(dev->v4l2_dev.dev,
						 &dev->vdev.entity, "sink(VIPP)"))
				continue;

			ret = media_create_pad_link(
				local, port_idx,
				&dev->vdev.entity, remote_port,
				MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE);
			if (ret)
				return ret;

			dev_dbg(dev->v4l2_dev.dev, "Linked %s:%u -> VIPP:%u\n",
				local->name, port_idx, remote_port);
		} else {
			struct chc5_vipp_graph_entity *remote_entity;

			remote_entity = chc5_vipp_graph_entity_find(dev,
								    remote_fwnode);
			fwnode_handle_put(remote_fwnode);

			if (!remote_entity || !remote_entity->entity)
				continue;
			if (remote_port >= remote_entity->entity->num_pads)
				continue;
			if (!chc5_vipp_entity_ok(dev->v4l2_dev.dev,
						 local, "source") ||
			    !chc5_vipp_entity_ok(dev->v4l2_dev.dev,
						 remote_entity->entity, "sink"))
				continue;

			ret = media_create_pad_link(
				local, port_idx,
				remote_entity->entity, remote_port,
				MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE);
			if (ret)
				return ret;
		}
	}
	return 0;
}

static int chc5_vipp_graph_bound(struct v4l2_async_notifier *notifier,
				 struct v4l2_subdev *sd,
				 struct v4l2_async_connection *asd)
{
	struct chc5_vipp_graph_entity *entity =
		container_of(asd, struct chc5_vipp_graph_entity, asd);

	entity->entity = &sd->entity;
	entity->subdev = sd;
	dev_dbg(sd->dev, "Subdev bound: %s\n", sd->name);
	return 0;
}

static int chc5_vipp_graph_complete(struct v4l2_async_notifier *notifier)
{
	struct chc5_vipp_dev *dev =
		container_of(notifier, struct chc5_vipp_dev, notifier);
	struct v4l2_async_connection *asd;
	int ret;

	dev_dbg(dev->v4l2_dev.dev,
		 "VIPP graph complete (%u sink pads)\n", dev->num_pads);

	list_for_each_entry(asd, &dev->notifier.done_list, asc_entry) {
		struct chc5_vipp_graph_entity *entity =
			container_of(asd, struct chc5_vipp_graph_entity, asd);

		if (!entity->subdev)
			continue;

		ret = chc5_vipp_graph_build_one(dev, entity);
		if (ret)
			return ret;
	}

	ret = v4l2_device_register_subdev_nodes(&dev->v4l2_dev);
	if (ret)
		return ret;

	return media_device_register(&dev->mdev);
}

static const struct v4l2_async_notifier_operations chc5_vipp_notifier_ops = {
	.bound    = chc5_vipp_graph_bound,
	.complete = chc5_vipp_graph_complete,
};

static const struct v4l2_file_operations chc5_vipp_fops = {
	.owner          = THIS_MODULE,
	.open           = v4l2_fh_open,
	.release        = vb2_fop_release,
	.unlocked_ioctl = video_ioctl2,
	.mmap           = vb2_fop_mmap,
	.poll           = vb2_fop_poll,
	.read           = vb2_fop_read,
};

static int chc5_vipp_querycap(struct file *file, void *priv,
			      struct v4l2_capability *cap)
{
	strscpy(cap->driver, "chc5_vipp", sizeof(cap->driver));
	strscpy(cap->card, "CHC5 VIPP Pipeline Manager", sizeof(cap->card));
	strscpy(cap->bus_info, "platform:chc5-vipp", sizeof(cap->bus_info));
	return 0;
}

static const struct v4l2_ioctl_ops chc5_vipp_ioctl_ops = {
	.vidioc_querycap            = chc5_vipp_querycap,
	.vidioc_enum_frameintervals = chc5_vipp_enum_frameintervals,
	.vidioc_enum_framesizes     = chc5_vipp_enum_framesizes,
	.vidioc_g_parm              = chc5_vipp_g_parm,
	.vidioc_s_parm              = chc5_vipp_s_parm,
	.vidioc_reqbufs             = vb2_ioctl_reqbufs,
	.vidioc_querybuf            = vb2_ioctl_querybuf,
	.vidioc_qbuf                = vb2_ioctl_qbuf,
	.vidioc_dqbuf               = vb2_ioctl_dqbuf,
	.vidioc_streamon            = vb2_ioctl_streamon,
	.vidioc_streamoff           = vb2_ioctl_streamoff,
	.vidioc_enum_fmt_vid_cap    = chc5_vipp_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap       = chc5_vipp_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap       = chc5_vipp_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap     = chc5_vipp_try_fmt_vid_cap,
};

static int chc5_vipp_graph_parse(struct chc5_vipp_dev *dev)
{
	struct fwnode_handle *queue[32];
	int queue_start = 0, queue_end = 0;
	int ret = 0;

	queue[queue_end++] = dev_fwnode(dev->v4l2_dev.dev);

	while (queue_start < queue_end) {
		struct fwnode_handle *node = queue[queue_start++];
		unsigned int port_idx;

		for (port_idx = 0; port_idx < CHC5_VIPP_MAX_PORTS_PER_NODE; port_idx++) {
			struct fwnode_handle *ep, *remote;
			struct chc5_vipp_graph_entity *entity;
			bool in_queue;
			int i;

			ep = fwnode_graph_get_endpoint_by_id(node,
							     port_idx, 0, 0);
			if (!ep)
				continue;

			remote = fwnode_graph_get_remote_port_parent(ep);
			fwnode_handle_put(ep);
			if (!remote)
				continue;

			if (remote == dev_fwnode(dev->v4l2_dev.dev)) {
				fwnode_handle_put(remote);
				continue;
			}

			in_queue = false;
			for (i = 0; i < queue_end; i++) {
				if (queue[i] == remote) {
					in_queue = true;
					break;
				}
			}
			if (in_queue) {
				fwnode_handle_put(remote);
				continue;
			}

			entity = v4l2_async_nf_add_fwnode(
					&dev->notifier, remote,
					struct chc5_vipp_graph_entity);
			if (IS_ERR(entity)) {
				ret = PTR_ERR(entity);
				fwnode_handle_put(remote);
				return ret;
			}

			if (queue_end < ARRAY_SIZE(queue))
				queue[queue_end++] = remote;

			fwnode_handle_put(remote);
		}
	}

	return ret;
}

static unsigned int chc5_vipp_count_sink_pads(struct device *dev)
{
	struct fwnode_handle *ports, *port;
	unsigned int max_reg = 0;
	unsigned int count = 0;

	ports = device_get_named_child_node(dev, "ports");
	if (!ports) {
		dev_warn(dev, "No 'ports' node, defaulting to 1 sink pad\n");
		return 1;
	}

	fwnode_for_each_child_node(ports, port) {
		u32 reg;
		if (fwnode_property_read_u32(port, "reg", &reg))
			continue;
		count++;
		if (reg > max_reg)
			max_reg = reg;
	}

	fwnode_handle_put(ports);

	if (count == 0)
		return 1;

	dev_dbg(dev, "Found %u DT ports, max reg=%u -> %u sink pads\n",
		 count, max_reg, max_reg + 1);
	return max_reg + 1;
}

static int chc5_vipp_probe(struct platform_device *pdev)
{
	struct chc5_vipp_dev *chc5_vipp;
	unsigned int num_pads;
	unsigned int i;
	int ret;

	chc5_vipp = devm_kzalloc(&pdev->dev, sizeof(*chc5_vipp), GFP_KERNEL);
	if (!chc5_vipp)
		return -ENOMEM;

	mutex_init(&chc5_vipp->lock);

	num_pads = chc5_vipp_count_sink_pads(&pdev->dev);
	if (num_pads > CHC5_VIPP_MAX_PADS) {
		ret = -EINVAL;
		goto err_mutex;
	}
	chc5_vipp->num_pads = num_pads;

	chc5_vipp->fmt.width        = 640;
	chc5_vipp->fmt.height       = 480;
	chc5_vipp->fmt.pixelformat  = chc5_vipp_formats[0].fourcc;
	chc5_vipp->fmt.field        = V4L2_FIELD_NONE;
	chc5_vipp->fmt.bytesperline = 640 * chc5_vipp_formats[0].bpp / 8;
	chc5_vipp->fmt.sizeimage    = chc5_vipp->fmt.bytesperline * 480;
	chc5_vipp->fmt.colorspace   = chc5_vipp_formats[0].colorspace;
	chc5_vipp->interval.numerator   = 1;
	chc5_vipp->interval.denominator = 1;

	chc5_vipp->vb_vidq.type             = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	chc5_vipp->vb_vidq.io_modes         = VB2_MMAP | VB2_READ;
	chc5_vipp->vb_vidq.drv_priv         = chc5_vipp;
	chc5_vipp->vb_vidq.buf_struct_size  = sizeof(struct vb2_v4l2_buffer);
	chc5_vipp->vb_vidq.ops              = &chc5_vipp_qops;
	chc5_vipp->vb_vidq.mem_ops          = &vb2_vmalloc_memops;
	chc5_vipp->vb_vidq.timestamp_flags  = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	chc5_vipp->vb_vidq.lock             = &chc5_vipp->lock;
	chc5_vipp->vb_vidq.min_buffers_needed = 1;
	chc5_vipp->vb_vidq.dev              = &pdev->dev;

	ret = vb2_queue_init(&chc5_vipp->vb_vidq);
	if (ret)
		goto err_mutex;

	chc5_vipp->mdev.dev = &pdev->dev;
	strscpy(chc5_vipp->mdev.model, "CHC5 VIPP",
		sizeof(chc5_vipp->mdev.model));
	media_device_init(&chc5_vipp->mdev);

	chc5_vipp->v4l2_dev.mdev = &chc5_vipp->mdev;
	ret = v4l2_device_register(&pdev->dev, &chc5_vipp->v4l2_dev);
	if (ret)
		goto err_mdev;

	for (i = 0; i < num_pads; i++)
		chc5_vipp->pads[i].flags = MEDIA_PAD_FL_SINK;

	chc5_vipp->vdev = (struct video_device) {
		.name        = "chc5-vipp",
		.v4l2_dev    = &chc5_vipp->v4l2_dev,
		.fops        = &chc5_vipp_fops,
		.ioctl_ops   = &chc5_vipp_ioctl_ops,
		.release     = video_device_release_empty,
		.lock        = &chc5_vipp->lock,
		.queue       = &chc5_vipp->vb_vidq,
		.device_caps = V4L2_CAP_VIDEO_CAPTURE |
			       V4L2_CAP_STREAMING |
			       V4L2_CAP_DEVICE_CAPS,
	};
	video_set_drvdata(&chc5_vipp->vdev, chc5_vipp);

	chc5_vipp->vdev.entity.function = MEDIA_ENT_F_IO_V4L;
	ret = media_entity_pads_init(&chc5_vipp->vdev.entity,
				     num_pads, chc5_vipp->pads);
	if (ret)
		goto err_v4l2;

	v4l2_async_nf_init(&chc5_vipp->notifier, &chc5_vipp->v4l2_dev);
	chc5_vipp->notifier.ops = &chc5_vipp_notifier_ops;

	ret = chc5_vipp_graph_parse(chc5_vipp);
	if (ret)
		goto err_nf_cleanup;

	ret = video_register_device(&chc5_vipp->vdev, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto err_nf_cleanup;

	ret = v4l2_async_nf_register(&chc5_vipp->notifier);
	if (ret)
		goto err_vdev;

	platform_set_drvdata(pdev, chc5_vipp);
	return 0;

err_vdev:
	video_unregister_device(&chc5_vipp->vdev);
err_nf_cleanup:
	v4l2_async_nf_cleanup(&chc5_vipp->notifier);
	media_entity_cleanup(&chc5_vipp->vdev.entity);
err_v4l2:
	v4l2_device_unregister(&chc5_vipp->v4l2_dev);
err_mdev:
	media_device_cleanup(&chc5_vipp->mdev);
err_mutex:
	mutex_destroy(&chc5_vipp->lock);
	return ret;
}

static int chc5_vipp_remove(struct platform_device *pdev)
{
	struct chc5_vipp_dev *chc5_vipp = platform_get_drvdata(pdev);

	v4l2_async_nf_unregister(&chc5_vipp->notifier);
	v4l2_async_nf_cleanup(&chc5_vipp->notifier);
	media_device_unregister(&chc5_vipp->mdev);
	video_unregister_device(&chc5_vipp->vdev);
	media_entity_cleanup(&chc5_vipp->vdev.entity);
	v4l2_device_unregister(&chc5_vipp->v4l2_dev);
	media_device_cleanup(&chc5_vipp->mdev);
	mutex_destroy(&chc5_vipp->lock);
	return 0;
}

static const struct of_device_id chc5_vipp_dt_ids[] = {
	{ .compatible = "circuitvalley,chc5_vipp-video" },
	{  }
};
MODULE_DEVICE_TABLE(of, chc5_vipp_dt_ids);

static struct platform_driver chc5_vipp_driver = {
	.probe  = chc5_vipp_probe,
	.remove = chc5_vipp_remove,
	.driver = {
		.name           = "chc5_vipp-video",
		.of_match_table = chc5_vipp_dt_ids,
	},
};

module_platform_driver(chc5_vipp_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CHC5 video pipeline");
MODULE_AUTHOR("Gaurav Singh <gauravsingh@circuitvalley.com>");
