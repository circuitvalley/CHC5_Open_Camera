// SPDX-License-Identifier: CC-BY-NC-ND-4.0
/*
 * camio_pub.c - read the camio configuration published by chc5_platformd
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#define _POSIX_C_SOURCE 200809L

#include "camio_pub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int camio_pub_poll(struct camio_cfg *out, uint32_t *mask)
{
    static struct timespec s_mtim;
    static ino_t  s_ino;
    static off_t  s_size;
    static int    s_seen;
    static struct camio_cfg s_last;

    struct stat st;
    if (stat(CAMIO_PUB_PATH, &st) != 0)
        return 0;
    if (s_seen && st.st_ino == s_ino && st.st_size == s_size &&
        st.st_mtim.tv_sec == s_mtim.tv_sec && st.st_mtim.tv_nsec == s_mtim.tv_nsec)
        return 0;

    FILE *f = fopen(CAMIO_PUB_PATH, "r");
    if (!f)
        return 0;

    struct camio_cfg c;
    memset(&c, 0, sizeof c);
    c.present = 1;

    char line[96];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#')
            continue;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        const char   *k = line;
        unsigned long v = strtoul(eq + 1, NULL, 10);
        if      (!strcmp(k, "in_function"))        c.in_function        = (uint8_t)v;
        else if (!strcmp(k, "in_activation"))      c.in_activation      = (uint8_t)v;
        else if (!strcmp(k, "in_invert"))          c.in_invert          = (uint8_t)v;
        else if (!strcmp(k, "trigger_delay_us"))   c.trigger_delay_ns   = chc5camio_us_to_ns((uint32_t)v);
        else if (!strcmp(k, "trigger_divider"))    c.trigger_divider    = (uint16_t)v;
        else if (!strcmp(k, "strobe_enable"))      c.strobe_enable      = (uint8_t)v;
        else if (!strcmp(k, "strobe_src"))         c.strobe_src         = (uint8_t)v;
        else if (!strcmp(k, "strobe_delay_us"))    c.strobe_delay_us    = (uint32_t)v;
        else if (!strcmp(k, "strobe_duration_us")) c.strobe_duration_us = (uint32_t)v;
        else if (!strcmp(k, "strobe_minon_us"))    c.strobe_minon_us    = (uint32_t)v;
        else if (!strcmp(k, "out_source"))         c.out_source         = (uint8_t)v;
        else if (!strcmp(k, "out_invert"))         c.out_invert         = (uint8_t)v;
        else if (!strcmp(k, "out_user_value"))     c.out_user_value     = (uint8_t)v;
        else if (!strcmp(k, "sync_role"))          c.sync_role          = (uint8_t)v;
        else if (!strcmp(k, "trig_src"))           c.trig_src           = v == 1 ? CHC5CAMIO_TRIG_SRC_SOFTWARE
                                                                                 : CHC5CAMIO_TRIG_SRC_LINE0;
        else if (!strcmp(k, "width_mode"))         c.width_mode         = v ? 1 : 0;
        else if (!strcmp(k, "in_filter_us"))       c.in_filter_ns       = v > 16777u ? 16777215u : (uint32_t)v * 1000u;
        else if (!strcmp(k, "sync_enable"))        c.sched_enable       = v ? 1 : 0;
        else if (!strcmp(k, "sync_rate_mhz"))      c.sched_period_ps    = chc5camio_mhz_to_ps((uint32_t)v);
    }
    fclose(f);

    *mask = s_seen ? camio_cfg_diff(&c, &s_last) : CAMIO_F_ALL;
    *mask &= ~(CAMIO_F(SYNC_ROLE_BY) | CAMIO_F(SYNC_TRIG_ROUTE));
    s_mtim  = st.st_mtim;
    s_ino   = st.st_ino;
    s_size  = st.st_size;
    s_seen  = 1;
    s_last  = c;
    *out = c;
    return *mask != 0;
}
