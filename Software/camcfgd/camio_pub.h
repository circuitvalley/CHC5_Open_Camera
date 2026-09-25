/* SPDX-License-Identifier: CC-BY-NC-ND-4.0 */
/*
 * camio_pub.h - read chc5_platformd's published aux I/O + sync (camio) config
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#ifndef CAMCFGD_CAMIO_PUB_H
#define CAMCFGD_CAMIO_PUB_H

#include "chc5_camio.h"

#ifndef CAMIO_PUB_PATH
#define CAMIO_PUB_PATH   "/run/chc5_platformd/camio"
#endif

int camio_pub_poll(struct camio_cfg *out);

#endif
