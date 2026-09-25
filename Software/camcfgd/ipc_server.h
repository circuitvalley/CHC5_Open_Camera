/* SPDX-License-Identifier: CC-BY-NC-ND-4.0 */
/*
 * ipc_server.h - Unix domain socket IPC server
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#ifndef CAMCFGD_IPC_SERVER_H
#define CAMCFGD_IPC_SERVER_H

int  ipc_server_start(void);

void ipc_server_stop(void);

#endif
