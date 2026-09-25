#!/bin/sh
# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# chc5-clock-floor.sh - lift the system clock to a sane floor at boot
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

DEFAULT_FILE=/etc/chc5-clock-default
PERSIST_FILE=/var/lib/chc5_platformd/clock

is_uint() { case "$1" in ''|*[!0-9]*) return 1 ;; *) return 0 ;; esac; }

floor=0
if [ -r "$DEFAULT_FILE" ]; then
    d=$(cat "$DEFAULT_FILE" 2>/dev/null)
    is_uint "$d" && floor="$d"
fi
if [ -r "$PERSIST_FILE" ]; then
    p=$(cat "$PERSIST_FILE" 2>/dev/null)
    if is_uint "$p" && [ "$p" -gt "$floor" ]; then
        floor="$p"
    fi
fi

now=$(date -u +%s 2>/dev/null)
is_uint "$now" || now=0

if [ "$now" -lt "$floor" ]; then
    if date -u -s "@$floor" >/dev/null 2>&1; then
        echo "chc5-clock-floor: clock ${now} < floor ${floor} -> set clock to ${floor}"
    else
        echo "chc5-clock-floor: failed to set clock to ${floor}" >&2
    fi
else
    echo "chc5-clock-floor: clock ${now} >= floor ${floor} -> no change"
fi

exit 0
