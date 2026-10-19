#!/bin/sh
# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# post-image-qspi.sh - Buildroot post-image for the QSPI update
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

set -e
export CHC5_MEDIUM=qspi
exec "$(dirname "$0")/post-image.sh" "$@"
