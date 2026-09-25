#!/bin/sh
# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# post-image-debug.sh - Buildroot post-image for the debug image
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

set -e
export CHC5_GENIMAGE_CFG=genimage-debug.cfg
exec "$(dirname "$0")/post-image.sh" "$@"
