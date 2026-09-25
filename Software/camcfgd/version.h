/* SPDX-License-Identifier: CC-BY-NC-ND-4.0 */
/*
 * version.h - Build version information for camcfgd
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#ifndef CAMCFGD_VERSION_H
#define CAMCFGD_VERSION_H

#define CAMCFGD_VERSION_MAJOR   0
#define CAMCFGD_VERSION_MINOR   1
#define CAMCFGD_VERSION_PATCH   0

#if __has_include("version_gen.h")
#include "version_gen.h"
#endif

#ifndef CAMCFGD_GIT_SHA
#define CAMCFGD_GIT_SHA     "unknown"
#endif

#ifndef CAMCFGD_GIT_DIRTY
#define CAMCFGD_GIT_DIRTY   ""
#endif

#ifndef CAMCFGD_BUILD_DATE
#define CAMCFGD_BUILD_DATE  __DATE__ " " __TIME__
#endif

#ifndef CAMCFGD_GIT_REV
#define CAMCFGD_GIT_REV     0
#endif

#ifndef CAMCFGD_BUILD_NUM
#define CAMCFGD_BUILD_NUM   0
#endif

#define _CAMCFGD_STR(x)   #x
#define _CAMCFGD_XSTR(x)  _CAMCFGD_STR(x)

#define CAMCFGD_VERSION_STRING \
    _CAMCFGD_XSTR(CAMCFGD_VERSION_MAJOR) "." \
    _CAMCFGD_XSTR(CAMCFGD_VERSION_MINOR) "." \
    _CAMCFGD_XSTR(CAMCFGD_VERSION_PATCH)

#endif
