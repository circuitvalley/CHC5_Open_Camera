#!/bin/sh
# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# chc5-sensor-load.sh - Apply the staged sensor overlay at boot
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

set -u

LIB_DIR_BS="/var/lib/chc5_platformd/bitstreams"
LIB_DIR_SN="/var/lib/chc5_platformd/sensors"

CURRENT_FILE_BS="$LIB_DIR_BS/.current"
CURRENT_FILE_SN="$LIB_DIR_SN/.current"

ACTIVE_FILE_BS="/run/chc5_platformd/bitstream.active"
ACTIVE_FILE_SN="/run/chc5_platformd/sensor.active"
KMOD_MARKER="/run/chc5_platformd/sensor.kmod"
LOAD_DONE_FILE="/run/chc5_platformd/sensor.load_done"

OVERLAY_SRC="/lib/firmware/chc5-active-sensor-overlay.dtbo"
CONFIGFS_OVERLAY_DIR="/sys/kernel/config/device-tree/overlays"
OVERLAY_NODE="$CONFIGFS_OVERLAY_DIR/chc5_sensor"

log() {
    echo "[chc5-sensor-load] $*"
}

done_exit() {
    code="${1:-0}"
    : > "$LOAD_DONE_FILE"
    log "done: bitstream.active='$(cat $ACTIVE_FILE_BS 2>/dev/null)' sensor.active='$(cat $ACTIVE_FILE_SN 2>/dev/null)' exit=$code"
    exit "$code"
}

mkdir -p /run/chc5_platformd
: > "$ACTIVE_FILE_BS"
: > "$ACTIVE_FILE_SN"

BS_NAME=""
SN_NAME=""
[ -s "$CURRENT_FILE_BS" ] && BS_NAME=$(head -1 "$CURRENT_FILE_BS" | tr -d '\r\n')
[ -s "$CURRENT_FILE_SN" ] && SN_NAME=$(head -1 "$CURRENT_FILE_SN" | tr -d '\r\n')

EFFECTIVE_FILE_SN="/run/chc5_platformd/sensor.effective"
if [ -s "$EFFECTIVE_FILE_SN" ]; then
    EFF=$(head -1 "$EFFECTIVE_FILE_SN" | tr -d '\r\n')
    if [ -n "$EFF" ] && [ "$EFF" != "$SN_NAME" ]; then
        log "boot fallback: configured sensor '$SN_NAME' replaced by '$EFF' this boot ($(sed -n 2p "$EFFECTIVE_FILE_SN"))"
        SN_NAME="$EFF"
    fi
fi

if [ -z "$SN_NAME" ]; then
    log "no sensor .current marker -- nothing to load"
    done_exit 0
fi

log "applying overlay for sensor '$SN_NAME' (bitstream='$BS_NAME')"

if [ ! -f "$OVERLAY_SRC" ]; then
    log "$OVERLAY_SRC missing -- was sensor activation completed?"
    done_exit 0
fi
if [ ! -d "$CONFIGFS_OVERLAY_DIR" ]; then
    log "configfs DT overlay dir missing -- kernel without CONFIG_OF_OVERLAY?"
    done_exit 0
fi

mkdir -p "$OVERLAY_NODE"
OVERLAY_REL=${OVERLAY_SRC#/lib/firmware/}

if ! echo "$OVERLAY_REL" > "$OVERLAY_NODE/path" 2>&1; then
    log "overlay apply failed (echo > $OVERLAY_NODE/path)"
    rmdir "$OVERLAY_NODE" 2>/dev/null
    done_exit 1
fi

for i in 1 2 3 4 5 6 7 8 9 10; do
    STATUS=$(cat "$OVERLAY_NODE/status" 2>/dev/null)
    if [ "$STATUS" = "applied" ]; then
        break
    fi
    sleep 0.5
done
STATUS=$(cat "$OVERLAY_NODE/status" 2>/dev/null)
if [ "$STATUS" != "applied" ]; then
    log "overlay status='$STATUS' (expected 'applied')"
    rmdir "$OVERLAY_NODE" 2>/dev/null
    done_exit 1
fi

log "overlay applied -- FPGA reconfigured, IP + sensor nodes declared"
[ -n "$BS_NAME" ] && echo "$BS_NAME" > "$ACTIVE_FILE_BS"

udevadm settle --timeout=5 2>/dev/null || sleep 2

EXPECT_KMOD=""
[ -s "$KMOD_MARKER" ] && EXPECT_KMOD=$(head -1 "$KMOD_MARKER" | tr -d '\r\n')

BOUND_DRV=""
BOUND_DEV=""
for dev_dir in /sys/bus/i2c/devices/*; do
    [ -L "$dev_dir/driver" ] || continue
    drv=$(basename "$(readlink -f "$dev_dir/driver")")
    if [ -n "$EXPECT_KMOD" ] && [ "$drv" != "$EXPECT_KMOD" ]; then
        continue
    fi
    BOUND_DRV="$drv"
    BOUND_DEV=$(basename "$dev_dir")
    break
done

if [ -z "$BOUND_DRV" ] && [ -n "$EXPECT_KMOD" ]; then
    for dev_dir in /sys/bus/platform/devices/*; do
        [ -L "$dev_dir/driver" ] || continue
        drv=$(basename "$(readlink -f "$dev_dir/driver")")
        [ "$drv" = "$EXPECT_KMOD" ] || continue
        BOUND_DRV="$drv"
        BOUND_DEV=$(basename "$dev_dir")
        break
    done
fi

if [ -n "$BOUND_DRV" ]; then
    echo "$SN_NAME" > "$ACTIVE_FILE_SN"
    log "sensor '$SN_NAME' active (driver='$BOUND_DRV' bound to device '$BOUND_DEV')"

    if [ -e /dev/media0 ]; then
        log "media device present: pipeline came up"
    else
        log "WARNING: sensor bound but NO /dev/media0 after 5s -- the video"
        log "         pipeline did not come up, so this camera cannot produce"
        log "         a frame.  Check that every block in the overlay has a"
        log "         driver claiming its compatible:"
        log "           for d in /sys/bus/platform/devices/*; do [ -L \$d/driver ] || echo \$d; done"
    fi
    done_exit 0
fi

log "no sensor driver bound (i2c or platform) after overlay apply + udev settle"
if [ -n "$EXPECT_KMOD" ]; then
    log "expected driver: '$EXPECT_KMOD'"
fi
log "possible causes: depmod stale, modalias missing in .ko, udev not running, probe failure"
done_exit 1
