// SPDX-License-Identifier: CC-BY-NC-ND-4.0
/*
 * ipc_server.c - Unix domain socket IPC server
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <stdatomic.h>
#include <stdlib.h>

#include "common.h"
#include "config.h"
#include "genconv.h"
#include "ipc_server.h"

#define MAX_CLIENTS          8
#define ACCEPT_POLL_TIMEOUT  500
#define CLIENT_RECV_TIMEOUT  2

static pthread_t g_thread;
static volatile int g_running;
static int g_listen_fd = -1;

static atomic_uint_fast64_t g_next_conn_id = 1;
static atomic_uint_fast64_t g_stream_owner = 0;

struct client_ctx {
    int      fd;
    uint64_t cid;
};

static int send_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, p + sent, len - sent);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static int recv_all(int fd, void *buf, size_t len)
{
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    while (got < len) {
        ssize_t n = read(fd, p + got, len - got);
        if (n < 0) {
            if (errno == EINTR) continue;
            if ((errno == EAGAIN || errno == EWOULDBLOCK) && got == 0 && g_running)
                continue;
            return -1;
        }
        if (n == 0)
            return -1;
        got += (size_t)n;
    }
    return 0;
}

static int send_reply(int client_fd, uint8_t msg_type,
                      const void *payload, uint16_t payload_len)
{
    uint8_t hdr[3];
    hdr[0] = msg_type;
    hdr[1] = (uint8_t)(payload_len >> 8);
    hdr[2] = (uint8_t)(payload_len & 0xFF);

    if (send_all(client_fd, hdr, 3) < 0)
        return -1;
    if (payload_len > 0 && payload)
        return send_all(client_fd, payload, payload_len);
    return 0;
}

static const char *validate_sensor_cfg(const imgsensor_cfg_t *c)
{
    if (c->width  == 0 || c->width  == 0xFFFFu) return "bad width";
    if (c->height == 0 || c->height == 0xFFFFu) return "bad height";
    if (c->offset_x == 0xFFFFu) return "bad offset_x";
    if (c->offset_y == 0xFFFFu) return "bad offset_y";
    if (!pixfmt_is_valid(c->pixel_format))
        return "bad pixel_format";
    if (c->exposure_us == 0 || c->exposure_us == 0xFFFFFFFFu)
        return "bad exposure_us";

    if ((c->binning >> 4) > 2 || (c->binning & 0x0F) > 2)
        return "bad binning";
    if (c->test_pattern > 8)
        return "bad test_pattern";
    if ((c->stream_enable & 0x0F) > 2 || (c->stream_enable & 0xE0))
        return "bad stream_enable";
    if ((c->auto_flags & 0xC0))
        return "bad auto_flags (reserved bits)";
    for (int shift = 0; shift <= 4; shift += 2) {
        unsigned pair = (c->auto_flags >> shift) & 0x03u;
        if (pair == 3) return "bad auto_flags (pair=11)";
    }
    if ((c->flip_flags & 0xF0))
        return "bad flip_flags (reserved bits)";
    for (int shift = 0; shift <= 2; shift += 2) {
        unsigned pair = (c->flip_flags >> shift) & 0x03u;
        if (pair == 3) return "bad flip_flags (pair=11)";
    }
    {
        uint16_t g = c->gamma_x100 & (uint16_t)~CHC5_GAMMA_CURVE_ADJ;
        if (c->gamma_x100 != CHC5_GAMMA_NO_CHANGE && c->gamma_x100 != CHC5_GAMMA_OFF &&
            c->gamma_x100 != CHC5_GAMMA_SRGB &&
            (g < CHC5_GAMMA_USER_MIN || g > CHC5_GAMMA_USER_MAX))
            return "bad gamma_x100";
    }

    if (c->width  > 16384) return "width above sanity ceiling";
    if (c->height > 16384) return "height above sanity ceiling";
    if ((unsigned)c->offset_x + (unsigned)c->width  > 0xFFFFu)
        return "offset_x + width overflow";
    if ((unsigned)c->offset_y + (unsigned)c->height > 0xFFFFu)
        return "offset_y + height overflow";
    if (c->exposure_us > EXPOSURE_US_MAX)
        return "exposure_us above sanity ceiling";
    if (c->fps > 10000)
        return "fps above sanity ceiling";

    unsigned bpp = (c->pixel_format >> 16) & 0xFFu;
    unsigned typ = (c->pixel_format >> 24) & 0xFFu;
    if (bpp == 0 || bpp > 64)
        return "PFNC bpp out of range";
    if (typ != 0x01 && typ != 0x02 && typ != 0x80)
        return "PFNC type byte invalid";

    return NULL;
}

static void handle_set_sensor_cfg(int client_fd,
                                  const uint8_t *payload, uint16_t len)
{
    if (len < sizeof(imgsensor_cfg_t)) {
        send_reply(client_fd, IPC_MSG_REPLY_ERR, "short payload", 13);
        return;
    }

    imgsensor_cfg_t cfg;
    memcpy(&cfg, payload, sizeof(cfg));

    const char *reject = validate_sensor_cfg(&cfg);
    if (reject) {
        LOG_WARN("ipc: SET_SENSOR_CFG rejected: %s "
                 "(%ux%u+%u+%u pfnc=0x%08X fps=%u exp=%u gain=%u bin=0x%02X)",
                 reject, cfg.width, cfg.height, cfg.offset_x, cfg.offset_y,
                 cfg.pixel_format, cfg.fps, cfg.exposure_us,
                 cfg.analog_gain, cfg.binning);
        send_reply(client_fd, IPC_MSG_REPLY_ERR, reject, strlen(reject));
        return;
    }

    LOG_INFO("ipc: SET_SENSOR_CFG %ux%u+%u+%u pfnc=0x%08X %ufps exp=%uus gain=%u bin=0x%02X",
             cfg.width, cfg.height, cfg.offset_x, cfg.offset_y,
             cfg.pixel_format, cfg.fps,
             cfg.exposure_us, cfg.analog_gain, cfg.binning);

    config_set_sensor(&cfg, CFG_SRC_GIGE);
    send_reply(client_fd, IPC_MSG_REPLY_OK, NULL, 0);
}

static void handle_get_config(int client_fd)
{
    struct camcfg_state_s state;
    config_get_full(&state);

    send_reply(client_fd, IPC_MSG_REPLY_OK,
               &state, (uint16_t)sizeof(state));
}

static void handle_set_genreg(int client_fd,
                              const uint8_t *payload, uint16_t len)
{
    if (len < 5) {
        send_reply(client_fd, IPC_MSG_REPLY_ERR, "short payload", 13);
        return;
    }

    uint32_t addr = ((uint32_t)payload[0] << 24) |
                    ((uint32_t)payload[1] << 16) |
                    ((uint32_t)payload[2] <<  8) |
                    ((uint32_t)payload[3]);
    int vlen = len - 4;
    const uint8_t *value = payload + 4;

    genconv_apply_register(addr, value, vlen);
    send_reply(client_fd, IPC_MSG_REPLY_OK, NULL, 0);
}

static void handle_stream_ctrl(int client_fd,
                               const uint8_t *payload, uint16_t len,
                               uint64_t cid)
{
    if (len < 1) {
        send_reply(client_fd, IPC_MSG_REPLY_ERR, "short payload", 13);
        return;
    }

    bool enable = (payload[0] != 0);
    LOG_INFO("ipc: STREAM_CTRL %s", enable ? "ON" : "OFF");
    config_set_stream(enable);
    if (enable)
        atomic_store(&g_stream_owner, cid);
    else if (atomic_load(&g_stream_owner) == cid)
        atomic_store(&g_stream_owner, 0);
    send_reply(client_fd, IPC_MSG_REPLY_OK, NULL, 0);
}

static int handle_client_message(int client_fd, uint64_t cid)
{
    uint8_t hdr[3];
    if (recv_all(client_fd, hdr, 3) < 0)
        return -1;

    uint8_t msg_type = hdr[0];
    uint16_t payload_len = ((uint16_t)hdr[1] << 8) | hdr[2];

    if (payload_len > IPC_MAX_PAYLOAD) {
        LOG_WARN("ipc: payload too large: %u", payload_len);
        return -1;
    }

    uint8_t payload[IPC_MAX_PAYLOAD];
    if (payload_len > 0) {
        if (recv_all(client_fd, payload, payload_len) < 0)
            return -1;
    }

    switch (msg_type) {
    case IPC_MSG_SET_SENSOR_CFG:
        handle_set_sensor_cfg(client_fd, payload, payload_len);
        break;
    case IPC_MSG_GET_CONFIG:
        handle_get_config(client_fd);
        break;
    case IPC_MSG_SET_GENREG:
        handle_set_genreg(client_fd, payload, payload_len);
        break;
    case IPC_MSG_STREAM_CTRL:
        handle_stream_ctrl(client_fd, payload, payload_len, cid);
        break;
    default:
        LOG_WARN("ipc: unknown msg type 0x%02X", msg_type);
        send_reply(client_fd, IPC_MSG_REPLY_ERR, "unknown msg", 11);
        break;
    }

    return 0;
}

static void *client_worker(void *arg)
{
    struct client_ctx *ctx = (struct client_ctx *)arg;
    int      client_fd = ctx->fd;
    uint64_t cid       = ctx->cid;
    free(ctx);

    struct timeval tv = { .tv_sec = CLIENT_RECV_TIMEOUT, .tv_usec = 0 };
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    LOG_DBG("ipc: client worker started fd=%d", client_fd);

    while (g_running) {
        if (handle_client_message(client_fd, cid) < 0)
            break;
    }

    if (atomic_load(&g_stream_owner) == cid) {
        if (config_get_usb_active()) {
            LOG_INFO("ipc: controlling client (conn %llu) gone, but a USB host "
                     "owns the stream now -- leaving it running",
                     (unsigned long long)cid);
            atomic_store(&g_stream_owner, 0);
        } else {
            LOG_WARN("ipc: controlling client (conn %llu, fd=%d) gone with stream "
                     "active -- stopping pipeline",
                     (unsigned long long)cid, client_fd);
            config_set_stream(false);
            atomic_store(&g_stream_owner, 0);
        }
    }

    close(client_fd);
    LOG_DBG("ipc: client worker exiting fd=%d", client_fd);
    return NULL;
}

static void *ipc_listener_thread(void *arg)
{
    (void)arg;

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        LOG_ERR("ipc: epoll_create1: %s", strerror(errno));
        return NULL;
    }

    struct epoll_event ev;
    ev.events  = EPOLLIN;
    ev.data.fd = g_listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, g_listen_fd, &ev) < 0) {
        LOG_ERR("ipc: epoll_ctl ADD listen: %s", strerror(errno));
        close(epoll_fd);
        return NULL;
    }

    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) {
        LOG_ERR("ipc: pthread_attr_init: %s", strerror(errno));
        close(epoll_fd);
        return NULL;
    }
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    LOG_INFO("ipc: server ready on %s", CAMCFGD_SOCK_PATH);

    while (g_running) {
        struct epoll_event events[1];
        int nfds = epoll_wait(epoll_fd, events, 1, ACCEPT_POLL_TIMEOUT);

        if (nfds < 0) {
            if (errno == EINTR) continue;
            LOG_ERR("ipc: epoll_wait: %s", strerror(errno));
            break;
        }
        if (nfds == 0)
            continue;

        struct sockaddr_un addr;
        socklen_t addrlen = sizeof(addr);
        int client = accept(g_listen_fd,
                            (struct sockaddr *)&addr, &addrlen);
        if (client < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            LOG_WARN("ipc: accept: %s", strerror(errno));
            continue;
        }

        struct client_ctx *ctx = malloc(sizeof *ctx);
        if (!ctx) {
            LOG_ERR("ipc: malloc client_ctx for fd=%d: out of memory", client);
            close(client);
            continue;
        }
        ctx->fd  = client;
        ctx->cid = atomic_fetch_add(&g_next_conn_id, 1);

        pthread_t tid;
        if (pthread_create(&tid, &attr, client_worker, ctx) != 0) {
            LOG_ERR("ipc: pthread_create for fd=%d: %s",
                    client, strerror(errno));
            close(client);
            free(ctx);
        }
    }

    pthread_attr_destroy(&attr);
    close(epoll_fd);
    LOG_INFO("ipc: server stopped");
    return NULL;
}

int ipc_server_start(void)
{
    unlink(CAMCFGD_SOCK_PATH);

    g_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        LOG_ERR("ipc: socket: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", CAMCFGD_SOCK_PATH);

    if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERR("ipc: bind %s: %s", CAMCFGD_SOCK_PATH, strerror(errno));
        close(g_listen_fd);
        g_listen_fd = -1;
        return -1;
    }

    chmod(CAMCFGD_SOCK_PATH, 0666);

    if (listen(g_listen_fd, MAX_CLIENTS) < 0) {
        LOG_ERR("ipc: listen: %s", strerror(errno));
        close(g_listen_fd);
        g_listen_fd = -1;
        return -1;
    }

    g_running = 1;
    if (pthread_create(&g_thread, NULL, ipc_listener_thread, NULL) != 0) {
        LOG_ERR("ipc: pthread_create: %s", strerror(errno));
        close(g_listen_fd);
        g_listen_fd = -1;
        return -1;
    }

    return 0;
}

void ipc_server_stop(void)
{
    g_running = 0;
    pthread_join(g_thread, NULL);

    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }
    unlink(CAMCFGD_SOCK_PATH);

}
