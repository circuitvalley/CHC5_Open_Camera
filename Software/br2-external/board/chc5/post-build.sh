#!/bin/sh
# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# post-build.sh - Buildroot post-build: kernel modules and version stamps
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

set -e

TARGET_DIR="$1"

if [ -z "$TARGET_DIR" ]; then
    echo "post-build.sh: TARGET_DIR (first arg) is empty." >&2
    exit 1
fi

if [ -z "$CHC5_KDIR" ]; then
    echo "post-build.sh: CHC5_KDIR is not set." >&2
    echo "                Set it to the kernel source tree path. Example:" >&2
    echo "                  CHC5_KDIR=/path/to/linux make ..." >&2
    echo "                (The top-level chc5_package/Makefile does this for you.)" >&2
    exit 1
fi

if [ ! -f "$CHC5_KDIR/Makefile" ]; then
    echo "post-build.sh: CHC5_KDIR=$CHC5_KDIR does not look like a kernel tree." >&2
    exit 1
fi

if ! find "$CHC5_KDIR" -path "*/Documentation" -prune -o \
        -name '*.ko' -print 2>/dev/null | grep -q .; then
    echo "post-build.sh: no .ko files found under $CHC5_KDIR." >&2
    echo "                Build the kernel modules first ('make modules'" >&2
    echo "                in the kernel tree, or 'make kernel' at the" >&2
    echo "                chc5_package root)." >&2
    exit 1
fi

echo "post-build.sh: installing kernel modules from $CHC5_KDIR"

CROSS_COMPILE_PREFIX=""
if [ -n "$HOST_DIR" ]; then
    cgcc=$(ls "$HOST_DIR"/bin/*-linux-gnueabihf-gcc 2>/dev/null | head -n1)
    [ -n "$cgcc" ] && CROSS_COMPILE_PREFIX="${cgcc%gcc}"
fi

if [ -z "$CROSS_COMPILE_PREFIX" ]; then
    echo "post-build.sh: cannot find a cross-gcc under \$HOST_DIR/bin/" >&2
    echo "                (HOST_DIR='$HOST_DIR')" >&2
    echo "                Falling back to install WITHOUT stripping modules." >&2
    make -C "$CHC5_KDIR" \
        ARCH=arm \
        INSTALL_MOD_PATH="$TARGET_DIR" \
        modules_install >/dev/null
else
    make -C "$CHC5_KDIR" \
        ARCH=arm \
        CROSS_COMPILE="$CROSS_COMPILE_PREFIX" \
        INSTALL_MOD_PATH="$TARGET_DIR" \
        INSTALL_MOD_STRIP=1 \
        modules_install >/dev/null
fi

KVER=$(cat "$CHC5_KDIR/include/config/kernel.release" 2>/dev/null)
[ -z "$KVER" ] && KVER=$(make -s -C "$CHC5_KDIR" ARCH=arm kernelrelease 2>/dev/null)
if [ -z "$KVER" ] || [ ! -d "$TARGET_DIR/lib/modules/$KVER" ]; then
    echo "post-build.sh: cannot determine the current kernel module tree." >&2
    exit 1
fi

for d in "$TARGET_DIR"/lib/modules/*/; do
    v=$(basename "$d")
    [ "$v" = "$KVER" ] && continue
    echo "post-build.sh: pruning stale module tree $v"
    rm -rf "$d"
done

rm -f "$TARGET_DIR/lib/modules/$KVER/kernel/drivers/media/i2c/"*.ko
echo "post-build.sh: stripped sensor modules from /lib/modules (deployed via archives)"

depmod -b "$TARGET_DIR" "$KVER"

echo "post-build.sh: kernel modules installed for $KVER, depmod done"

MONOREPO_VER="1.0.0"
REPO_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BUILD_DATE="$(date -u '+%Y-%m-%d %H:%M:%S UTC')"
if git -C "$REPO_ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    GIT_SHA="$(git -C "$REPO_ROOT" rev-parse --short=8 HEAD 2>/dev/null || echo unknown)"
    GIT_DIRTY="$("$REPO_ROOT/scripts/git-dirty.sh" "$REPO_ROOT")"
    GIT_REV="$(git -C "$REPO_ROOT" rev-list --count HEAD 2>/dev/null || echo 0)"
else
    GIT_SHA="unknown"
    GIT_DIRTY=""
    GIT_REV="0"
fi
mkdir -p "$TARGET_DIR/etc"
printf '%s-g%s%s rev %s (%s)\n' "$MONOREPO_VER" "$GIT_SHA" "$GIT_DIRTY" "$GIT_REV" "$BUILD_DATE" \
    > "$TARGET_DIR/etc/chc5-version"
echo "post-build.sh: /etc/chc5-version = $(cat "$TARGET_DIR/etc/chc5-version")"

echo "3" > "$TARGET_DIR/etc/chc5-boot-env-required"
echo "post-build.sh: /etc/chc5-boot-env-required = $(cat "$TARGET_DIR/etc/chc5-boot-env-required")"

date -u +%s > "$TARGET_DIR/etc/chc5-clock-default"
echo "post-build.sh: /etc/chc5-clock-default = $(cat "$TARGET_DIR/etc/chc5-clock-default")"

mkdir -p "$TARGET_DIR/etc/chc5"
chmod 0755 "$TARGET_DIR/etc/chc5"
echo "post-build.sh: /etc/chc5 mount point created"
