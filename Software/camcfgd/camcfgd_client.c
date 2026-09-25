// SPDX-License-Identifier: CC-BY-NC-ND-4.0
/*
 * camcfgd_client.c - IPC client library for camcfgd
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "camcfgd_client.h"

static int g_fd = -1;
static pthread_mutex_t g_client_lock = PTHREAD_MUTEX_INITIALIZER;

static void drop_connection(void)
{
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
}

static int client_send_all(const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(g_fd, p + sent, len - sent);
        if (n < 0) {
            if (errno == EINTR) continue;
            drop_connection();
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static int client_recv_all(void *buf, size_t len)
{
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    while (got < len) {
        ssize_t n = read(g_fd, p + got, len - got);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            drop_connection();
            return -1;
        }
        got += (size_t)n;
    }
    return 0;
}

static int send_msg(uint8_t msg_type, const void *payload, uint16_t payload_len)
{
    uint8_t hdr[3];
    hdr[0] = msg_type;
    hdr[1] = (uint8_t)(payload_len >> 8);
    hdr[2] = (uint8_t)(payload_len & 0xFF);

    if (client_send_all(hdr, 3) < 0)
        return -1;
    if (payload_len > 0 && payload)
        return client_send_all(payload, payload_len);
    return 0;
}

static int recv_reply(uint8_t *msg_type, void *payload, uint16_t *payload_len,
                      uint16_t max_payload)
{
    uint8_t hdr[3];
    if (client_recv_all(hdr, 3) < 0)
        return -1;

    *msg_type = hdr[0];
    uint16_t plen = ((uint16_t)hdr[1] << 8) | hdr[2];

    if (plen > max_payload) {
        uint8_t drain[256];
        uint16_t remaining = plen;
        while (remaining > 0) {
            uint16_t chunk = remaining > 256 ? 256 : remaining;
            if (client_recv_all(drain, chunk) < 0)
                return -1;
            remaining -= chunk;
        }
        *payload_len = 0;
        return -1;
    }

    if (plen > 0 && payload) {
        if (client_recv_all(payload, plen) < 0)
            return -1;
    }
    *payload_len = plen;
    return 0;
}

static int try_connect_locked(void)
{
    if (g_fd >= 0)
        return 0;

    g_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_fd < 0)
        return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", CAMCFGD_SOCK_PATH);

    if (connect(g_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
        setsockopt(g_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        return 0;
    }

    close(g_fd);
    g_fd = -1;
    return -1;
}

static int ensure_connected(void)
{
    if (g_fd >= 0)
        return 0;
    return try_connect_locked();
}

int camcfgd_connect(void)
{
    pthread_mutex_lock(&g_client_lock);

    if (g_fd >= 0) {
        pthread_mutex_unlock(&g_client_lock);
        return 0;
    }

    for (int attempt = 0; attempt < 50; attempt++) {
        if (try_connect_locked() == 0) {
            pthread_mutex_unlock(&g_client_lock);
            return 0;
        }
        usleep(100000);
    }

    pthread_mutex_unlock(&g_client_lock);
    return -1;
}

void camcfgd_disconnect(void)
{
    pthread_mutex_lock(&g_client_lock);
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
    pthread_mutex_unlock(&g_client_lock);
}

int camcfgd_is_connected(void)
{
    pthread_mutex_lock(&g_client_lock);
    int connected = (g_fd >= 0);
    pthread_mutex_unlock(&g_client_lock);
    return connected;
}

int camcfgd_send_sensor_cfg(const imgsensor_cfg_t *cfg)
{
    pthread_mutex_lock(&g_client_lock);
    if (ensure_connected() < 0) { pthread_mutex_unlock(&g_client_lock); return -1; }

    int ret = send_msg(IPC_MSG_SET_SENSOR_CFG, cfg,
                       (uint16_t)sizeof(*cfg));
    if (ret == 0) {
        uint8_t rtype;
        uint16_t rlen;
        ret = recv_reply(&rtype, NULL, &rlen, 0);
        if (rtype != IPC_MSG_REPLY_OK)
            ret = -1;
    }

    pthread_mutex_unlock(&g_client_lock);
    return ret;
}

int camcfgd_send_genreg(uint32_t addr, const uint8_t *value, int len)
{
    if (len <= 0 || len > 8)
        return -1;

    pthread_mutex_lock(&g_client_lock);
    if (ensure_connected() < 0) { pthread_mutex_unlock(&g_client_lock); return -1; }

    uint8_t payload[12];
    payload[0] = (uint8_t)(addr >> 24);
    payload[1] = (uint8_t)(addr >> 16);
    payload[2] = (uint8_t)(addr >>  8);
    payload[3] = (uint8_t)(addr);
    memcpy(payload + 4, value, len);

    int ret = send_msg(IPC_MSG_SET_GENREG, payload, (uint16_t)(4 + len));
    if (ret == 0) {
        uint8_t rtype;
        uint16_t rlen;
        ret = recv_reply(&rtype, NULL, &rlen, 0);
    }

    pthread_mutex_unlock(&g_client_lock);
    return ret;
}

int camcfgd_stream_ctrl(int enable)
{
    pthread_mutex_lock(&g_client_lock);
    if (ensure_connected() < 0) { pthread_mutex_unlock(&g_client_lock); return -1; }

    uint8_t val = enable ? 1 : 0;
    int ret = send_msg(IPC_MSG_STREAM_CTRL, &val, 1);
    if (ret == 0) {
        uint8_t rtype;
        uint16_t rlen;
        ret = recv_reply(&rtype, NULL, &rlen, 0);
    }

    pthread_mutex_unlock(&g_client_lock);
    return ret;
}

int camcfgd_get_config(struct camcfg_state_s *out)
{
    pthread_mutex_lock(&g_client_lock);
    if (ensure_connected() < 0) { pthread_mutex_unlock(&g_client_lock); return -1; }

    int ret = send_msg(IPC_MSG_GET_CONFIG, NULL, 0);
    if (ret == 0) {
        uint8_t rtype;
        uint16_t rlen;
        ret = recv_reply(&rtype, out, &rlen, (uint16_t)sizeof(*out));
        if (rtype != IPC_MSG_REPLY_OK || rlen != sizeof(*out))
            ret = -1;
    }

    pthread_mutex_unlock(&g_client_lock);
    return ret;
}
