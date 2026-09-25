// SPDX-License-Identifier: CC-BY-NC-ND-4.0
/*
 * media_pipeline.c - Media controller discovery & V4L2 pipeline config
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#include <string.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <linux/media.h>
#include <linux/v4l2-subdev.h>
#include <linux/v4l2-mediabus.h>
#include <linux/videodev2.h>

#ifndef V4L2_SEL_TGT_CROP
#define V4L2_SEL_TGT_CROP      0x0000
#endif
#ifndef V4L2_SEL_TGT_COMPOSE
#define V4L2_SEL_TGT_COMPOSE   0x0100
#endif

#include "media_pipeline.h"
#include "config.h"
#include "genconv.h"

#define ENT_SENSOR       0
#define ENT_CSI2RX       1
#define ENT_BLKC         2
#define ENT_DEMOSAIC     3
#define ENT_GAMMA        4
#define ENT_CSC          5
#define ENT_VTIMING      6
#define ENT_XVSUB        7
#define ENT_VIPP         8
#define ENT_PIXPACK_ETH  9
#define ENT_PIXPACK_USB 10
#define ENT_VDMA        11
#define ENT_CCM         12
#define ENT_WBG         13
#define ENT_AESTATS     14
#define ENT_CROP        15
#define ENT_COUNT       16

static const char *ent_labels[ENT_COUNT] = {
    "sensor", "csi2rx", "blkc", "demosaic", "gamma", "csc", "vtiming",
    "xvsub", "vipp", "pp_eth", "pp_usb", "vdma", "ccm", "wbg", "aestats",
    "crop"
};

static int graph_slot_fd(int slot);
static struct graph_entity *graph_slot_ent(int slot);
static int g_media_fd = -1;
static bool g_stream_buffers_setup = false;

struct sensor_params {
    uint64_t pixel_rate;
    int32_t  gain_min;
    int32_t  gain_max;
    int32_t  bl_min;
    int32_t  bl_max;
    int32_t  bl_default;

    uint32_t line_length;
    uint32_t active_width;
    uint32_t active_height;
    int32_t  hblank_min;
    int32_t  hblank_max;
    int32_t  vblank_min;
    int32_t  vblank_max;
    int32_t  exposure_min;
    int32_t  exposure_max;
    uint32_t exposure_offset;

    int32_t  ob_left, ob_top, ob_right, ob_bottom;

    int32_t  ob_on_image_dt;

    bool     fixed_valid;
    bool     mode_valid;
};

static struct sensor_params g_sensor = {0};

static uint32_t g_sensor_raw_mbus = 0x3012;

static uint32_t g_prog_width, g_prog_height;

static uint32_t g_wire_w, g_wire_h;

static struct {
    int32_t  vblank;
    int32_t  hblank;
    int32_t  exposure;
    int32_t  gain_code;
    int32_t  test_pattern;
    int32_t  black_level;
    int32_t  xvsub_num;
    int32_t  xvsub_den;
    uint16_t xvsub_hdmi_max;
    uint32_t xvsub_vblank;
    int32_t  xvsub_hblank;
    int32_t  hdmi_enabled;
    bool     valid;
} g_ctrl_cache = { .hdmi_enabled = -1, .valid = false };

static pthread_mutex_t g_sensor_lock = PTHREAD_MUTEX_INITIALIZER;

static float g_actual_fps = 0.0f;
float media_pipeline_get_actual_fps(void) { return g_actual_fps; }

static float g_max_fps = 0.0f;
float media_pipeline_get_max_fps(void) { return g_max_fps; }

static int32_t gain_db_to_code(double gain_db, int32_t code_min, int32_t code_max)
{
    double code = gain_db * GAIN_DB10_PER_DB;

    if (code < (double)code_min) code = (double)code_min;
    if (code > (double)code_max) code = (double)code_max;
    return (int32_t)(code + 0.5);
}

static int32_t gain_code_max(void)
{
    if (g_sensor.fixed_valid && g_sensor.gain_max > 0)
        return g_sensor.gain_max;

    return (int32_t)(GAIN_DB_FALLBACK * GAIN_DB10_PER_DB + 0.5);
}

static int find_dev_path(unsigned int dev_major, unsigned int dev_minor,
                         char *path, size_t pathlen)
{
    for (int i = 0; i < 64; i++) {
        char candidate[64];
        struct stat st;
        snprintf(candidate, sizeof(candidate), "/dev/v4l-subdev%d", i);
        if (stat(candidate, &st) == 0 && S_ISCHR(st.st_mode) &&
            major(st.st_rdev) == dev_major &&
            minor(st.st_rdev) == dev_minor) {
            snprintf(path, pathlen, "%s", candidate);
            return 0;
        }
    }
    for (int i = 0; i < 64; i++) {
        char candidate[64];
        struct stat st;
        snprintf(candidate, sizeof(candidate), "/dev/video%d", i);
        if (stat(candidate, &st) == 0 && S_ISCHR(st.st_mode) &&
            major(st.st_rdev) == dev_major &&
            minor(st.st_rdev) == dev_minor) {
            snprintf(path, pathlen, "%s", candidate);
            return 0;
        }
    }
    return -1;
}

static int subdev_get_ctrl(int fd, uint32_t id, int32_t *value)
{
    if (fd < 0) return -1;
    struct v4l2_control ctrl;
    ctrl.id = id;
    if (ioctl(fd, VIDIOC_G_CTRL, &ctrl) < 0)
        return -1;
    *value = ctrl.value;
    return 0;
}

static int subdev_get_ctrl64(int fd, uint32_t id, int64_t *value)
{
    if (fd < 0) return -1;
    struct v4l2_ext_control ctrl;
    struct v4l2_ext_controls ctrls;
    memset(&ctrl, 0, sizeof(ctrl));
    memset(&ctrls, 0, sizeof(ctrls));
    ctrl.id = id;
    ctrls.count = 1;
    ctrls.controls = &ctrl;
    ctrls.ctrl_class = V4L2_CTRL_ID2CLASS(id);
    if (ioctl(fd, VIDIOC_G_EXT_CTRLS, &ctrls) < 0)
        return -1;
    *value = ctrl.value64;
    return 0;
}

static int subdev_query_ctrl(int fd, uint32_t id,
                             int32_t *out_min, int32_t *out_max,
                             int32_t *out_default)
{
    if (fd < 0) return -1;
    struct v4l2_queryctrl qc;
    memset(&qc, 0, sizeof(qc));
    qc.id = id;
    if (ioctl(fd, VIDIOC_QUERYCTRL, &qc) < 0)
        return -1;
    if (out_min)     *out_min = qc.minimum;
    if (out_max)     *out_max = qc.maximum;
    if (out_default) *out_default = qc.default_value;
    return 0;
}

static void sensor_query_fixed_params(void)
{
    int fd = graph_slot_fd(ENT_SENSOR);
    if (fd < 0) return;

    memset(&g_sensor, 0, sizeof(g_sensor));

    int64_t pr = 0;
    if (subdev_get_ctrl64(fd, V4L2_CID_PIXEL_RATE, &pr) == 0 && pr > 0) {
        g_sensor.pixel_rate = (uint64_t)pr;
    } else {
        g_sensor.pixel_rate = 840000000ULL;
        LOG_ERR("sensor: PIXEL_RATE unreadable -- falling back to %llu, which "
                "is IMX477's rate and is almost certainly WRONG for this part; "
                "every exposure conversion will be mistimed",
                (unsigned long long)g_sensor.pixel_rate);
    }

    if (subdev_query_ctrl(fd, V4L2_CID_ANALOGUE_GAIN,
                          &g_sensor.gain_min, &g_sensor.gain_max, NULL) < 0) {
        g_sensor.gain_min = 0;
        g_sensor.gain_max = (int32_t)(GAIN_DB_FALLBACK * GAIN_DB10_PER_DB + 0.5);
        LOG_WARN("sensor: cannot query ANALOGUE_GAIN range, using fallback 0-%d "
                 "(0.1 dB units)", g_sensor.gain_max);
    }

    if (subdev_query_ctrl(fd, CHC5_CID_BLACK_LEVEL,
                          &g_sensor.bl_min, &g_sensor.bl_max,
                          &g_sensor.bl_default) < 0) {
        g_sensor.bl_min = 0;
        g_sensor.bl_max = 4095;
        g_sensor.bl_default = 0;
        LOG_WARN("sensor: no BLACK_LEVEL control -- assuming no pedestal; "
                 "blkc will subtract nothing");
    }

    g_sensor.fixed_valid = true;

    LOG_INFO("sensor: pixel_rate=%llu gain=[%d..%d] black_level=[%d..%d] def=%d",
             (unsigned long long)g_sensor.pixel_rate,
             g_sensor.gain_min, g_sensor.gain_max,
             g_sensor.bl_min, g_sensor.bl_max, g_sensor.bl_default);
}

static void sensor_query_ob(void)
{
    int fd = graph_slot_fd(ENT_SENSOR);

    g_sensor.ob_left = g_sensor.ob_top = 0;
    g_sensor.ob_right = g_sensor.ob_bottom = 0;
    g_sensor.ob_on_image_dt = 0;

    if (fd < 0)
        return;

    (void)subdev_get_ctrl(fd, CHC5SENSOR_CID_OB_LEFT,   &g_sensor.ob_left);
    (void)subdev_get_ctrl(fd, CHC5SENSOR_CID_OB_TOP,    &g_sensor.ob_top);
    (void)subdev_get_ctrl(fd, CHC5SENSOR_CID_OB_RIGHT,  &g_sensor.ob_right);
    (void)subdev_get_ctrl(fd, CHC5SENSOR_CID_OB_BOTTOM, &g_sensor.ob_bottom);
    (void)subdev_get_ctrl(fd, CHC5SENSOR_CID_OB_ON_IMAGE_DT,
                          &g_sensor.ob_on_image_dt);

    if (g_sensor.ob_left   < 0) g_sensor.ob_left   = 0;
    if (g_sensor.ob_top    < 0) g_sensor.ob_top    = 0;
    if (g_sensor.ob_right  < 0) g_sensor.ob_right  = 0;
    if (g_sensor.ob_bottom < 0) g_sensor.ob_bottom = 0;

    if (g_sensor.ob_left || g_sensor.ob_top ||
        g_sensor.ob_right || g_sensor.ob_bottom)
        LOG_INFO("sensor: transmits optical black l=%d t=%d r=%d b=%d "
                 "(wire pixels, this mode), %s",
                 g_sensor.ob_left, g_sensor.ob_top,
                 g_sensor.ob_right, g_sensor.ob_bottom,
                 g_sensor.ob_on_image_dt
                     ? "on the IMAGE data type -- inside the reported format, "
                       "only a crop can remove it (a crop block, or a CSI-2 "
                       "receiver with its own crop)"
                     : "on its own data type -- NOT in the reported format, "
                       "the CSI-2 RX filter's business, not ours");
}

static uint32_t geom_correct(uint32_t want, uint32_t got)
{
    if (got > want) {
        uint32_t over = got - want;
        return (want > over) ? want - over : want;
    }
    return want + (want - got);
}

#define OB_CROP_UNPROBED  (-2)
static int g_ob_crop_slot = OB_CROP_UNPROBED;

static int ob_crop_target(void)
{
    if (g_ob_crop_slot != OB_CROP_UNPROBED)
        return g_ob_crop_slot;

    g_ob_crop_slot = -1;
    if (graph_slot_fd(ENT_CROP) >= 0) {
        g_ob_crop_slot = ENT_CROP;
    } else {
        int fd = graph_slot_fd(ENT_CSI2RX);
        struct v4l2_subdev_selection sel;

        memset(&sel, 0, sizeof(sel));
        sel.which  = V4L2_SUBDEV_FORMAT_ACTIVE;
        sel.pad    = 0;
        sel.target = V4L2_SEL_TGT_CROP_BOUNDS;
        if (fd >= 0 && ioctl(fd, VIDIOC_SUBDEV_G_SELECTION, &sel) == 0)
            g_ob_crop_slot = ENT_CSI2RX;
    }
    LOG_INFO("media_pipeline: optical-black crop: %s",
             g_ob_crop_slot == ENT_CROP   ? "the crop block" :
             g_ob_crop_slot == ENT_CSI2RX ? "the CSI-2 receiver's own crop window" :
             "none on this fabric -- a band on the image data type stays in "
             "the picture");
    return g_ob_crop_slot;
}

static bool crop_can_strip_ob(void)
{
    return ob_crop_target() >= 0 &&
           g_sensor.ob_on_image_dt &&
           (g_sensor.ob_left || g_sensor.ob_top ||
            g_sensor.ob_right || g_sensor.ob_bottom);
}

static void sensor_query_mode_params(uint32_t width)
{
    int fd = graph_slot_fd(ENT_SENSOR);
    if (fd < 0) return;

    uint32_t height = 0;
    {
        struct v4l2_subdev_format sfmt;
        memset(&sfmt, 0, sizeof(sfmt));
        sfmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
        sfmt.pad   = 0;
        if (ioctl(fd, VIDIOC_SUBDEV_G_FMT, &sfmt) == 0 &&
            sfmt.format.width && sfmt.format.height) {
            width  = sfmt.format.width;
            height = sfmt.format.height;
        }
    }
    g_sensor.active_width  = width;
    g_sensor.active_height = height;

    {
        int64_t pr = 0;
        if (subdev_get_ctrl64(fd, V4L2_CID_PIXEL_RATE, &pr) == 0 && pr > 0) {
            if (g_sensor.fixed_valid && (uint64_t)pr != g_sensor.pixel_rate)
                LOG_INFO("sensor: pixel_rate %llu -> %llu at width %u",
                         (unsigned long long)g_sensor.pixel_rate,
                         (unsigned long long)pr, width);
            g_sensor.pixel_rate = (uint64_t)pr;
        } else {
            LOG_ERR("sensor: PIXEL_RATE unreadable at width %u -- exposure "
                    "timing will use the stale value %llu; every exposure "
                    "conversion from here is suspect", width,
                    (unsigned long long)g_sensor.pixel_rate);
        }
    }

    int32_t hblank = 0;
    if (subdev_get_ctrl(fd, V4L2_CID_HBLANK, &hblank) == 0) {
        g_sensor.line_length = width + (uint32_t)hblank;
    } else {
        g_sensor.line_length = (width > 2048) ? 0x5DC0 : 0x31C4;
        LOG_WARN("sensor: cannot read HBLANK, using fallback line_length=%u",
                 g_sensor.line_length);
    }

    if (subdev_query_ctrl(fd, V4L2_CID_HBLANK,
                          &g_sensor.hblank_min, &g_sensor.hblank_max, NULL) < 0) {
        g_sensor.hblank_min = (int32_t)(g_sensor.line_length - width);
        g_sensor.hblank_max = g_sensor.hblank_min;
        LOG_WARN("sensor: cannot query HBLANK range, assuming fixed");
    }

    if (subdev_query_ctrl(fd, V4L2_CID_VBLANK,
                          &g_sensor.vblank_min, &g_sensor.vblank_max, NULL) < 0) {
        g_sensor.vblank_min = 40;
        g_sensor.vblank_max = 0xFFFF;
        LOG_WARN("sensor: cannot query VBLANK range, using fallback");
    }

    if (subdev_query_ctrl(fd, V4L2_CID_EXPOSURE,
                          &g_sensor.exposure_min, &g_sensor.exposure_max, NULL) < 0) {
        g_sensor.exposure_min = 4;
        g_sensor.exposure_max = 65500;
        LOG_WARN("sensor: cannot query EXPOSURE range, using fallback");
    }

    int32_t cur_vblank = 0;
    if (subdev_get_ctrl(fd, V4L2_CID_VBLANK, &cur_vblank) == 0 && cur_vblank > 0) {
        uint32_t cur_height = height ? height : 1080;
        uint32_t cur_fl = cur_height + (uint32_t)cur_vblank;
        if ((int32_t)cur_fl > g_sensor.exposure_max)
            g_sensor.exposure_offset = cur_fl - (uint32_t)g_sensor.exposure_max;
        else
            g_sensor.exposure_offset = 22;
    } else {
        g_sensor.exposure_offset = 22;
    }

    g_sensor.mode_valid = true;

    LOG_INFO("sensor: line_length=%u hblank=[%d..%d] vblank=[%d..%d] "
             "exposure=[%d..%d] offset=%u",
             g_sensor.line_length,
             g_sensor.hblank_min, g_sensor.hblank_max,
             g_sensor.vblank_min, g_sensor.vblank_max,
             g_sensor.exposure_min, g_sensor.exposure_max,
             g_sensor.exposure_offset);
}

#define GRAPH_MAX_ENTITIES  40
#define GRAPH_MAX_PADS      8

struct graph_entity {
    uint32_t id;
    uint32_t function;
    uint32_t num_pads;
    char     name[64];
    char     devpath[64];
    char     compatible[96];
    int      fd;
    int      slot;
    uint32_t pad_flags[GRAPH_MAX_PADS];
    int      up_ent[GRAPH_MAX_PADS];
    uint32_t up_pad[GRAPH_MAX_PADS];
    uint32_t links;
    bool     enabled_link;
};

static struct graph_entity g_graph[GRAPH_MAX_ENTITIES];
static int g_graph_n;
static int g_topo[GRAPH_MAX_ENTITIES];
static int g_topo_n;

static int graph_vipp_port_of(int idx);

static int subdev_set_format_actual(int fd, unsigned int pad,
                                    uint32_t width, uint32_t height,
                                    uint32_t mbus_code,
                                    uint32_t *actual_code,
                                    uint32_t *actual_w, uint32_t *actual_h);
static int subdev_set_format(int fd, unsigned int pad,
                             uint32_t width, uint32_t height,
                             uint32_t mbus_code);
static int subdev_set_selection(int fd, unsigned int pad, uint32_t target,
                                uint32_t left, uint32_t top,
                                uint32_t width, uint32_t height);
static int subdev_set_selection_actual(int fd, unsigned int pad, uint32_t target,
                                       uint32_t left, uint32_t top,
                                       uint32_t width, uint32_t height,
                                       struct v4l2_rect *applied);

static struct graph_entity *graph_slot_ent(int slot)
{
    if (slot < 0)
        return NULL;
    for (int i = 0; i < g_graph_n; i++)
        if (g_graph[i].slot == slot)
            return &g_graph[i];
    return NULL;
}

static int graph_slot_fd(int slot)
{
    struct graph_entity *g = graph_slot_ent(slot);
    return g ? g->fd : -1;
}

#define CTRL_OWNER_MAX 32

static struct {
    uint32_t id;
    int      idx;
} g_ctrl_owner[CTRL_OWNER_MAX];
static int g_ctrl_owner_n;

static int graph_ctrl_fd(uint32_t id)
{
    for (int k = 0; k < g_ctrl_owner_n; k++)
        if (g_ctrl_owner[k].id == id)
            return g_ctrl_owner[k].idx >= 0 ? g_graph[g_ctrl_owner[k].idx].fd : -1;

    int owner = -1, other = -1, n = 0;
    for (int i = 0; i < g_graph_n; i++) {
        const struct graph_entity *g = &g_graph[i];
        if (g->fd < 0 || g->function == MEDIA_ENT_F_CAM_SENSOR ||
            g->function == MEDIA_ENT_F_IO_V4L)
            continue;
        struct v4l2_query_ext_ctrl q;
        memset(&q, 0, sizeof(q));
        q.id = id;
        if (ioctl(g->fd, VIDIOC_QUERY_EXT_CTRL, &q) < 0 ||
            (q.flags & V4L2_CTRL_FLAG_DISABLED))
            continue;
        if (n++ == 0)
            owner = i;
        else if (other < 0)
            other = i;
    }

    if (n > 1)
        LOG_WARN("media_pipeline: control 0x%08X is registered by %d blocks "
                 "('%s', '%s'%s) -- ambiguous, so it is sent to none of them",
                 id, n, g_graph[owner].name, g_graph[other].name,
                 n > 2 ? ", ..." : "");

    int idx = n == 1 ? owner : (n > 1 ? -2 : -1);
    if (g_ctrl_owner_n < CTRL_OWNER_MAX) {
        g_ctrl_owner[g_ctrl_owner_n].id  = id;
        g_ctrl_owner[g_ctrl_owner_n].idx = idx;
        g_ctrl_owner_n++;
    }
    return idx >= 0 ? g_graph[idx].fd : -1;
}

static int graph_find_by_id(uint32_t id)
{
    for (int i = 0; i < g_graph_n; i++)
        if (g_graph[i].id == id)
            return i;
    return -1;
}

static void graph_read_compatible(const char *devpath, char *out, size_t len)
{
    out[0] = '\0';
    if (!devpath || !devpath[0])
        return;

    const char *base = strrchr(devpath, '/');
    base = base ? base + 1 : devpath;

    char sysfs[192];
    snprintf(sysfs, sizeof(sysfs),
             "/sys/class/video4linux/%s/device/of_node/compatible", base);

    int fd = open(sysfs, O_RDONLY);
    if (fd < 0)
        return;

    char buf[192];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return;

    buf[n] = '\0';
    size_t copy = strnlen(buf, len - 1);
    memcpy(out, buf, copy);
    out[copy] = '\0';
}

static void graph_read_links(int idx)
{
    struct graph_entity *g = &g_graph[idx];
    struct media_pad_desc  pads[GRAPH_MAX_PADS];
    struct media_link_desc links[GRAPH_MAX_PADS * 4];
    struct media_links_enum le;

    if (g->num_pads == 0 || g->num_pads > GRAPH_MAX_PADS)
        return;

    if (g->links > sizeof(links) / sizeof(links[0])) {
        LOG_WARN("media_pipeline: '%s' has %u links, more than the %zu this "
                 "walk holds; its links are not followed",
                 g->name, g->links, sizeof(links) / sizeof(links[0]));
        return;
    }

    memset(&le, 0, sizeof(le));
    memset(pads, 0, sizeof(pads));
    memset(links, 0, sizeof(links));
    le.entity = g->id;
    le.pads   = pads;
    le.links  = links;

    if (ioctl(g_media_fd, MEDIA_IOC_ENUM_LINKS, &le) < 0) {
        LOG_WARN("media_pipeline: ENUM_LINKS on '%s' (%u pads, %u links): %s "
                 "-- its links are unknown, so it is neither walked nor "
                 "reported as unclassified",
                 g->name, g->num_pads, g->links, strerror(errno));
        return;
    }

    for (unsigned p = 0; p < g->num_pads; p++)
        g->pad_flags[p] = pads[p].flags;

    for (unsigned l = 0; l < g->links && l < GRAPH_MAX_PADS * 4; l++) {
        if (!(links[l].flags & MEDIA_LNK_FL_ENABLED))
            continue;

        int src = graph_find_by_id(links[l].source.entity);
        int dst = graph_find_by_id(links[l].sink.entity);
        if (src < 0 || dst < 0 || links[l].sink.index >= GRAPH_MAX_PADS)
            continue;

        g_graph[src].enabled_link = true;
        g_graph[dst].enabled_link = true;
        g_graph[dst].up_ent[links[l].sink.index] = src;
        g_graph[dst].up_pad[links[l].sink.index] = links[l].source.index;
    }
}

static void graph_topo_sort(void)
{
    bool done[GRAPH_MAX_ENTITIES] = { false };

    g_topo_n = 0;
    for (int pass = 0; pass < GRAPH_MAX_ENTITIES && g_topo_n < g_graph_n; pass++) {
        bool progress = false;

        for (int i = 0; i < g_graph_n; i++) {
            if (done[i])
                continue;

            bool ready = true;
            for (unsigned p = 0; p < g_graph[i].num_pads && p < GRAPH_MAX_PADS; p++) {
                int up = g_graph[i].up_ent[p];
                if (up >= 0 && !done[up]) {
                    ready = false;
                    break;
                }
            }
            if (!ready)
                continue;

            done[i] = true;
            g_topo[g_topo_n++] = i;
            progress = true;
        }

        if (!progress)
            break;
    }

    if (g_topo_n < g_graph_n) {
        LOG_WARN("media_pipeline: only %d of %d entities could be ordered "
                 "(cycle in the graph?); the rest are left unprogrammed",
                 g_topo_n, g_graph_n);
    }
}

static int graph_vipp_port_of(int idx)
{
    for (int hop = 0; hop < GRAPH_MAX_ENTITIES; hop++) {
        int next = -1, next_pad = -1;

        for (int i = 0; i < g_graph_n && next < 0; i++) {
            for (unsigned p = 0; p < g_graph[i].num_pads && p < GRAPH_MAX_PADS; p++) {
                if (g_graph[i].up_ent[p] == idx) {
                    next = i;
                    next_pad = (int)p;
                    break;
                }
            }
        }

        if (next < 0)
            return -1;
        if (g_graph[next].function == MEDIA_ENT_F_IO_V4L)
            return next_pad;
        idx = next;
    }
    return -1;
}

static void graph_log_topology(void)
{
    LOG_INFO("media_pipeline: === Media graph (%d entities, upstream first) ===",
             g_graph_n);

    for (int t = 0; t < g_topo_n; t++) {
        int idx = g_topo[t];
        struct graph_entity *g = &g_graph[idx];
        char up[128];
        int  n = 0;

        up[0] = '\0';
        for (unsigned p = 0; p < g->num_pads && p < GRAPH_MAX_PADS; p++) {
            if (g->up_ent[p] < 0)
                continue;
            n += snprintf(up + n, sizeof(up) - (size_t)n, "%spad%u<-%s:%u",
                          n ? ", " : "", p,
                          g_graph[g->up_ent[p]].name, g->up_pad[p]);
            if (n >= (int)sizeof(up))
                break;
        }

        LOG_INFO("  %-28s %-10s pads=%u %s",
                 g->name,
                 g->slot >= 0 ? ent_labels[g->slot] : "(no rule)",
                 g->num_pads, up[0] ? up : "(source)");
    }
}

static bool chain_programs_slot(int slot)
{
    switch (slot) {
    case ENT_SENSOR:  case ENT_VIPP:  case ENT_AESTATS:
        return true;
    default:
        return false;
    }
}

static const struct {
    int         up_slot;
    int         sink_slot;
    bool        geometry_may_differ;
    bool        code_may_differ;
    bool        keep_sink_code;
    const char *why;
} link_exceptions[] = {
    { -1, ENT_VTIMING, false, true, true,
      "a format converter without a control interface sits between these blocks" },
    { ENT_WBG, ENT_DEMOSAIC, false, true, false,
      "a bit-depth converter without a control interface sits between these blocks" },
    { ENT_DEMOSAIC, ENT_CCM, false, true, true,
      "a bit-depth converter without a control interface sits between these blocks" },
    { ENT_BLKC, ENT_GAMMA, false, true, true,
      "a format converter without a control interface sits between these blocks" },
    { -1, ENT_VDMA, true, false, false,
      "the HDMI preview output has a fixed size" },
    { -1, -1, false, false, false, NULL }
};

static int link_exception_index(int up_slot, int sink_slot)
{
    for (int e = 0; link_exceptions[e].sink_slot >= 0; e++)
        if (link_exceptions[e].sink_slot == sink_slot &&
            (link_exceptions[e].up_slot < 0 ||
             link_exceptions[e].up_slot == up_slot))
            return e;
    return -1;
}

static const struct {
    uint32_t code;
    uint8_t  order;
    uint8_t  depth;
} bayer_codes[] = {
    { MEDIA_BUS_FMT_SRGGB8_1X8,   1,  8 }, { MEDIA_BUS_FMT_SGBRG8_1X8,   2,  8 },
    { MEDIA_BUS_FMT_SGRBG8_1X8,   3,  8 }, { MEDIA_BUS_FMT_SBGGR8_1X8,   4,  8 },
    { MEDIA_BUS_FMT_SRGGB10_1X10, 1, 10 }, { MEDIA_BUS_FMT_SGBRG10_1X10, 2, 10 },
    { MEDIA_BUS_FMT_SGRBG10_1X10, 3, 10 }, { MEDIA_BUS_FMT_SBGGR10_1X10, 4, 10 },
    { MEDIA_BUS_FMT_SRGGB12_1X12, 1, 12 }, { MEDIA_BUS_FMT_SGBRG12_1X12, 2, 12 },
    { MEDIA_BUS_FMT_SGRBG12_1X12, 3, 12 }, { MEDIA_BUS_FMT_SBGGR12_1X12, 4, 12 },
    { MEDIA_BUS_FMT_SRGGB14_1X14, 1, 14 }, { MEDIA_BUS_FMT_SGBRG14_1X14, 2, 14 },
    { MEDIA_BUS_FMT_SGRBG14_1X14, 3, 14 }, { MEDIA_BUS_FMT_SBGGR14_1X14, 4, 14 },
    { MEDIA_BUS_FMT_SRGGB16_1X16, 1, 16 }, { MEDIA_BUS_FMT_SGBRG16_1X16, 2, 16 },
    { MEDIA_BUS_FMT_SGRBG16_1X16, 3, 16 }, { MEDIA_BUS_FMT_SBGGR16_1X16, 4, 16 },
};

static int bayer_index(uint32_t code)
{
    for (size_t i = 0; i < sizeof(bayer_codes) / sizeof(bayer_codes[0]); i++)
        if (bayer_codes[i].code == code)
            return (int)i;
    return -1;
}

static uint32_t bayer_code_of(uint8_t order, uint8_t depth)
{
    for (size_t i = 0; i < sizeof(bayer_codes) / sizeof(bayer_codes[0]); i++)
        if (bayer_codes[i].order == order && bayer_codes[i].depth == depth)
            return bayer_codes[i].code;
    return 0;
}

static const char *link_exception_why(int up_slot, int sink_slot,
                                      bool geom_differs, bool code_differs)
{
    int e = link_exception_index(up_slot, sink_slot);

    if (e < 0)
        return NULL;
    if (geom_differs && !link_exceptions[e].geometry_may_differ)
        return NULL;
    if (code_differs && !link_exceptions[e].code_may_differ)
        return NULL;
    return link_exceptions[e].why;
}

static bool link_keeps_sink_code(int up_slot, int sink_slot)
{
    int e = link_exception_index(up_slot, sink_slot);
    return e >= 0 && link_exceptions[e].keep_sink_code;
}

static uint32_t graph_bayer_keep_order(int fd, unsigned int pad,
                                       uint32_t w, uint32_t h,
                                       uint32_t wire, uint32_t got,
                                       uint32_t *aw, uint32_t *ah)
{
    int iw = bayer_index(wire), ig = bayer_index(got);

    if (iw < 0 || ig < 0 || bayer_codes[iw].order == bayer_codes[ig].order)
        return got;

    uint32_t want = bayer_code_of(bayer_codes[iw].order, bayer_codes[ig].depth);
    uint32_t ac = 0, rw = 0, rh = 0;

    if (!want || subdev_set_format_actual(fd, pad, w, h, want,
                                          &ac, &rw, &rh) != 0)
        return got;
    if (ac == want) {
        *aw = rw;
        *ah = rh;
    }
    return ac;
}

struct graph_fmt_ctx {
    uint32_t w, h;
    uint32_t raw_mbus;
    uint32_t yuv_mbus;
};

struct graph_policy_rule {
    int         slot;
    bool        own_sink;
    const char *why;
};

static const struct graph_policy_rule graph_policy[] = {
    { ENT_VTIMING,     true,
      "gamma emits RGB and vtiming receives YUV across an AXI-Stream converter "
      "with no AXI-Lite interface, so the code on the wire is not the code in "
      "the graph; and writing the SOURCE pad is what calls "
      "chc5tc_set_lines_per_frame(), which is a register write, not a record" },
    { ENT_CSI2RX,      false,
      "sink propagates from the sensor at the WIRE geometry; a receiver with "
      "its own crop (chc5-csi2rx) takes the optical black off when the fabric "
      "has no separate crop block -- the Xilinx receiver has none and passes "
      "through" },
    { ENT_CROP,        false,
      "sink propagates from csi2rx at the WIRE geometry; the window is the "
      "optical black the sensor reports, which no upstream pad can express" },
    { ENT_XVSUB,       false,
      "sink propagates from vtiming; the crop window and the source geometry "
      "are the HDMI fit policy" },
    { ENT_VDMA,        true,
      "pinned to 1920x1080 whatever the stream size" },
    { ENT_PIXPACK_ETH, false,
      "sink propagates from wb-gain (blkc on the mono fabric); the source "
      "code is the PFNC the Ethernet transport asked for" },
    { ENT_PIXPACK_USB, false,
      "same, USB transport" },
    { -1, false, NULL }
};

static const struct graph_policy_rule *graph_policy_for(int slot)
{
    if (slot < 0)
        return NULL;
    for (int i = 0; graph_policy[i].slot >= 0; i++)
        if (graph_policy[i].slot == slot)
            return &graph_policy[i];
    return NULL;
}

static int graph_upstream_geom(const struct graph_entity *g, unsigned int pad,
                               uint32_t *w, uint32_t *h)
{
    struct v4l2_subdev_format src;
    int up;

    if (pad >= GRAPH_MAX_PADS)
        return -1;

    up = g->up_ent[pad];
    if (up < 0 || g_graph[up].fd < 0)
        return -1;

    memset(&src, 0, sizeof(src));
    src.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    src.pad   = g->up_pad[pad];
    if (ioctl(g_graph[up].fd, VIDIOC_SUBDEV_G_FMT, &src) < 0)
        return -1;

    if (!src.format.width || !src.format.height)
        return -1;

    *w = src.format.width;
    *h = src.format.height;
    return 0;
}

static int ob_crop_apply(struct graph_entity *g, uint32_t in_w, uint32_t in_h,
                         bool in_from_wire)
{
    bool strip = crop_can_strip_ob();
    uint32_t obl = strip ? (uint32_t)g_sensor.ob_left : 0;
    uint32_t obt = strip ? (uint32_t)g_sensor.ob_top  : 0;
    uint32_t obw = obl + (strip ? (uint32_t)g_sensor.ob_right  : 0);
    uint32_t obh = obt + (strip ? (uint32_t)g_sensor.ob_bottom : 0);
    struct v4l2_rect got;

    if (!in_from_wire && (obw || obh)) {
        LOG_ERR("media_pipeline: crop: cannot read the geometry arriving "
                "on its sink -- leaving it passthrough rather than "
                "guessing the window");
        obl = obt = obw = obh = 0;
    }

    if (in_w <= obw || in_h <= obh) {
        LOG_ERR("media_pipeline: crop: optical black %ux%u does not fit "
                "the %ux%u frame arriving -- leaving it passthrough",
                obw, obh, in_w, in_h);
        obl = obt = obw = obh = 0;
    }

    if (subdev_set_selection_actual(g->fd, 0, V4L2_SEL_TGT_CROP,
                                    obl, obt, in_w - obw, in_h - obh,
                                    &got) != 0) {
        LOG_WARN("media_pipeline: crop: window %u,%u/%ux%u refused",
                 obl, obt, in_w - obw, in_h - obh);
        return -1;
    }

    if ((uint32_t)got.width  != in_w - obw ||
        (uint32_t)got.height != in_h - obh ||
        (uint32_t)got.left   != obl ||
        (uint32_t)got.top    != obt)
        LOG_ERR("media_pipeline: crop: asked %u,%u/%ux%u but the block took "
                "%d,%d/%ux%u -- downstream is sized for the wrong frame",
                obl, obt, in_w - obw, in_h - obh,
                got.left, got.top, got.width, got.height);
    else
        LOG_INFO("media_pipeline: crop: %ux%u -> %u,%u/%ux%u "
                 "(optical black l=%d t=%d r=%d b=%d stripped by %s)",
                 in_w, in_h, obl, obt, got.width, got.height,
                 g_sensor.ob_left, g_sensor.ob_top,
                 g_sensor.ob_right, g_sensor.ob_bottom, g->name);
    return 0;
}

static int pixpack_set_source(struct graph_entity *g, uint32_t pfnc,
                              const char *tag)
{
    uint32_t mbus = genconv_pfnc_to_pixpack_mbus(pfnc);
    uint32_t w = g_prog_width, h = g_prog_height, ac = 0;

    if (mbus == 0) {
        LOG_ERR("media_pipeline: %s: unsupported PixelFormat 0x%08X - rejected",
                tag, pfnc);
        return -1;
    }

    (void)graph_upstream_geom(g, 0, &w, &h);
    if (!w || !h) {
        imgsensor_cfg_t scfg;
        config_get_sensor(&scfg);
        w = scfg.width;
        h = scfg.height;
    }

    if (subdev_set_format_actual(g->fd, 1, w, h, mbus, &ac, NULL, NULL) != 0)
        return -1;
    if (ac != mbus) {
        LOG_ERR("media_pipeline: %s: asked the packer for 0x%04X (pfnc 0x%08X) "
                "and it took 0x%04X -- this fabric cannot produce that format",
                tag, mbus, pfnc, ac);
        return -1;
    }

    LOG_INFO("media_pipeline: %s: src=0x%04X %ux%u (pfnc=0x%08X)",
             tag, mbus, w, h, pfnc);
    return 0;
}

static int graph_apply_policy(struct graph_entity *g,
                              const struct graph_fmt_ctx *c)
{
    int ret = 0;

    uint32_t in_w = c->w, in_h = c->h;
    bool in_from_wire = (graph_upstream_geom(g, 0, &in_w, &in_h) == 0);

    if (!in_from_wire) {
        in_w = c->w;
        in_h = c->h;
    }

    switch (g->slot) {

    case ENT_VTIMING:
        ret |= subdev_set_format(g->fd, 0, in_w, in_h, c->yuv_mbus);
        ret |= subdev_set_format(g->fd, 1, in_w, in_h, c->yuv_mbus);
        break;

    case ENT_VDMA:
        ret |= subdev_set_format(g->fd, 0, HDMI_MAX_WIDTH, HDMI_MAX_HEIGHT,
                                 c->yuv_mbus);
        ret |= subdev_set_format(g->fd, 1, HDMI_MAX_WIDTH, HDMI_MAX_HEIGHT,
                                 c->yuv_mbus);
        break;

    case ENT_XVSUB: {
        uint32_t xvsub_w = in_w;
        uint32_t xvsub_h = in_h;

        if (in_w > HDMI_MAX_WIDTH || in_h > HDMI_MAX_HEIGHT) {
            uint32_t crop_w = (in_w > HDMI_MAX_WIDTH)  ? HDMI_MAX_WIDTH  : in_w;
            uint32_t crop_h = (in_h > HDMI_MAX_HEIGHT) ? HDMI_MAX_HEIGHT : in_h;

            uint16_t hcx = 0, hcy = 0;
            uint8_t  hc_auto = 1;
            config_get_hdmi_crop(&hcx, &hcy, &hc_auto);

            uint32_t max_l = in_w - crop_w;
            uint32_t max_t = in_h - crop_h;
            uint32_t crop_l, crop_t;

            if (hc_auto) {
                crop_l = (max_l / 2) & ~1u;
                crop_t = (max_t / 2) & ~1u;
            } else {
                crop_l = (uint32_t)hcx & ~1u;
                crop_t = (uint32_t)hcy & ~1u;

                if (crop_l > max_l) {
                    crop_l = max_l & ~1u;
                    LOG_WARN("media_pipeline: xvsub: crop_x %u out of bounds, "
                             "clamped to %u", hcx, crop_l);
                }
                if (crop_t > max_t) {
                    crop_t = max_t & ~1u;
                    LOG_WARN("media_pipeline: xvsub: crop_y %u out of bounds, "
                             "clamped to %u", hcy, crop_t);
                }
            }

            ret |= subdev_set_selection(g->fd, 0, V4L2_SEL_TGT_CROP,
                                        crop_l, crop_t, crop_w, crop_h);
            xvsub_w = crop_w;
            xvsub_h = crop_h;

            LOG_INFO("media_pipeline: xvsub: %ux%u -> crop=%u,%u/%ux%u (%s)",
                     in_w, in_h, crop_l, crop_t, crop_w, crop_h,
                     hc_auto ? "auto-centre" : "manual");
        } else {
            ret |= subdev_set_selection(g->fd, 0, V4L2_SEL_TGT_CROP,
                                        0, 0, in_w, in_h);
            LOG_INFO("media_pipeline: xvsub: %ux%u pass-through", in_w, in_h);
        }

        ret |= subdev_set_format(g->fd, 1, xvsub_w, xvsub_h, c->yuv_mbus);
        break;
    }

    case ENT_CROP:
        ret |= ob_crop_apply(g, in_w, in_h, in_from_wire);
        break;

    case ENT_CSI2RX:
        if (ob_crop_target() == ENT_CSI2RX)
            ret |= ob_crop_apply(g, in_w, in_h, in_from_wire);
        break;

    case ENT_PIXPACK_ETH:
    case ENT_PIXPACK_USB: {
        bool is_eth = (g->slot == ENT_PIXPACK_ETH);
        struct camcfg_state_s ppstate;
        config_get_full(&ppstate);

        ret |= pixpack_set_source(g, is_eth ? ppstate.pixfmt_eth
                                            : ppstate.pixfmt_usb,
                                  is_eth ? "pp_eth" : "pp_usb");
        break;
    }

    default:
        break;
    }

    return ret;
}

static void graph_carry_sink_size(const struct graph_entity *g)
{
    int sink = -1, nsink = 0;

    for (unsigned p = 0; p < g->num_pads && p < GRAPH_MAX_PADS; p++)
        if ((g->pad_flags[p] & MEDIA_PAD_FL_SINK) && g->up_ent[p] >= 0) {
            sink = (int)p;
            nsink++;
        }
    if (nsink != 1)
        return;

    struct v4l2_subdev_format in;
    memset(&in, 0, sizeof(in));
    in.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    in.pad   = (uint32_t)sink;
    if (ioctl(g->fd, VIDIOC_SUBDEV_G_FMT, &in) < 0)
        return;

    for (unsigned p = 0; p < g->num_pads && p < GRAPH_MAX_PADS; p++) {
        if (!(g->pad_flags[p] & MEDIA_PAD_FL_SOURCE))
            continue;

        struct v4l2_subdev_format out;
        memset(&out, 0, sizeof(out));
        out.which = V4L2_SUBDEV_FORMAT_ACTIVE;
        out.pad   = p;
        if (ioctl(g->fd, VIDIOC_SUBDEV_G_FMT, &out) < 0)
            continue;
        if (out.format.width == in.format.width &&
            out.format.height == in.format.height)
            continue;

        uint32_t aw = 0, ah = 0, ac = 0;
        if (subdev_set_format_actual(g->fd, p, in.format.width, in.format.height,
                                     out.format.code, &ac, &aw, &ah) != 0 ||
            aw != in.format.width || ah != in.format.height)
            LOG_WARN("media_pipeline: link walk: %s pad %u is %ux%u, its input "
                     "%ux%u -- the driver neither carried the size nor took it",
                     g->name, p, aw ? aw : out.format.width,
                     ah ? ah : out.format.height,
                     in.format.width, in.format.height);
        else
            LOG_DBG("media_pipeline: link walk: %s pad %u %ux%u -> %ux%u "
                    "(its driver does not carry the input size to its outputs)",
                    g->name, p, out.format.width, out.format.height, aw, ah);
    }
}

static int graph_propagate_formats(const struct graph_fmt_ctx *c)
{
    int ret = 0;

    for (int t = 0; t < g_topo_n; t++) {
        int idx = g_topo[t];
        struct graph_entity *g = &g_graph[idx];

        if (g->slot >= 0 && chain_programs_slot(g->slot))
            continue;
        if (g->fd < 0)
            continue;
        if (g->function == MEDIA_ENT_F_CAM_SENSOR ||
            g->function == MEDIA_ENT_F_IO_V4L)
            continue;

        const struct graph_policy_rule *pol = graph_policy_for(g->slot);
        bool sink_from_wire = !(pol && pol->own_sink);

        for (unsigned p = 0; sink_from_wire && p < g->num_pads &&
                             p < GRAPH_MAX_PADS; p++) {
            int up = g->up_ent[p];
            if (up < 0 || g_graph[up].fd < 0)
                continue;

            struct v4l2_subdev_format src;
            memset(&src, 0, sizeof(src));
            src.which = V4L2_SUBDEV_FORMAT_ACTIVE;
            src.pad   = g->up_pad[p];
            if (ioctl(g_graph[up].fd, VIDIOC_SUBDEV_G_FMT, &src) < 0) {
                LOG_WARN("media_pipeline: link walk: G_FMT %s pad %u: %s",
                         g_graph[up].name, g->up_pad[p], strerror(errno));
                continue;
            }

            uint32_t code = src.format.code;
            if (link_keeps_sink_code(g_graph[up].slot, g->slot)) {
                struct v4l2_subdev_format cur;
                memset(&cur, 0, sizeof(cur));
                cur.which = V4L2_SUBDEV_FORMAT_ACTIVE;
                cur.pad   = p;
                if (ioctl(g->fd, VIDIOC_SUBDEV_G_FMT, &cur) == 0)
                    code = cur.format.code;
            }

            uint32_t aw = 0, ah = 0, ac = 0;
            if (subdev_set_format_actual(g->fd, p,
                                         src.format.width, src.format.height,
                                         code, &ac, &aw, &ah) != 0) {
                LOG_WARN("media_pipeline: link walk: %s pad %u refused "
                         "%ux%u code 0x%04X from %s",
                         g->name, p, src.format.width, src.format.height,
                         code, g_graph[up].name);
                continue;
            }
            if (ac != code)
                ac = graph_bayer_keep_order(g->fd, p, src.format.width,
                                            src.format.height, code, ac,
                                            &aw, &ah);

            bool code_expected = ac != code &&
                link_exception_why(g_graph[up].slot, g->slot, false, true);

            if (aw != src.format.width || ah != src.format.height ||
                (ac != code && !code_expected)) {
                LOG_WARN("media_pipeline: link walk: %s pad %u ADJUSTED "
                         "%ux%u/0x%04X -> %ux%u/0x%04X (from %s pad %u)",
                         g->name, p, src.format.width, src.format.height,
                         code, aw, ah, ac,
                         g_graph[up].name, g->up_pad[p]);
            } else {
                LOG_DBG("media_pipeline: link walk: %s pad %u = %ux%u/0x%04X "
                        "from %s pad %u",
                        g->name, p, aw, ah, ac,
                        g_graph[up].name, g->up_pad[p]);
            }
        }

        if (!pol)
            graph_carry_sink_size(g);

        if (pol) {
            LOG_DBG("media_pipeline: link walk: %s policy (%s)",
                    g->name, pol->why);
            ret |= graph_apply_policy(g, c);
        }
    }

    return ret;
}

static void graph_verify_links(void)
{
    int bad = 0, expected = 0;

    for (int t = 0; t < g_topo_n; t++) {
        int idx = g_topo[t];
        struct graph_entity *g = &g_graph[idx];

        for (unsigned p = 0; p < g->num_pads && p < GRAPH_MAX_PADS; p++) {
            int up = g->up_ent[p];
            if (up < 0 || g->fd < 0 || g_graph[up].fd < 0)
                continue;
            if (g->function == MEDIA_ENT_F_IO_V4L)
                continue;

            struct v4l2_subdev_format a, b;
            memset(&a, 0, sizeof(a));
            memset(&b, 0, sizeof(b));
            a.which = b.which = V4L2_SUBDEV_FORMAT_ACTIVE;
            a.pad = g->up_pad[p];
            b.pad = p;

            if (ioctl(g_graph[up].fd, VIDIOC_SUBDEV_G_FMT, &a) < 0 ||
                ioctl(g->fd, VIDIOC_SUBDEV_G_FMT, &b) < 0)
                continue;

            bool geom_differs = a.format.width  != b.format.width ||
                                a.format.height != b.format.height;
            bool code_differs = a.format.code   != b.format.code;

            if (!geom_differs && !code_differs)
                continue;

            const char *why = link_exception_why(g_graph[up].slot, g->slot,
                                                 geom_differs, code_differs);
            if (why) {
                expected++;
                LOG_DBG("media_pipeline: link %s:%u -> %s:%u differs as "
                        "expected (%ux%u/0x%04X vs %ux%u/0x%04X): %s",
                        g_graph[up].name, g->up_pad[p], g->name, p,
                        a.format.width, a.format.height, a.format.code,
                        b.format.width, b.format.height, b.format.code, why);
                continue;
            }

            LOG_WARN("media_pipeline: LINK %s %s:%u -> %s:%u  "
                     "%ux%u/0x%04X vs %ux%u/0x%04X",
                     geom_differs ? "MISMATCH" : "CODE CHANGES (undeclared "
                                                 "converter on this wire?)",
                     g_graph[up].name, g->up_pad[p], g->name, p,
                     a.format.width, a.format.height, a.format.code,
                     b.format.width, b.format.height, b.format.code);
            bad++;
        }
    }

    if (bad == 0)
        LOG_DBG("media_pipeline: every enabled link agrees on both ends "
                "(%d known exception%s skipped)",
                expected, expected == 1 ? "" : "s");
    else
        LOG_WARN("media_pipeline: %d enabled link(s) disagree unexpectedly; "
                 "a geometry difference means the block downstream is sized "
                 "for a frame it will not receive", bad);
}

static const struct {
    const char *pattern;
    int         slot;
} name_rules[] = {
    { "pixel-packer_eth", ENT_PIXPACK_ETH },
    { "pixel-packer_usb", ENT_PIXPACK_USB },
    { "demosaic",       ENT_DEMOSAIC },
    { "blkc",           ENT_BLKC     },
    { "gamma",          ENT_GAMMA    },
    { "csi2",           ENT_CSI2RX   },
    { "video-timing",   ENT_VTIMING  },
    { "vtiming",        ENT_VTIMING  },
    { "axis_vt",        ENT_VTIMING  },
    { "video-subsample", ENT_XVSUB   },
    { "xvsub",          ENT_XVSUB    },
    { "vdma-triple",    ENT_VDMA     },
    { "vipp",           ENT_VIPP     },
    { "crop",           ENT_CROP     },
    { "ccm",            ENT_CCM      },
    { "wb-gain",        ENT_WBG      },
    { "wbg",            ENT_WBG      },
    { "ae_stats",       ENT_AESTATS  },
    { "ae-stats",       ENT_AESTATS  },
    { "aestats",        ENT_AESTATS  },
    { NULL, -1 }
};

#define SLOT_UNKNOWN      (-1)
#define SLOT_NO_CAMCFGD   (-2)

static const struct {
    const char *compatible;
    int         slot;
} compat_rules[] = {
    { "xlnx,mipi-csi2-rx-subsystem",     ENT_CSI2RX   },
    { "circuitvalley,chc5-csi2rx",       ENT_CSI2RX   },
    { "circuitvalley,chc5-blkc",         ENT_BLKC     },
    { "circuitvalley,chc5-wb-gain",      ENT_WBG      },
    { "circuitvalley,chc5-demosaic",     ENT_DEMOSAIC },
    { "circuitvalley,chc5-crop",         ENT_CROP     },
    { "circuitvalley,chc5-ccm",          ENT_CCM      },
    { "circuitvalley,chc5-gamma",        ENT_GAMMA    },
    { "circuitvalley,chc5-vtiming",      ENT_VTIMING  },
    { "circuitvalley,chc5-xvsub",        ENT_XVSUB    },
    { "circuitvalley,chc5-vdma-triple",  ENT_VDMA     },
    { "circuitvalley,chc5-ae-stats",     ENT_AESTATS  },
    { "circuitvalley,chc5_vipp-video",   ENT_VIPP     },
    { "circuitvalley,chc5-pixel-packer", ENT_PIXPACK_ETH },

    { "circuitvalley,chc5-gamma-wide",    SLOT_NO_CAMCFGD },
    { "circuitvalley,chc5-sharpen",       SLOT_NO_CAMCFGD },
    { "circuitvalley,chc5-pwl-expand",    SLOT_NO_CAMCFGD },
    { "circuitvalley,chc5-pwl-compand",   SLOT_NO_CAMCFGD },
    { "circuitvalley,chc5-emb-parser",    SLOT_NO_CAMCFGD },
    { "circuitvalley,chc5-pkt-recombine", SLOT_NO_CAMCFGD },
    { "circuitvalley,chc5-lfm-unpack",    SLOT_NO_CAMCFGD },
    { "circuitvalley,chc5-camio-sync",    SLOT_NO_CAMCFGD },

    { NULL, SLOT_UNKNOWN }
};

static bool compat_matches(const char *compat, const char *pattern)
{
    size_t n = strlen(pattern);

    if (strncmp(compat, pattern, n) != 0)
        return false;
    if (compat[n] == '\0')
        return true;
    if (compat[n] != '-')
        return false;
    for (const char *p = compat + n + 1; *p; p++)
        if (!((*p >= '0' && *p <= '9') || *p == '.'))
            return false;
    return true;
}

static int classify_by_compatible(const char *compat)
{
    if (!compat || !compat[0])
        return SLOT_UNKNOWN;
    for (int i = 0; compat_rules[i].compatible != NULL; i++)
        if (compat_matches(compat, compat_rules[i].compatible))
            return compat_rules[i].slot;
    return SLOT_UNKNOWN;
}

static int classify_by_name(const char *name)
{
    for (int i = 0; name_rules[i].pattern != NULL; i++)
        if (strcasestr(name, name_rules[i].pattern) != NULL)
            return name_rules[i].slot;
    return -1;
}

static int classify_graph_entity(int idx)
{
    struct graph_entity *g = &g_graph[idx];

    if (g->function == MEDIA_ENT_F_CAM_SENSOR)
        return ENT_SENSOR;

    if (g->function == MEDIA_ENT_F_IO_V4L) {
        if (g->num_pads > 0)
            return ENT_VIPP;
        LOG_DBG("media_pipeline: video node '%s' has no pads -- a capture "
                "node off the pipeline graph (the ae-stats metadata node), "
                "not the VIPP", g->name);
        return -1;
    }

    int by_compat = classify_by_compatible(g->compatible);
    int by_name   = classify_by_name(g->name);

    if (by_compat == ENT_PIXPACK_ETH) {
        int port = graph_vipp_port_of(idx);
        int by_port = (port == 1) ? ENT_PIXPACK_ETH :
                      (port == 2) ? ENT_PIXPACK_USB : -1;
        bool named = (by_name == ENT_PIXPACK_ETH || by_name == ENT_PIXPACK_USB);

        if (by_port >= 0) {
            if (named && by_name != by_port)
                LOG_WARN("media_pipeline: packer '%s' is NAMED %s but WIRED to "
                         "VIPP port %d; using the wiring (%s) -- check the "
                         "overlay", g->name, ent_labels[by_name], port,
                         ent_labels[by_port]);
            return by_port;
        }

        if (named) {
            LOG_WARN("media_pipeline: packer '%s' reaches no VIPP port 1 or 2 "
                     "(port %d); falling back to its name -> %s",
                     g->name, port, ent_labels[by_name]);
            return by_name;
        }

        LOG_WARN("media_pipeline: packer '%s' is neither wired to VIPP port 1/2 "
                 "nor named _eth/_usb -- not programmed", g->name);
        return -1;
    }

    if (by_compat >= 0) {
        if (by_name >= 0 && by_name != by_compat)
            LOG_WARN("media_pipeline: '%s' compatible '%s' says %s but its "
                     "name says %s; using the compatible",
                     g->name, g->compatible,
                     ent_labels[by_compat], ent_labels[by_name]);
        return by_compat;
    }

    if (by_compat == SLOT_NO_CAMCFGD) {
        if (by_name >= 0)
            LOG_DBG("media_pipeline: '%s' is %s, which camcfgd does not "
                    "program; ignoring the name rule that would call it %s",
                    g->name, g->compatible, ent_labels[by_name]);
    } else if (by_name >= 0 && !g->compatible[0]) {
        return by_name;
    } else if (by_name >= 0) {
        LOG_DBG("media_pipeline: '%s' (compatible '%s') is named like %s; "
                "not taken -- the compatible is the identity",
                g->name, g->compatible, ent_labels[by_name]);
    }

    if (g->enabled_link)
        LOG_WARN("media_pipeline: %s entity '%s' (compatible '%s') "
                 "func=0x%08X pads=%u -- no camcfgd role: format from the "
                 "link walk, settings only for the controls it registers",
                 by_compat == SLOT_NO_CAMCFGD ? "UNSUPPORTED" : "UNCLASSIFIED",
                 g->name, g->compatible[0] ? g->compatible : "none",
                 g->function, g->num_pads);
    else
        LOG_DBG("media_pipeline: unclassified entity '%s' func=0x%08X pads=%u "
                "-- no ENABLED link, so it is not in the streaming path "
                "(an off-graph control block, or a branch left disabled)",
                g->name, g->function, g->num_pads);
    return -1;
}

int media_pipeline_init(void)
{
    memset(g_graph, 0, sizeof(g_graph));
    g_ctrl_owner_n = 0;
    g_ob_crop_slot = OB_CROP_UNPROBED;
    g_graph_n = 0;
    g_topo_n  = 0;

    g_media_fd = open("/dev/media0", O_RDWR);
    if (g_media_fd < 0) {
        LOG_ERR("media_pipeline: cannot open /dev/media0: %s", strerror(errno));
        return -1;
    }

    struct media_entity_desc ent;
    memset(&ent, 0, sizeof(ent));

    for (ent.id = 0 | MEDIA_ENT_ID_FLAG_NEXT; ; ent.id |= MEDIA_ENT_ID_FLAG_NEXT) {
        if (ioctl(g_media_fd, MEDIA_IOC_ENUM_ENTITIES, &ent) < 0) {
            if (errno == EINVAL)
                break;
            LOG_ERR("media_pipeline: ENUM_ENTITIES: %s", strerror(errno));
            break;
        }

        if (g_graph_n >= GRAPH_MAX_ENTITIES) {
            LOG_WARN("media_pipeline: more than %d entities; '%s' and any "
                     "after it are ignored", GRAPH_MAX_ENTITIES, ent.name);
            break;
        }

        struct graph_entity *g = &g_graph[g_graph_n];

        g->id       = ent.id;
        g->function = ent.type;
        g->num_pads = ent.pads;
        g->links    = ent.links;
        g->slot     = -1;
        g->fd       = -1;
        snprintf(g->name, sizeof(g->name), "%s", ent.name);

        if (ent.dev.major != 0 &&
            find_dev_path(ent.dev.major, ent.dev.minor,
                          g->devpath, sizeof(g->devpath)) == 0) {
            g->fd = open(g->devpath, O_RDWR);
            if (g->fd < 0)
                LOG_WARN("media_pipeline: cannot open %s (%s): %s",
                         g->devpath, g->name, strerror(errno));
            graph_read_compatible(g->devpath, g->compatible,
                                  sizeof(g->compatible));
        }

        LOG_DBG("media_pipeline: entity id=%u name='%s' func=0x%08X pads=%u "
                "links=%u compatible='%s'",
                ent.id, ent.name, ent.type, ent.pads, ent.links,
                g->compatible[0] ? g->compatible : "none");

        g_graph_n++;
    }

    for (int i = 0; i < g_graph_n; i++)
        for (unsigned p = 0; p < GRAPH_MAX_PADS; p++) {
            g_graph[i].up_ent[p] = -1;
            g_graph[i].up_pad[p] = 0;
        }
    for (int i = 0; i < g_graph_n; i++)
        graph_read_links(i);

    graph_topo_sort();

    for (int t = 0; t < g_topo_n; t++) {
        int idx = g_topo[t];
        struct graph_entity *g = &g_graph[idx];

        int slot = classify_graph_entity(idx);
        if (slot < 0 || slot >= ENT_COUNT)
            continue;

        struct graph_entity *held = graph_slot_ent(slot);
        if (held) {
            LOG_WARN("media_pipeline: '%s' also looks like %s, which '%s' "
                     "already fills; leaving it to the link walk",
                     g->name, ent_labels[slot], held->name);
            continue;
        }

        g->slot = slot;

        const char *how =
            (g->function == MEDIA_ENT_F_CAM_SENSOR ||
             g->function == MEDIA_ENT_F_IO_V4L)        ? "function" :
            ((slot == ENT_PIXPACK_ETH || slot == ENT_PIXPACK_USB) &&
             graph_vipp_port_of(idx) >= 1)                  ? "VIPP port" :
            (classify_by_compatible(g->compatible) == slot) ? "compatible"
                                                            : "name";

        LOG_INFO("media_pipeline: found %-9s -> '%s' (%s) pads=%u by %s",
                 ent_labels[slot], g->name,
                 g->devpath[0] ? g->devpath : "n/a", g->num_pads, how);
    }

    LOG_INFO("media_pipeline: === Pipeline summary ===");
    for (int i = 0; i < ENT_COUNT; i++) {
        const struct graph_entity *e = graph_slot_ent(i);
        LOG_INFO("  [%d] %-9s: %s  %s",
                 i, ent_labels[i],
                 e ? e->name : "(not found)",
                 (e && e->fd >= 0) ? e->devpath : "");
    }
    graph_log_topology();

    if (graph_slot_fd(ENT_SENSOR) < 0) {
        LOG_ERR("media_pipeline: no sensor entity found!");
        media_pipeline_close();
        return -1;
    }

    sensor_query_fixed_params();

    return 0;
}

void media_pipeline_close(void)
{
    for (int i = 0; i < g_graph_n; i++) {
        if (g_graph[i].fd >= 0) {
            close(g_graph[i].fd);
            g_graph[i].fd = -1;
        }
    }
    g_graph_n = 0;
    g_ctrl_owner_n = 0;
    g_topo_n  = 0;

    if (g_media_fd >= 0) {
        close(g_media_fd);
        g_media_fd = -1;
    }
}

static int subdev_set_format(int fd, unsigned int pad,
                             uint32_t width, uint32_t height,
                             uint32_t mbus_code)
{
    if (fd < 0)
        return -1;

    struct v4l2_subdev_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    fmt.pad   = pad;
    fmt.format.width  = width;
    fmt.format.height = height;
    fmt.format.code   = mbus_code;
    fmt.format.field  = V4L2_FIELD_NONE;
    fmt.format.colorspace = V4L2_COLORSPACE_RAW;

    if (ioctl(fd, VIDIOC_SUBDEV_S_FMT, &fmt) < 0) {
        LOG_WARN("subdev_set_format: pad %u %ux%u code 0x%04X: %s",
                 pad, width, height, mbus_code, strerror(errno));
        return -1;
    }

    LOG_DBG("subdev_set_format: pad %u -> %ux%u code 0x%04X (actual %ux%u 0x%04X)",
            pad, width, height, mbus_code,
            fmt.format.width, fmt.format.height, fmt.format.code);
    return 0;
}

static int subdev_set_format_actual(int fd, unsigned int pad,
                                     uint32_t width, uint32_t height,
                                     uint32_t mbus_code,
                                     uint32_t *actual_code,
                                     uint32_t *actual_w, uint32_t *actual_h)
{
    if (fd < 0)
        return -1;

    struct v4l2_subdev_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    fmt.pad   = pad;
    fmt.format.width  = width;
    fmt.format.height = height;
    fmt.format.code   = mbus_code;
    fmt.format.field  = V4L2_FIELD_NONE;
    fmt.format.colorspace = V4L2_COLORSPACE_RAW;

    if (ioctl(fd, VIDIOC_SUBDEV_S_FMT, &fmt) < 0) {
        LOG_WARN("subdev_set_format_actual: pad %u %ux%u code 0x%04X: %s",
                 pad, width, height, mbus_code, strerror(errno));
        return -1;
    }

    if (actual_code)
        *actual_code = fmt.format.code;
    if (actual_w)
        *actual_w = fmt.format.width;
    if (actual_h)
        *actual_h = fmt.format.height;

    LOG_DBG("subdev_set_format: pad %u -> %ux%u code 0x%04X (actual %ux%u 0x%04X)",
            pad, width, height, mbus_code,
            fmt.format.width, fmt.format.height, fmt.format.code);
    return 0;
}

static int subdev_set_selection_actual(int fd, unsigned int pad, uint32_t target,
                                        uint32_t left, uint32_t top,
                                        uint32_t width, uint32_t height,
                                        struct v4l2_rect *applied)
{
    if (fd < 0)
        return -1;

    struct v4l2_subdev_selection sel;
    memset(&sel, 0, sizeof(sel));
    sel.which  = V4L2_SUBDEV_FORMAT_ACTIVE;
    sel.pad    = pad;
    sel.target = target;
    sel.r.left   = left;
    sel.r.top    = top;
    sel.r.width  = width;
    sel.r.height = height;

    if (ioctl(fd, VIDIOC_SUBDEV_S_SELECTION, &sel) < 0) {
        LOG_WARN("subdev_set_selection: pad %u target=0x%X %u,%u/%ux%u: %s",
                 pad, target, left, top, width, height, strerror(errno));
        return -1;
    }

    if (applied)
        *applied = sel.r;

    if (sel.r.left  != (int32_t)left  || sel.r.top    != (int32_t)top ||
        sel.r.width != width           || sel.r.height != height)
        LOG_INFO("sensor adjusted selection 0x%X %u,%u/%ux%u -> %d,%d/%ux%u "
                 "(hardware grid/floor); using the ADJUSTED rectangle",
                 target, left, top, width, height,
                 sel.r.left, sel.r.top, sel.r.width, sel.r.height);
    else
        LOG_DBG("subdev_set_selection: pad %u target=0x%X -> %u,%u/%ux%u",
                pad, target, left, top, width, height);
    return 0;
}

static int subdev_set_selection(int fd, unsigned int pad, uint32_t target,
                                 uint32_t left, uint32_t top,
                                 uint32_t width, uint32_t height)
{
    return subdev_set_selection_actual(fd, pad, target, left, top,
                                       width, height, NULL);
}

static int subdev_set_ctrl(int fd, uint32_t id, int32_t value)
{
    if (fd < 0)
        return -1;

    struct v4l2_control ctrl;
    ctrl.id    = id;
    ctrl.value = value;

    if (ioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0) {
        if (ioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0) {
            LOG_WARN("subdev_set_ctrl: id=0x%08X val=%d: %s (after 1 retry)",
                     id, value, strerror(errno));
            return -1;
        }
    }
    return 0;
}

static int subdev_has_ctrl(int fd, uint32_t id)
{
    if (fd < 0)
        return 0;

    struct v4l2_queryctrl q;
    memset(&q, 0, sizeof q);
    q.id = id;

    return ioctl(fd, VIDIOC_QUERYCTRL, &q) == 0 &&
           !(q.flags & V4L2_CTRL_FLAG_DISABLED);
}

static int apply_flip_axis(int fd, uint32_t id, int want, const char *axis)
{
    if (!subdev_has_ctrl(fd, id)) {
        if (!want)
            return 0;
        LOG_WARN("media_pipeline: sensor has no %s control -- %s flip NOT "
                 "applied; image stays unflipped on this axis", axis, axis);
        return -1;
    }
    return subdev_set_ctrl(fd, id, want);
}

static int subdev_set_ext_ctrl(int fd, uint32_t id, int32_t value)
{
    if (fd < 0)
        return -1;

    struct v4l2_ext_control ctrl;
    struct v4l2_ext_controls ctrls;
    memset(&ctrl, 0, sizeof(ctrl));
    memset(&ctrls, 0, sizeof(ctrls));

    ctrl.id    = id;
    ctrl.value = value;
    ctrls.count    = 1;
    ctrls.controls = &ctrl;
    ctrls.which    = 0;

    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrls) < 0) {
        LOG_WARN("subdev_set_ext_ctrl: id=0x%08X val=%d: %s",
                 id, value, strerror(errno));
        return -1;
    }
    return 0;
}

static int subdev_set_ext_ctrl2(int fd,
                                 uint32_t id1, int32_t val1,
                                 uint32_t id2, int32_t val2)
{
    if (fd < 0)
        return -1;

    struct v4l2_ext_control ctrl[2];
    struct v4l2_ext_controls ctrls;
    memset(ctrl, 0, sizeof(ctrl));
    memset(&ctrls, 0, sizeof(ctrls));

    ctrl[0].id    = id1;
    ctrl[0].value = val1;
    ctrl[1].id    = id2;
    ctrl[1].value = val2;
    ctrls.count    = 2;
    ctrls.controls = ctrl;
    ctrls.which    = 0;

    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrls) < 0) {
        LOG_WARN("subdev_set_ext_ctrl2: id=0x%08X/%08X val=%d/%d: %s",
                 id1, id2, val1, val2, strerror(errno));
        return -1;
    }
    return 0;
}

static int subdev_set_ext_ctrl_u8_array(int fd, uint32_t id,
                                        const uint8_t *arr, unsigned n)
{
    if (fd < 0)
        return -1;

    struct v4l2_ext_control ctrl;
    struct v4l2_ext_controls ctrls;
    memset(&ctrl, 0, sizeof(ctrl));
    memset(&ctrls, 0, sizeof(ctrls));

    ctrl.id   = id;
    ctrl.size = n;
    ctrl.p_u8 = (uint8_t *)arr;
    ctrls.count    = 1;
    ctrls.controls = &ctrl;
    ctrls.which    = 0;

    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrls) < 0) {
        LOG_DBG("subdev_set_ext_ctrl_u8_array: id=0x%08X n=%u: %s",
                id, n, strerror(errno));
        return -1;
    }
    return 0;
}

static int subdev_set_ext_ctrl_array(int fd, uint32_t id,
                                     const uint32_t *arr, unsigned n)
{
    if (fd < 0)
        return -1;

    struct v4l2_ext_control ctrl;
    struct v4l2_ext_controls ctrls;
    memset(&ctrl, 0, sizeof(ctrl));
    memset(&ctrls, 0, sizeof(ctrls));

    ctrl.id    = id;
    ctrl.size  = n * sizeof(uint32_t);
    ctrl.p_u32 = (uint32_t *)arr;
    ctrls.count    = 1;
    ctrls.controls = &ctrl;
    ctrls.which    = 0;

    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrls) < 0) {
        LOG_WARN("subdev_set_ext_ctrl_array: id=0x%08X n=%u: %s",
                 id, n, strerror(errno));
        return -1;
    }
    return 0;
}

#define CHC5CCM_MATRIX_ELEMS  12
#define CHC5CCM_SCALE         4096

static int32_t g_ccm_matrix[CHC5CCM_MATRIX_ELEMS] = {
    CHC5CCM_SCALE, 0, 0, 0,
    0, CHC5CCM_SCALE, 0, 0,
    0, 0, CHC5CCM_SCALE, 0,
};
static bool g_ccm_enable = true;

static void apply_ccm(void)
{
    int fd = graph_ctrl_fd(CHC5CCM_CID_MATRIX);

    if (fd < 0)
        return;

    uint32_t wire[CHC5CCM_MATRIX_ELEMS];
    for (unsigned wi = 0; wi < CHC5CCM_MATRIX_ELEMS; wi++)
        wire[wi] = (uint32_t)(g_ccm_matrix[wi] & 0xFFFF);

    if (subdev_set_ext_ctrl_array(fd, CHC5CCM_CID_MATRIX,
                                  wire, CHC5CCM_MATRIX_ELEMS) == 0) {
        subdev_set_ext_ctrl(fd, CHC5CCM_CID_ENABLE, g_ccm_enable ? 1 : 0);
        LOG_INFO("media_pipeline: CCM driven (%s, K11=%d)",
                 g_ccm_enable ? "enabled" : "bypass", g_ccm_matrix[0]);
    }
}

int media_pipeline_set_ccm(const int32_t *matrix, bool enable)
{
    if (matrix)
        memcpy(g_ccm_matrix, matrix, sizeof(g_ccm_matrix));
    g_ccm_enable = enable;
    apply_ccm();
    return 0;
}

void media_pipeline_ccm_tune_tick(void)
{
    static time_t last_mtime = 0;
    const char *path = "/run/camcfgd/ccm";
    struct stat stt;

    if (stat(path, &stt) != 0) { last_mtime = 0; return; }
    if (stt.st_mtime == last_mtime) return;
    last_mtime = stt.st_mtime;

    FILE *f = fopen(path, "r");
    if (!f)
        return;

    char first[16] = {0};
    if (fscanf(f, "%15s", first) == 1 && strcmp(first, "bypass") == 0) {
        fclose(f);
        media_pipeline_set_ccm(NULL, false);
        LOG_INFO("ccm-tune: bypass (passthrough)");
        return;
    }

    rewind(f);
    int32_t m[CHC5CCM_MATRIX_ELEMS];
    int n;
    for (n = 0; n < CHC5CCM_MATRIX_ELEMS; n++)
        if (fscanf(f, "%d", &m[n]) != 1)
            break;
    fclose(f);

    if (n != CHC5CCM_MATRIX_ELEMS) {
        LOG_WARN("ccm-tune: expected %d ints, got %d -- ignored",
                 CHC5CCM_MATRIX_ELEMS, n);
        return;
    }
    media_pipeline_set_ccm(m, true);
    LOG_INFO("ccm-tune: applied [%d %d %d / %d %d %d / %d %d %d] off[%d %d %d]",
             m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10],
             m[3], m[7], m[11]);
}

void media_pipeline_apply_manual_wb(void)
{
    static float last_r = -1.0f, last_b = -1.0f;

    if (config_get_auto_wb()) {
        last_r = last_b = -1.0f;
        return;
    }
    float r = config_get_wb_ratio(0);
    float b = config_get_wb_ratio(2);
    if (r != last_r || b != last_b) {
        media_pipeline_set_wb_gains(r, b);
        last_r = r;  last_b = b;
    }
}

#define CHC5GAMMA_CID_RED     CHC5GAMMA_CID_RED_GAMMA
#define CHC5GAMMA_CID_GREEN   CHC5GAMMA_CID_GREEN_GAMMA
#define CHC5GAMMA_CID_BLUE    CHC5GAMMA_CID_BLUE_GAMMA
void media_pipeline_apply_gamma(void)
{
    int fd = graph_ctrl_fd(CHC5GAMMA_CID_ENABLE);

    if (fd < 0)
        return;

    uint8_t lut[3][256];
    bool    host_off;

    if (!config_get_output_gamma(lut, &host_off)) {
        subdev_set_ext_ctrl(fd, CHC5GAMMA_CID_ENABLE, 0);
        if (host_off)
            LOG_INFO("media_pipeline: gamma OFF (host) -> chc5_gamma DISABLED "
                     "(linear output)");
        else
            LOG_WARN("media_pipeline: no gamma tuning in the sensor archive "
                     "-> chc5_gamma DISABLED (linear output)");
        return;
    }

    if (subdev_set_ext_ctrl_u8_array(fd, CHC5GAMMA_CID_TABLE,
                                     (const uint8_t *)lut, 3u * 256u) != 0) {
        uint8_t x10 = config_get_gamma_x10();
        if (x10 == 0) {
            subdev_set_ext_ctrl(fd, CHC5GAMMA_CID_ENABLE, 0);
            LOG_WARN("media_pipeline: gamma.bin supplied but this driver has no "
                     "table control -> block DISABLED (will not approximate a LUT)");
            return;
        }
        subdev_set_ext_ctrl(fd, CHC5GAMMA_CID_RED,    (int)x10);
        subdev_set_ext_ctrl(fd, CHC5GAMMA_CID_GREEN,  (int)x10);
        subdev_set_ext_ctrl(fd, CHC5GAMMA_CID_BLUE,   (int)x10);
        subdev_set_ext_ctrl(fd, CHC5GAMMA_CID_ENABLE, 1);
        LOG_WARN("media_pipeline: gamma table control absent -- fell back to "
                 "power curve %u.%u", x10 / 10, x10 % 10);
        return;
    }

    subdev_set_ext_ctrl(fd, CHC5GAMMA_CID_ENABLE, 1);
    LOG_INFO("media_pipeline: gamma LUT applied (3x256; in=16 -> %u, in=128 -> %u)",
             lut[1][16], lut[1][128]);
}

int media_pipeline_set_aestats_fmt(uint32_t width, uint32_t height)
{
    if (graph_slot_fd(ENT_AESTATS) < 0)
        return -1;

    if (g_prog_width && g_prog_height &&
        (g_prog_width != width || g_prog_height != height)) {
        LOG_INFO("media_pipeline: aestats geometry %ux%u requested, metering "
                 "the streamed %ux%u instead", width, height,
                 g_prog_width, g_prog_height);
        width  = g_prog_width;
        height = g_prog_height;
    }

    int ret = subdev_set_format(graph_slot_fd(ENT_AESTATS), 0, width, height,
                                g_sensor_raw_mbus);
    if (ret == 0)
        LOG_INFO("media_pipeline: aestats geometry -> %ux%u (code 0x%04X)",
                 width, height, g_sensor_raw_mbus);
    return ret;
}

#define CHC5WBG_PHASE_RGGB    0xE4
#define CHC5WBG_PHASE_GRBG    0xB1
#define CHC5WBG_PHASE_GBRG    0x4E
#define CHC5WBG_PHASE_BGGR    0x1B

static uint32_t bayer_code_to_phase_map(uint32_t code)
{
    switch (code) {
    case MEDIA_BUS_FMT_SGRBG8_1X8:
    case MEDIA_BUS_FMT_SGRBG10_1X10:
    case MEDIA_BUS_FMT_SGRBG12_1X12:
    case MEDIA_BUS_FMT_SGRBG14_1X14:
        return CHC5WBG_PHASE_GRBG;
    case MEDIA_BUS_FMT_SGBRG8_1X8:
    case MEDIA_BUS_FMT_SGBRG10_1X10:
    case MEDIA_BUS_FMT_SGBRG12_1X12:
    case MEDIA_BUS_FMT_SGBRG14_1X14:
        return CHC5WBG_PHASE_GBRG;
    case MEDIA_BUS_FMT_SBGGR8_1X8:
    case MEDIA_BUS_FMT_SBGGR10_1X10:
    case MEDIA_BUS_FMT_SBGGR12_1X12:
    case MEDIA_BUS_FMT_SBGGR14_1X14:
        return CHC5WBG_PHASE_BGGR;
    default:
        return CHC5WBG_PHASE_RGGB;
    }
}

static int bayer_code_to_cfa_order(uint32_t code)
{
    switch (code) {
    case MEDIA_BUS_FMT_SRGGB8_1X8:   case MEDIA_BUS_FMT_SRGGB10_1X10:
    case MEDIA_BUS_FMT_SRGGB12_1X12: case MEDIA_BUS_FMT_SRGGB14_1X14:
        return 1;
    case MEDIA_BUS_FMT_SGBRG8_1X8:   case MEDIA_BUS_FMT_SGBRG10_1X10:
    case MEDIA_BUS_FMT_SGBRG12_1X12: case MEDIA_BUS_FMT_SGBRG14_1X14:
        return 2;
    case MEDIA_BUS_FMT_SGRBG8_1X8:   case MEDIA_BUS_FMT_SGRBG10_1X10:
    case MEDIA_BUS_FMT_SGRBG12_1X12: case MEDIA_BUS_FMT_SGRBG14_1X14:
        return 3;
    case MEDIA_BUS_FMT_SBGGR8_1X8:   case MEDIA_BUS_FMT_SBGGR10_1X10:
    case MEDIA_BUS_FMT_SBGGR12_1X12: case MEDIA_BUS_FMT_SBGGR14_1X14:
        return 4;
    default:
        return 0;
    }
}

static void check_cfa_order(uint32_t raw_mbus)
{
    int drv = bayer_code_to_cfa_order(raw_mbus);
    static const char *nm[] = { "?", "BayerRG", "BayerGB", "BayerGR", "BayerBG" };

    if (g_bayer_order == 0 || drv == 0)
        return;

    if (g_bayer_order != drv)
        LOG_ERR("media_pipeline: CFA MISMATCH -- archive declares %s but the "
                "sensor driver reports %s (mbus 0x%04X).  Raw Bayer output "
                "will have red and blue swapped at the host; fix the "
                "manifest's bayer_order / pixel_formats.",
                nm[g_bayer_order], nm[drv], raw_mbus);
}

void media_pipeline_refresh_cfa_order(void)
{
    int fd = graph_slot_fd(ENT_SENSOR);
    struct v4l2_subdev_format sfmt;

    if (fd < 0) {
        config_set_live_cfa_order(0);
        return;
    }
    apply_flip_axis(fd, V4L2_CID_HFLIP, config_get_effective_hflip(), "HFLIP");
    apply_flip_axis(fd, V4L2_CID_VFLIP, config_get_effective_vflip(), "VFLIP");
    memset(&sfmt, 0, sizeof(sfmt));
    sfmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    sfmt.pad   = 0;
    config_set_live_cfa_order(ioctl(fd, VIDIOC_SUBDEV_G_FMT, &sfmt) == 0
                              ? bayer_code_to_cfa_order(sfmt.format.code) : 0);
}

static void set_wbg_phase_map(uint32_t raw_mbus)
{
    int fd = graph_ctrl_fd(CHC5WBG_CID_PHASE_MAP);
    uint32_t pm;

    if (fd < 0)
        return;

    pm = bayer_code_to_phase_map(raw_mbus);
    if (subdev_set_ctrl(fd, CHC5WBG_CID_PHASE_MAP, (int32_t)pm) != 0) {
        LOG_WARN("media_pipeline: wb_gain PHASE_MAP 0x%02X not applied "
                 "(colour channels may be swapped)", pm);
        return;
    }
    LOG_INFO("media_pipeline: wb_gain PHASE_MAP=0x%02X (sensor code 0x%04X)",
             pm, raw_mbus);
}
#define CHC5WBG_GAIN_ONE      256
#define CHC5WBG_GAIN_MAX      0xFFFF

int media_pipeline_set_wb_gains(float gain_r, float gain_b)
{
    int fd = graph_ctrl_fd(CHC5WBG_CID_GAINS);
    uint32_t g[4];
    long r, b;

    if (fd < 0)
        return -1;

    r = lroundf(gain_r * (float)CHC5WBG_GAIN_ONE);
    b = lroundf(gain_b * (float)CHC5WBG_GAIN_ONE);
    if (r < 1) r = 1; else if (r > CHC5WBG_GAIN_MAX) r = CHC5WBG_GAIN_MAX;
    if (b < 1) b = 1; else if (b > CHC5WBG_GAIN_MAX) b = CHC5WBG_GAIN_MAX;

    g[0] = (uint32_t)r;
    g[1] = CHC5WBG_GAIN_ONE;
    g[2] = CHC5WBG_GAIN_ONE;
    g[3] = (uint32_t)b;

    if (subdev_set_ext_ctrl_array(fd, CHC5WBG_CID_GAINS, g, 4) != 0)
        return -1;
    subdev_set_ext_ctrl(fd, CHC5WBG_CID_ENABLE, 1);

    return 0;
}

static int video_set_format(int fd, uint32_t width, uint32_t height,
                            uint32_t pixfmt)
{
    if (fd < 0)
        return -1;

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = width;
    fmt.fmt.pix.height      = height;
    fmt.fmt.pix.pixelformat = pixfmt;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        LOG_WARN("video_set_format: %ux%u fmt=0x%08X: %s",
                 width, height, pixfmt, strerror(errno));
        return -1;
    }
    return 0;
}

#ifndef MEDIA_BUS_FMT_YUYV8_1X16
#define MEDIA_BUS_FMT_YUYV8_1X16      0x2008
#endif

int media_pipeline_set_format(const imgsensor_cfg_t *cfg)
{
    uint32_t w = cfg->width;
    uint32_t h = cfg->height;

    {
        uint32_t winc_g = g_width_inc  ? g_width_inc  : 8u;
        uint32_t hinc_g = g_height_inc ? g_height_inc : 8u;

        if (((cfg->binning >> 4) & 0x0Fu) != 0u && g_binning_width_inc)
            winc_g = g_binning_width_inc;
        if ((cfg->binning & 0x0Fu) != 0u && g_binning_height_inc)
            hinc_g = g_binning_height_inc;

    if (winc_g > 1u && (w % winc_g) != 0u) {
        uint32_t w_aligned = align_down(w, winc_g);
        if (w_aligned < winc_g)
            w_aligned = winc_g;
        LOG_WARN("media_pipeline: width %u is not a multiple of the sensor's "
                 "width_inc (%u) -- using %u", w, winc_g, w_aligned);
        w = w_aligned;
    }

    if (hinc_g > 1u && (h % hinc_g) != 0u) {
        uint32_t h_aligned = align_down(h, hinc_g);
        if (h_aligned < hinc_g)
            h_aligned = hinc_g;
        LOG_WARN("media_pipeline: height %u is not a multiple of the sensor's "
                 "height_inc (%u) -- using %u (a non-aligned height breaks the "
                 "AE/AWB stats block when binned)",
                 h, hinc_g, h_aligned);
        h = h_aligned;
    }
    }

    g_prog_width  = w;
    g_prog_height = h;

    uint32_t yuv_mbus = MEDIA_BUS_FMT_YUYV8_1X16;

    uint32_t raw_mbus;
    switch (g_sensor_bit_depth) {
    case 16: raw_mbus = MEDIA_BUS_FMT_SRGGB16_1X16; break;
    case 14: raw_mbus = MEDIA_BUS_FMT_SRGGB14_1X14; break;
    case 12: raw_mbus = MEDIA_BUS_FMT_SRGGB12_1X12; break;
    case 10: raw_mbus = MEDIA_BUS_FMT_SRGGB10_1X10; break;
    case  8: raw_mbus = MEDIA_BUS_FMT_SRGGB8_1X8;   break;
    default: raw_mbus = MEDIA_BUS_FMT_SRGGB12_1X12; break;
    }

    int ret = 0;

    if (graph_slot_fd(ENT_SENSOR) >= 0) {
        int fd = graph_slot_fd(ENT_SENSOR);

        uint32_t winc = g_width_inc  ? g_width_inc  : 8u;
        uint32_t hinc = g_height_inc ? g_height_inc : 2u;
        if (!g_sensor_width || !g_sensor_height || !g_width_inc || !g_height_inc) {
            static bool warned, noted;
            if (!g_sensor_caps_looked) {
                if (!noted) {
                    noted = true;
                    LOG_INFO("media_pipeline: sensor_caps not read yet -- the "
                             "start-up setup uses the default steps %u/%u", winc, hinc);
                }
            } else if (!warned) {
                warned = true;
                LOG_WARN("media_pipeline: sensor_caps has no geometry "
                         "(pre-update file?) -- using legacy steps %u/%u and "
                         "leaving array bounds to the driver; re-activate the "
                         "sensor to pick up its real limits", winc, hinc);
            }
        }

        uint8_t bin_x_enc = (cfg->binning >> 4) & 0x0F;
        uint8_t bin_y_enc =  cfg->binning       & 0x0F;
        if (bin_x_enc > 2u) bin_x_enc = 2u;
        if (bin_y_enc > 2u) bin_y_enc = 2u;
        uint32_t bin_x = 1u << bin_x_enc;
        uint32_t bin_y = 1u << bin_y_enc;

        uint32_t max_bx = g_binning_max_h ? g_binning_max_h : 2u;
        uint32_t max_by = g_binning_max_v ? g_binning_max_v : 2u;
        if (bin_x > max_bx) bin_x = max_bx;
        if (bin_y > max_by) bin_y = max_by;

        bool bin_req = (bin_x > 1u || bin_y > 1u);

        bool binning = false;
        if (bin_req) {
            uint64_t need_w = (uint64_t)w * bin_x;
            uint64_t need_h = (uint64_t)h * bin_y;
            if (!g_sensor_width || !g_sensor_height) {
                binning = true;
            } else if (g_binning_width && g_binning_height) {
                binning = ((uint64_t)cfg->offset_x + w <= g_binning_width &&
                           (uint64_t)cfg->offset_y + h <= g_binning_height);
                if (!binning)
                    LOG_WARN("media_pipeline: binning %ux%u refused at %ux%u "
                             "+%u,%u -- the binned readout tops out at %ux%u; "
                             "streaming UNBINNED",
                             bin_x, bin_y, w, h, cfg->offset_x, cfg->offset_y,
                             g_binning_width, g_binning_height);
                if (binning && g_binning_width_min && g_binning_height_min &&
                    (w < g_binning_width_min || h < g_binning_height_min)) {
                    binning = false;
                    LOG_WARN("media_pipeline: binning %ux%u refused at %ux%u "
                             "-- the binned readout starts at %ux%u; "
                             "streaming UNBINNED",
                             bin_x, bin_y, w, h,
                             g_binning_width_min, g_binning_height_min);
                }
            } else if ((uint64_t)cfg->offset_x * bin_x + need_w <= g_sensor_width &&
                       (uint64_t)cfg->offset_y * bin_y + need_h <= g_sensor_height) {
                binning = true;
            } else {
                LOG_WARN("media_pipeline: binning %ux%u refused at %ux%u -- "
                         "readout %llux%llu + offset exceeds the %ux%u array; "
                         "streaming UNBINNED",
                         bin_x, bin_y, w, h,
                         (unsigned long long)need_w, (unsigned long long)need_h,
                         g_sensor_width, g_sensor_height);
            }
        }
        if (!binning) {
            bin_x = 1u; bin_y = 1u;
            uint32_t uw = g_width_inc  ? g_width_inc  : 8u;
            uint32_t uh = g_height_inc ? g_height_inc : 2u;
            if (uh < 4u) uh = 8u;
            if (uw > 1u && (w % uw) != 0u) {
                uint32_t a = align_down(w, uw); if (a < uw) a = uw;
                LOG_WARN("media_pipeline: binning refused -- re-aligning width "
                         "%u to the unbinned step %u -> %u", w, uw, a);
                w = a;
            }
            if (uh > 1u && (h % uh) != 0u) {
                uint32_t a = align_down(h, uh); if (a < uh) a = uh;
                LOG_WARN("media_pipeline: binning refused -- re-aligning height "
                         "%u to the unbinned step %u -> %u", h, uh, a);
                h = a;
            }
            g_prog_width  = w;
            g_prog_height = h;
        }

        uint32_t oxinc = g_offset_x_inc ? g_offset_x_inc : winc;
        uint32_t oyinc = g_offset_y_inc ? g_offset_y_inc : hinc;
        if (bin_x > 1u && g_binning_offset_x_inc)
            oxinc = g_binning_offset_x_inc;
        if (bin_y > 1u && g_binning_offset_y_inc)
            oyinc = g_binning_offset_y_inc;

        uint32_t crop_l = align_down(cfg->offset_x * bin_x, oxinc);
        uint32_t crop_t = align_down(cfg->offset_y * bin_y, oyinc);
        uint32_t crop_w = align_down(w * bin_x, winc);
        uint32_t crop_h = align_down(h * bin_y, hinc);

        uint32_t lim_w = g_sensor_width, lim_h = g_sensor_height;
        if (binning && g_binning_width && g_binning_height) {
            lim_w = g_binning_width  * bin_x;
            lim_h = g_binning_height * bin_y;
        }
        if (lim_w && lim_h) {
            if (crop_w > lim_w)  crop_w = align_down(lim_w,  winc);
            if (crop_h > lim_h)  crop_h = align_down(lim_h,  hinc);
            if (crop_l + crop_w > lim_w)
                crop_l = align_down(lim_w - crop_w, oxinc);
            if (crop_t + crop_h > lim_h)
                crop_t = align_down(lim_h - crop_h, oyinc);
        }

        {
            struct v4l2_rect got;
            if (subdev_set_selection_actual(fd, 0, V4L2_SEL_TGT_CROP,
                                            crop_l, crop_t, crop_w, crop_h,
                                            &got) == 0) {
                crop_l = (uint32_t)got.left; crop_t = (uint32_t)got.top;
                crop_w = got.width;          crop_h = got.height;
            } else {
                ret |= -1;
            }
        }

        {
            struct v4l2_rect got;
            if (subdev_set_selection_actual(fd, 0, V4L2_SEL_TGT_COMPOSE,
                                            0, 0, w, h, &got) == 0) {
                if (got.width != w || got.height != h) {
                    LOG_INFO("media_pipeline: adopting sensor compose %ux%u "
                             "(requested %ux%u)", got.width, got.height, w, h);
                    w = got.width;
                    h = got.height;
                    g_prog_width  = w;
                    g_prog_height = h;
                }
            } else {
                ret |= -1;
            }
        }

        if (w && h && (crop_w != w * bin_x || crop_h != h * bin_y))
            LOG_WARN("media_pipeline: crop/compose ratio is %ux%u / %ux%u, "
                     "not the %ux%u binning that was asked for -- the driver "
                     "will infer a DIFFERENT factor from this and the stream "
                     "may come back unbinned",
                     crop_w, crop_h, w, h, bin_x, bin_y);

        LOG_INFO("media_pipeline: sensor crop=%u,%u/%ux%u compose=%ux%u bin=%ux%u",
                 crop_l, crop_t, crop_w, crop_h, w, h, bin_x, bin_y);

        {
            int eh = config_get_effective_hflip();
            int ev = config_get_effective_vflip();
            int fh = apply_flip_axis(fd, V4L2_CID_HFLIP, eh, "HFLIP");
            int fv = apply_flip_axis(fd, V4L2_CID_VFLIP, ev, "VFLIP");

            if (fh || fv)
                LOG_WARN("media_pipeline: orientation INCOMPLETE -- wanted "
                         "h=%d v=%d, applied h=%s v=%s%s",
                         eh, ev, fh ? "NO" : "yes", fv ? "NO" : "yes",
                         (eh && ev && (!!fh != !!fv))
                             ? " -- result is a MIRROR, not a 180 rotation"
                             : "");
            else if (eh || ev || g_orient_hflip || g_orient_vflip)
                LOG_INFO("media_pipeline: sensor flip h=%d v=%d "
                         "(mount h=%d v=%d, host rx=%d ry=%d)%s",
                         eh, ev, g_orient_hflip, g_orient_vflip,
                         config_get_reverse_x(), config_get_reverse_y(),
                         (eh && ev) ? " = 180 rotation" : "");
        }

        uint32_t actual = raw_mbus, aw = w, ah = h;
        ret |= subdev_set_format_actual(fd, 0, w, h, raw_mbus, &actual, &aw, &ah);
        if (actual != 0)
            raw_mbus = actual;

        sensor_query_ob();

        g_wire_w = aw;
        g_wire_h = ah;

        bool ob_mismatch_logged = false;
        {
            uint32_t obw = (uint32_t)(g_sensor.ob_left + g_sensor.ob_right);
            uint32_t obh = (uint32_t)(g_sensor.ob_top  + g_sensor.ob_bottom);

            bool strip = crop_can_strip_ob();

            if (obw || obh || strip) {
                uint32_t img_w = strip && aw > obw ? aw - obw : aw;
                uint32_t img_h = strip && ah > obh ? ah - obh : ah;

                if (strip && (aw <= obw || ah <= obh)) {
                    LOG_ERR("media_pipeline: optical black %ux%u does not fit "
                            "the wire frame %ux%u -- crop passthrough",
                            obw, obh, aw, ah);
                    g_sensor.ob_left = g_sensor.ob_top = 0;
                    g_sensor.ob_right = g_sensor.ob_bottom = 0;
                    strip = false;
                    img_w = aw;
                    img_h = ah;
                }

                if (img_w != w || img_h != h) {
                    uint32_t rw = geom_correct(w, img_w);
                    uint32_t rh = geom_correct(h, img_h);
                    uint32_t a2 = rw, h2 = rh, c2 = raw_mbus;

                    LOG_INFO("media_pipeline: asked the sensor for %ux%u and "
                             "the host would receive %ux%u (%s) -- re-asking "
                             "for %ux%u so the delivered image is the %ux%u "
                             "that was requested",
                             w, h, img_w, img_h,
                             strip ? "wire minus the band the crop takes"
                                   : "the wire, band included",
                             rw, rh, w, h);

                    ret |= subdev_set_format_actual(fd, 0, rw, rh, raw_mbus,
                                                    &c2, &a2, &h2);
                    if (c2 != 0)
                        raw_mbus = c2;
                    if (a2 && h2) {
                        g_wire_w = a2;
                        g_wire_h = h2;
                        aw = a2;
                        ah = h2;
                        img_w = strip && aw > obw ? aw - obw : aw;
                        img_h = strip && ah > obh ? ah - obh : ah;
                    }

                    if (img_w != w || img_h != h) {
                        ob_mismatch_logged = true;
                        LOG_WARN("media_pipeline: after compensating, the "
                                 "sensor settled on a wire frame of %ux%u -- "
                                 "an image of %ux%u, not the requested %ux%u. "
                                 "The geometry grid cannot express it; the "
                                 "host is told %ux%u, which is what arrives",
                                 aw, ah, img_w, img_h, w, h, img_w, img_h);
                    }
                }

                if (strip)
                    LOG_INFO("media_pipeline: wire %ux%u carries %ux%u optical "
                             "black -> image %ux%u (the crop strips it)",
                             aw, ah, obw, obh, img_w, img_h);

                aw = img_w;
                ah = img_h;
            }
        }

        if (aw && ah && (aw != w || ah != h)) {
            if (!ob_mismatch_logged)
                LOG_WARN("sensor snapped %ux%u -> %ux%u (hardware grid); using "
                         "the SNAPPED size downstream -- a mismatch here "
                         "delivers zero frames over USB", w, h, aw, ah);
            LOG_INFO("media_pipeline: adopting sensor geometry %ux%u "
                     "(requested %ux%u)", aw, ah, w, h);
            w = aw;
            h = ah;
        }
    }

    g_sensor_raw_mbus = raw_mbus;

    g_prog_width  = w;
    g_prog_height = h;

    set_wbg_phase_map(raw_mbus);
    if (!config_get_reverse_x() && !config_get_reverse_y() && !cfg->binning)
        check_cfa_order(raw_mbus);
    config_set_live_cfa_order(bayer_code_to_cfa_order(raw_mbus));

    media_pipeline_set_aestats_fmt(w, h);

    LOG_INFO("media_pipeline: set format %ux%u sensor_raw=0x%04X",
             w, h, raw_mbus);

    {
        const struct graph_fmt_ctx fctx = {
            .w = w, .h = h, .raw_mbus = raw_mbus, .yuv_mbus = yuv_mbus,
        };
        ret |= graph_propagate_formats(&fctx);
    }

    if (graph_slot_fd(ENT_VIPP) >= 0)
        ret |= video_set_format(graph_slot_fd(ENT_VIPP), 32, 32,
                                V4L2_PIX_FMT_YUYV);

    graph_verify_links();

    pthread_mutex_lock(&g_sensor_lock);
    sensor_query_mode_params(w);
    pthread_mutex_unlock(&g_sensor_lock);

    g_ctrl_cache.valid = false;

    return ret;
}

int media_pipeline_set_pixpack_format(uint32_t pfnc_usb, uint32_t pfnc_eth)
{
    struct graph_entity *eth = graph_slot_ent(ENT_PIXPACK_ETH);
    struct graph_entity *usb = graph_slot_ent(ENT_PIXPACK_USB);
    int ret = 0;

    if (eth && eth->fd >= 0 && pfnc_eth != 0)
        ret |= pixpack_set_source(eth, pfnc_eth, "pp_eth");
    if (usb && usb->fd >= 0 && pfnc_usb != 0)
        ret |= pixpack_set_source(usb, pfnc_usb, "pp_usb");

    return ret;
}

int media_pipeline_set_sensor_controls(const imgsensor_cfg_t *cfg)
{
    int fd = graph_slot_fd(ENT_SENSOR);
    if (fd < 0) {
        LOG_WARN("media_pipeline: sensor not open, cannot set controls");
        return -1;
    }

    pthread_mutex_lock(&g_sensor_lock);

    int ret = 0;
    uint64_t pr = g_sensor.fixed_valid ? g_sensor.pixel_rate : 840000000ULL;
    uint32_t aw = g_sensor.mode_valid ? g_sensor.active_width : cfg->width;
    uint32_t ah = (g_sensor.mode_valid && g_sensor.active_height)
                ? g_sensor.active_height : cfg->height;
    uint32_t iw = g_prog_width ? g_prog_width : cfg->width;

    int32_t hblank_min = g_sensor.mode_valid ? g_sensor.hblank_min
                       : (int32_t)((aw > 2048 ? 0x5DC0u : 0x31C4u) - aw);
    int32_t hblank_max = g_sensor.mode_valid ? g_sensor.hblank_max : hblank_min;

    unsigned k_lines = 1;
    if (g_sensor.mode_valid && ah > 0 && g_sensor.vblank_min >= 0) {
        double ratio = ((double)ah + (double)g_sensor.vblank_min)
                     / (double)ah;
        unsigned k = (unsigned)(ratio + 0.5);
        if (k >= 2 && k <= 4 && ratio >= (double)k - 0.35 && ratio <= (double)k + 0.35)
            k_lines = k;
    }
    if (k_lines != 1)
        LOG_INFO("sensor: %u line periods per output line (height %u + vblank_min %d) "
                 "-- bandwidth hblank divided by %u",
                 k_lines, ah, g_sensor.vblank_min, k_lines);

    int32_t hblank_bw = hblank_min;
    {
        struct camcfg_state_s bwstate;
        config_get_full(&bwstate);

        if (bwstate.usb_active) {
            uint8_t bpp = (bwstate.pixfmt_usb >> 16) & 0xFF;
            if (bpp > 0) {
                int32_t hb = (int32_t)((uint64_t)iw * bpp * pr /
                                       ((uint64_t)k_lines * 8ULL * config_get_link_bw_bps(CFG_LINK_USB))) - (int32_t)aw;
                if (hb > hblank_bw) hblank_bw = hb;
            }
        }
        if (bwstate.eth_active) {
            uint8_t bpp = (bwstate.pixfmt_eth >> 16) & 0xFF;
            if (bpp > 0) {
                int32_t hb = (int32_t)((uint64_t)iw * bpp * pr /
                                       ((uint64_t)k_lines * 8ULL * config_get_link_bw_bps(CFG_LINK_ETH))) - (int32_t)aw;
                if (hb > hblank_bw) hblank_bw = hb;
            }
        }

        {
            const uint32_t pf[2] = { bwstate.pixfmt_usb, bwstate.pixfmt_eth };

            for (int i = 0; i < 2; i++) {
                if (pf[i] != GVSP_PIX_BGR8 && pf[i] != GVSP_PIX_BGRA8)
                    continue;
                uint8_t bpp = (pf[i] >> 16) & 0xFF;
                int32_t hb = (int32_t)((uint64_t)iw * bpp * pr /
                                       ((uint64_t)k_lines * 8ULL * BW_PIXPACK_MAX)) - (int32_t)aw;
                if (hb > hblank_bw) hblank_bw = hb;
            }
        }

        if (hblank_bw < hblank_min) hblank_bw = hblank_min;
        if (hblank_bw > hblank_max) hblank_bw = hblank_max;
    }

    int32_t  hblank;
    uint32_t vblank;
    uint32_t ll;

    float fps_f = config_get_fps_f();
    if (fps_f <= 0.0f) fps_f = (float)cfg->fps;

#ifdef TIMING_MAXIMIZE_HBLANK
    vblank = (uint32_t)g_sensor.vblank_min;
    hblank = hblank_bw;
    ll = aw + (uint32_t)hblank;

    if (fps_f > 0.0f) {
        uint64_t total_clks = (uint64_t)((double)pr / (double)fps_f);
        uint32_t fl = ah + vblank;
        uint32_t needed_ll = (uint32_t)(total_clks / fl);
        hblank = (int32_t)(needed_ll - aw);

        if (hblank < hblank_bw) hblank = hblank_bw;
        if (hblank < hblank_min) hblank = hblank_min;
        if (hblank > hblank_max) {
            hblank = hblank_max;
            ll = aw + (uint32_t)hblank;
            uint32_t fl_needed = (uint32_t)(total_clks / ll);
            if (fl_needed > ah)
                vblank = fl_needed - ah;
            if ((int32_t)vblank > g_sensor.vblank_max)
                vblank = (uint32_t)g_sensor.vblank_max;
        }
        ll = aw + (uint32_t)hblank;
    }
#else
    hblank = hblank_bw;
    vblank = (uint32_t)g_sensor.vblank_min;
    ll = aw + (uint32_t)hblank;

    if (fps_f > 0.0f) {
        uint64_t total_clks = (uint64_t)((double)pr / (double)fps_f);
        uint32_t fl_needed = (uint32_t)(total_clks / ll);

        if (fl_needed > ah)
            vblank = fl_needed - ah;

        if ((int32_t)vblank < g_sensor.vblank_min)
            vblank = (uint32_t)g_sensor.vblank_min;
        if ((int32_t)vblank > g_sensor.vblank_max)
            vblank = (uint32_t)g_sensor.vblank_max;
    }
#endif

    double line_us = (double)ll / (double)pr * 1e6;
    uint32_t exp_lines = 4;
    if (line_us > 0.0 && cfg->exposure_us > 0)
        exp_lines = (uint32_t)(cfg->exposure_us / line_us + 0.5);
    uint32_t req_exp_lines = exp_lines;
    if (exp_lines < (uint32_t)g_sensor.exposure_min)
        exp_lines = (uint32_t)g_sensor.exposure_min;

    uint32_t fl_exp = exp_lines + g_sensor.exposure_offset;
    if (fl_exp > ah) {
        uint32_t vblank_exp = fl_exp - ah;
        if (vblank_exp > vblank) vblank = vblank_exp;
    }

    if ((int32_t)vblank < g_sensor.vblank_min)
        vblank = (uint32_t)g_sensor.vblank_min;
    if ((int32_t)vblank > g_sensor.vblank_max)
        vblank = (uint32_t)g_sensor.vblank_max;

    uint32_t fl_actual = ah + vblank;
    uint32_t exp_max = (fl_actual > g_sensor.exposure_offset)
                     ? fl_actual - g_sensor.exposure_offset : 4;
    if (exp_lines > exp_max)
        exp_lines = exp_max;

    int32_t gain_code;
    {
        double gain_db = gain_code_to_db(cfg->analog_gain);

        if (!config_get_ae_enabled() && !config_get_host_active() &&
            req_exp_lines > exp_lines && exp_lines > 0) {
            double lost_stops_db = 20.0 * log10((double)req_exp_lines / (double)exp_lines);
            gain_db += lost_stops_db;
            LOG_DBG("media_pipeline: exp clamped %u->%u lines, +%.1fdB gain rebalance",
                    req_exp_lines, exp_lines, lost_stops_db);
        }

        gain_code = gain_db_to_code(gain_db, g_sensor.gain_min, gain_code_max());
    }

    int32_t bl = (int32_t)cfg->black_level;
    if (bl < g_sensor.bl_min) bl = g_sensor.bl_min;
    if (bl > g_sensor.bl_max) bl = g_sensor.bl_max;
    int32_t blkc_sub = blkc_subtract_for(bl);

    bool ae_owns_exp  = config_get_stream_active() && config_get_ae_enabled();
    bool ae_owns_gain = config_get_stream_active() && config_get_auto_gain();

    if (hblank_max > hblank_min) {
        if (!g_ctrl_cache.valid || g_ctrl_cache.hblank != hblank) {
            int32_t hb_actual = hblank;

            ret |= subdev_set_ctrl(fd, V4L2_CID_HBLANK, hblank);

            if (subdev_get_ctrl(fd, V4L2_CID_HBLANK, &hb_actual) == 0 &&
                hb_actual >= 0)
                ll = (uint32_t)aw + (uint32_t)hb_actual;

            g_ctrl_cache.hblank = hblank;
            g_sensor.line_length = ll;
        }
    }

    if (!g_ctrl_cache.valid || g_ctrl_cache.vblank != (int32_t)vblank) {
        ret |= subdev_set_ctrl(fd, V4L2_CID_VBLANK, (int32_t)vblank);
        g_ctrl_cache.vblank = (int32_t)vblank;
    }

    if (!ae_owns_exp &&
        (!g_ctrl_cache.valid || g_ctrl_cache.exposure != (int32_t)exp_lines)) {
        ret |= subdev_set_ctrl(fd, V4L2_CID_EXPOSURE, (int32_t)exp_lines);
        g_ctrl_cache.exposure = (int32_t)exp_lines;
    }

    if (!ae_owns_gain &&
        (!g_ctrl_cache.valid || g_ctrl_cache.gain_code != gain_code)) {
        ret |= subdev_set_ctrl(fd, V4L2_CID_ANALOGUE_GAIN, gain_code);
        g_ctrl_cache.gain_code = gain_code;
    }

    if (!g_ctrl_cache.valid || g_ctrl_cache.test_pattern != (int32_t)cfg->test_pattern) {
        ret |= subdev_set_ctrl(fd, V4L2_CID_TEST_PATTERN, (int32_t)cfg->test_pattern);
        g_ctrl_cache.test_pattern = (int32_t)cfg->test_pattern;
    }

    if (!g_ctrl_cache.valid) {
        ret |= subdev_set_ctrl(fd, CHC5_CID_BLACK_LEVEL, g_sensor.bl_default);
        LOG_INFO("sensor: pedestal set to %d (the sensor's own default)",
                 g_sensor.bl_default);
    }
    if (!g_ctrl_cache.valid || g_ctrl_cache.black_level != blkc_sub) {
        if (graph_ctrl_fd(CHC5_CID_BLACK_LEVEL) >= 0)
            ret |= subdev_set_ctrl(graph_ctrl_fd(CHC5_CID_BLACK_LEVEL),
                                   CHC5_CID_BLACK_LEVEL, blkc_sub);
        LOG_INFO("black level: sensor pedestal %d - BlackLevel %d "
                 "-> blkc subtracts %d", g_sensor.bl_default, bl, blkc_sub);
        g_ctrl_cache.black_level = blkc_sub;
    }

    double actual_fps = (ll > 0 && (ah + vblank) > 0)
        ? (double)pr / ((double)ll * (double)(ah + vblank))
        : 0.0;
    if (actual_fps > 0.0) g_actual_fps = (float)actual_fps;

    uint32_t ll_floor = aw + (uint32_t)hblank_bw;
    double max_fps = (ll_floor > 0)
        ? (double)pr / ((double)ll_floor * (double)(ah
                                                    + (uint32_t)g_sensor.vblank_min))
        : 0.0;
    if (max_fps > 0.0) g_max_fps = (float)max_fps;

    if (graph_ctrl_fd(CHC5_XVSUB_CID_FRAME_NUM) >= 0) {
        uint16_t hdmi_max = config_get_hdmi_max_fps();
        if (hdmi_max > HDMI_FPS_MAX) hdmi_max = HDMI_FPS_MAX;
        int hdmi_en = config_get_hdmi_enable() ? 1 : 0;

        if (!g_ctrl_cache.valid ||
            g_ctrl_cache.xvsub_vblank != vblank ||
            g_ctrl_cache.xvsub_hblank != hblank ||
            g_ctrl_cache.xvsub_hdmi_max != hdmi_max ||
            g_ctrl_cache.hdmi_enabled != hdmi_en) {

            int32_t best_num = 1, best_den = 1;
            uint32_t ifps = (uint32_t)(actual_fps + 0.5);

            if (hdmi_max > 0 && ifps > hdmi_max) {
                #define HDMI_TARGET_LO  23
                #define HDMI_TARGET_HI  28

                uint32_t best_out = 0;
                bool best_is_num1 = false;

                for (int32_t d = 1; d <= 60; d++) {
                    for (int32_t n = 1; n <= d && n <= 255; n++) {
                        uint32_t out = ifps * (uint32_t)n / (uint32_t)d;
                        if (out > hdmi_max) continue;

                        bool in_range = (out >= HDMI_TARGET_LO && out <= HDMI_TARGET_HI);
                        bool is_num1 = (n == 1);

                        bool best_in_range = (best_out >= HDMI_TARGET_LO &&
                                              best_out <= HDMI_TARGET_HI);
                        bool better = false;

                        if (in_range && !best_in_range) {
                            better = true;
                        } else if (in_range == best_in_range) {
                            if (is_num1 && !best_is_num1) {
                                better = true;
                            } else if (is_num1 == best_is_num1) {
                                better = (out > best_out);
                            }
                        }

                        if (better) {
                            best_out = out;
                            best_num = n;
                            best_den = d;
                            best_is_num1 = is_num1;
                        }
                    }
                }
            }

            if (!hdmi_en) { best_num = 0; best_den = 1; }

            int was_en = (g_ctrl_cache.hdmi_enabled != 0);

            if (hdmi_en && !was_en && graph_ctrl_fd(CHC5_VDMA_CID_HDMI_ENABLE) >= 0)
                subdev_set_ctrl(graph_ctrl_fd(CHC5_VDMA_CID_HDMI_ENABLE),
                                CHC5_VDMA_CID_HDMI_ENABLE, 1);

            if (!g_ctrl_cache.valid ||
                g_ctrl_cache.xvsub_num != best_num ||
                g_ctrl_cache.xvsub_den != best_den) {
                subdev_set_ext_ctrl2(graph_ctrl_fd(CHC5_XVSUB_CID_FRAME_NUM),
                                     CHC5_XVSUB_CID_FRAME_NUM, best_num,
                                     CHC5_XVSUB_CID_FRAME_DEN, best_den);
                g_ctrl_cache.xvsub_num = best_num;
                g_ctrl_cache.xvsub_den = best_den;

                LOG_INFO("media_pipeline: xvsub: fps=%u limit=%u -> %d/%d (%u fps out)",
                         ifps, hdmi_max, best_num, best_den,
                         ifps * (uint32_t)best_num / (uint32_t)best_den);
            }

            if (!hdmi_en && was_en && graph_ctrl_fd(CHC5_VDMA_CID_HDMI_ENABLE) >= 0)
                subdev_set_ctrl(graph_ctrl_fd(CHC5_VDMA_CID_HDMI_ENABLE),
                                CHC5_VDMA_CID_HDMI_ENABLE, 0);

            if (hdmi_en != was_en)
                LOG_INFO("media_pipeline: HDMI output %s",
                         hdmi_en ? "ENABLED (xvsub restored + VDMA start)"
                                 : "DISABLED (xvsub drop-all + VDMA stop)");

            g_ctrl_cache.xvsub_vblank   = vblank;
            g_ctrl_cache.xvsub_hblank   = hblank;
            g_ctrl_cache.xvsub_hdmi_max = hdmi_max;
            g_ctrl_cache.hdmi_enabled   = hdmi_en;
        }
    }

    g_ctrl_cache.valid = true;

    LOG_INFO("media_pipeline: sensor: exp=%uus(%uL) hblank=%d ll=%u "
             "vblank=%u(%.1ffps) gain=%u(%.1fdB) bl=%u test=%u",
             cfg->exposure_us, exp_lines, hblank, ll,
             vblank, actual_fps,
             cfg->analog_gain,
             gain_code_to_db(cfg->analog_gain),
             cfg->black_level, cfg->test_pattern);

    pthread_mutex_unlock(&g_sensor_lock);
    return ret;
}
int32_t media_pipeline_get_gain_max(void)
{
    return gain_code_max();
}

double media_pipeline_get_gain_db_max(void)
{
    return (double)gain_code_max() / GAIN_DB10_PER_DB;
}

int32_t media_pipeline_get_bl_default(void)
{
    return g_sensor.fixed_valid ? g_sensor.bl_default : 0;
}

int32_t blkc_subtract_for(int32_t black_level)
{
    int32_t sub = media_pipeline_get_bl_default() - black_level;
    if (sub < 0) sub = 0;
    if (sub > CHC5_BLKC_SUBTRACT_MAX) sub = CHC5_BLKC_SUBTRACT_MAX;
    return sub;
}

static int stream_on_once(void)
{
    int fd = graph_slot_fd(ENT_VIPP);
    if (fd < 0) {
        LOG_WARN("media_pipeline: VIPP not open, cannot stream on");
        return -1;
    }

    struct v4l2_requestbuffers reqbuf;
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.count  = 1;
    reqbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
        LOG_ERR("media_pipeline: REQBUFS failed: %s", strerror(errno));
        return -1;
    }

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index  = 0;

    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
        LOG_ERR("media_pipeline: QBUF failed: %s", strerror(errno));
        reqbuf.count = 0;
        ioctl(fd, VIDIOC_REQBUFS, &reqbuf);
        return -1;
    }

    g_stream_buffers_setup = true;

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        LOG_ERR("media_pipeline: STREAMON failed: %s", strerror(errno));
        reqbuf.count = 0;
        ioctl(fd, VIDIOC_REQBUFS, &reqbuf);
        g_stream_buffers_setup = false;
        return -1;
    }

    LOG_INFO("media_pipeline: stream ON");
    return 0;
}

int media_pipeline_stream_on(void)
{
    for (int attempt = 1; attempt <= 3; attempt++) {
        if (stream_on_once() == 0) {
            apply_ccm();
            media_pipeline_apply_gamma();
            media_pipeline_set_wb_gains(config_get_wb_ratio(0),
                                        config_get_wb_ratio(2));
            return 0;
        }
        LOG_WARN("media_pipeline: stream_on attempt %d/3 failed -- rolling back",
                 attempt);
        media_pipeline_stream_off();
        if (attempt < 3)
            usleep(50000);
    }
    LOG_ERR("media_pipeline: stream_on failed after 3 attempts");
    return -1;
}

int media_pipeline_stream_off(void)
{
    int fd = graph_slot_fd(ENT_VIPP);
    if (fd < 0)
        return -1;

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
        LOG_WARN("media_pipeline: STREAMOFF failed: %s", strerror(errno));
    }

    if (g_stream_buffers_setup) {
        struct v4l2_requestbuffers reqbuf;
        memset(&reqbuf, 0, sizeof(reqbuf));
        reqbuf.count  = 0;
        reqbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        reqbuf.memory = V4L2_MEMORY_MMAP;
        ioctl(fd, VIDIOC_REQBUFS, &reqbuf);
        g_stream_buffers_setup = false;
    }

    LOG_INFO("media_pipeline: stream OFF");

    g_ctrl_cache.valid = false;

    return 0;
}
