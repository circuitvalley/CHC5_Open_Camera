/*
 * version.h - Firmware version and build stamp macros
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */
#ifndef _VERSION_H_
#define _VERSION_H_

#define FW_VERSION_MAJOR    1
#define FW_VERSION_MINOR    1
#define FW_VERSION_PATCH    0

#if __has_include("version_gen.h")
#include "version_gen.h"
#endif

#ifndef FW_GIT_SHA
#define FW_GIT_SHA       "unknown"
#endif

#ifndef FW_GIT_DIRTY
#define FW_GIT_DIRTY     ""
#endif

#ifndef FW_BUILD_DATE
#define FW_BUILD_DATE    __DATE__ " " __TIME__
#endif

#ifndef FW_BUILD_NUM
#define FW_BUILD_NUM     0
#endif

#ifndef FW_GIT_REV
#define FW_GIT_REV       0
#endif

#define _FW_STR(x)       #x
#define _FW_XSTR(x)      _FW_STR(x)

#define FW_VERSION_STRING \
    _FW_XSTR(FW_VERSION_MAJOR) "." \
    _FW_XSTR(FW_VERSION_MINOR) "." \
    _FW_XSTR(FW_VERSION_PATCH)

#endif
