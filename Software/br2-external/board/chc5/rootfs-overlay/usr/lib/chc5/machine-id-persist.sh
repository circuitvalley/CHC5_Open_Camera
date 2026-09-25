#!/bin/sh
# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# machine-id-persist.sh - keep /etc/machine-id across boots
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

set -u

PERSIST=/var/lib/chc5-rw/machine-id
MID=/etc/machine-id

if [ ! -s "$PERSIST" ] && [ -s "$MID" ]; then
	mkdir -p "$(dirname "$PERSIST")"
	cat "$MID" > "$PERSIST"
	chmod 0444 "$PERSIST"
fi

if [ -s "$PERSIST" ] && [ "$(cat "$MID" 2>/dev/null)" != "$(cat "$PERSIST")" ]; then
	mount --bind "$PERSIST" "$MID"
fi
