#!/bin/sh
# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# hostname-persist.sh - re-apply the per-unit hostname on every boot
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

set -u

PERSIST=/var/lib/chc5-rw/hostname
ETC=/etc/hostname

[ -s "$PERSIST" ] || exit 0

hostname "$(cat "$PERSIST")" 2>/dev/null || true

if [ "$(cat "$ETC" 2>/dev/null)" != "$(cat "$PERSIST")" ]; then
	mount --bind "$PERSIST" "$ETC" 2>/dev/null || true
fi
exit 0
