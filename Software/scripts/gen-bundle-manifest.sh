#!/bin/sh
# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# gen-bundle-manifest.sh - write the RAUC bundle manifest
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

set -eu

REPO="$1"
TGT="$2"
COMPAT="$3"
ROOTFS_FN="$4"
KERNEL_FN="$5"
OUT="$6"
MEDIUM="${7:-}"

VERSION="$(head -n1 "$TGT/etc/chc5-version" 2>/dev/null || true)"
[ -n "$VERSION" ] || VERSION="unknown"

BUILD="$(date -u '+%Y-%m-%d %H:%M:%S UTC')"
FLAVOR="$(head -n1 "$TGT/etc/chc5-flavor" 2>/dev/null || true)"
[ -n "$FLAVOR" ] || FLAVOR="unknown"
case "$FLAVOR" in
  dev)  FLAVOR_TEXT="Flavour: dev -- this image accepts official releases and bundles signed with the open dev key from the source repo." ;;
  prod) FLAVOR_TEXT="Flavour: production -- this image accepts official releases only." ;;
  debug) FLAVOR_TEXT="Flavour: debug -- this image accepts bundles signed with the open dev key only." ;;
  *)    FLAVOR_TEXT="Flavour: unknown." ;;
esac

FJSON="$TGT/usr/share/chc5/factory/factory.json"
jget() {
	[ -f "$FJSON" ] || { echo ""; return; }
	python3 -c "import json,sys;print(json.load(open('$FJSON')).get('$1',''))" \
		2>/dev/null || echo ""
}
FAC_BS="$(jget bitstream)"
FAC_SN="$(jget sensor)"
FAC_USB="$(jget usb_fw)"

BOOT_ENV_REQ="$(head -n1 "$TGT/etc/chc5-boot-env-required" 2>/dev/null || true)"
[ -n "$BOOT_ENV_REQ" ] || BOOT_ENV_REQ="0"

rev() { git -C "$REPO/$1" describe --tags --always --dirty 2>/dev/null || echo "unknown"; }
GIT_LINUX="$(rev linux-xlnx)"
GIT_UBOOT="$(rev u-boot-xlnx)"
GIT_PD="$(rev chc5_platformd)"
GIT_WEBD="$(rev chc5_webd)"
GIT_CAM="$(rev camcfgd)"
GIT_GVCP="$(rev gvcp_server)"
GIT_SUPER="$(git -C "$REPO" describe --tags --always --dirty 2>/dev/null || echo unknown)"

KIND="${MANIFEST_KIND:-system}"
if [ "$KIND" = "boot" ]; then
DESC="CHC5 boot-layer update (RARE) -- BOOT.BIN (FSBL + u-boot) and the A/B boot script/env stamps, written in place by the bundle's own hook. Does not change the A/B slots, counters or active system; takes effect at the next reboot. On QSPI only the working copy is rewritten, the golden copy at 2 MB stays as the fallback."
else
DESC="CHC5 camera A/B system update -- Linux kernel + root filesystem, written to the inactive slot and activated on the next reboot (auto-rollback if it fails to boot). Factory defaults baked into this image -- bitstream: ${FAC_BS:-n/a}, sensor: ${FAC_SN:-n/a}, USB firmware: ${FAC_USB:-n/a}. $FLAVOR_TEXT"
fi

rm -f "$OUT"
cat > "$OUT" <<EOF
[update]
compatible=$COMPAT
version=$VERSION
description=$DESC
build=$BUILD

[bundle]
format=plain

[meta.system]
chc5_version=$VERSION
build=$BUILD
flavor=$FLAVOR

[meta.components]
linux=$GIT_LINUX
uboot=$GIT_UBOOT
platformd=$GIT_PD
webd=$GIT_WEBD
camcfgd=$GIT_CAM
gvcp=$GIT_GVCP
superproject=$GIT_SUPER

[meta.factory]
bitstream=$FAC_BS
sensor=$FAC_SN
usb_fw=$FAC_USB

[meta.boot_env]
required_gen=$BOOT_ENV_REQ
EOF
if [ "$KIND" = "boot" ]; then
	cat >> "$OUT" <<EOF

[image.boot]
filename=$ROOTFS_FN
hooks=install
EOF
else
	cat >> "$OUT" <<EOF

[image.rootfs]
filename=$ROOTFS_FN

[image.kernel]
filename=$KERNEL_FN
EOF
fi

if [ -n "$MEDIUM" ]; then
	cat >> "$OUT" <<EOF

[hooks]
filename=hook.sh
hooks=install-check

[meta.medium]
kind=$MEDIUM
EOF
fi

echo "gen-bundle-manifest: $OUT  (kind=$KIND, version='$VERSION', medium='${MEDIUM:-none}')"
