#!/bin/sh
# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# rauc-hook.sh - RAUC bundle hook, runs on the camera during install
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

set -eu

BUNDLE_MEDIUM="@MEDIUM@"

case "${1:-}" in
install-check)
	if grep -qE '^[[:space:]]*type=nor' /etc/rauc/system.conf 2>/dev/null; then
		running=qspi
		running_label="QSPI flash (NOR)"
	else
		running=sd
		running_label="SD card"
	fi

	case "$BUNDLE_MEDIUM" in
	qspi) bundle_label="QSPI flash (NOR)" ;;
	sd)   bundle_label="SD card" ;;
	*)    bundle_label="$BUNDLE_MEDIUM" ;;
	esac

	if [ "$BUNDLE_MEDIUM" != "$running" ]; then
		echo "This firmware is built for a ${bundle_label} camera, but this camera boots from ${running_label}. Use the ${running_label} firmware file instead." 1>&2
		exit 10
	fi
	;;
slot-install)
	[ "${RAUC_SLOT_CLASS:-}" = "boot" ] || { echo "slot-install: unexpected slot class '${RAUC_SLOT_CLASS:-}'" 1>&2; exit 1; }
	dev="${RAUC_SLOT_DEVICE:?}"
	img="${RAUC_IMAGE_NAME:?}"
	work="$(mktemp -d /tmp/boot-layer.XXXXXX)"
	trap 'rm -rf "$work"' EXIT
	tar -xf "$img" -C "$work" || { echo "slot-install: cannot unpack $(basename "$img")" 1>&2; exit 1; }
	[ -f "$work/BOOT.BIN" ] || { echo "slot-install: boot-layer payload has no BOOT.BIN" 1>&2; exit 1; }
	want="$(md5sum < "$work/BOOT.BIN" | cut -d' ' -f1)"
	size=$(( $(wc -c < "$work/BOOT.BIN") ))
	case "$dev" in
	/dev/mtd*)
		[ "$size" -le 2097152 ] || { echo "slot-install: BOOT.BIN is $size bytes, larger than the 2 MB working-copy region" 1>&2; exit 1; }
		flashcp "$work/BOOT.BIN" "$dev" || { echo "slot-install: flashcp to $dev failed" 1>&2; exit 1; }
		got="$(dd if="$dev" bs=65536 count=$(( (size + 65535) / 65536 )) 2>/dev/null | head -c "$size" | md5sum | cut -d' ' -f1)"
		;;
	/dev/mmcblk*)
		mnt="$(awk -v d="$dev" '$1 == d { print $2; exit }' /proc/mounts)"
		if [ -z "$mnt" ]; then
			mnt="$work/mnt"; mkdir -p "$mnt"
			mount -t vfat "$dev" "$mnt" || { echo "slot-install: cannot mount $dev" 1>&2; exit 1; }
			trap 'umount "$mnt" 2>/dev/null; rm -rf "$work"' EXIT
		fi
		cp "$work/BOOT.BIN" "$mnt/BOOT.NEW" && sync && mv -f "$mnt/BOOT.NEW" "$mnt/BOOT.BIN" && sync \
			|| { echo "slot-install: writing BOOT.BIN to $mnt failed" 1>&2; exit 1; }
		if [ -f "$work/boot.scr" ]; then
			cp "$work/boot.scr" "$mnt/boot.new" && sync && mv -f "$mnt/boot.new" "$mnt/boot.scr" && sync \
				|| { echo "slot-install: writing boot.scr to $mnt failed" 1>&2; exit 1; }
		fi
		got="$(md5sum < "$mnt/BOOT.BIN" | cut -d' ' -f1)"
		;;
	*)
		echo "slot-install: unsupported boot slot device '$dev'" 1>&2; exit 1 ;;
	esac
	[ "$got" = "$want" ] || { echo "slot-install: BOOT.BIN read-back mismatch on $dev ($got != $want)" 1>&2; exit 1; }
	if [ -f "$work/env.txt" ]; then
		while IFS= read -r line; do
			case "$line" in ''|'#'*) continue ;; esac
			name="${line%%=*}"; value="${line#*=}"
			fw_setenv "$name" "$value" || { echo "slot-install: fw_setenv $name failed" 1>&2; exit 1; }
		done < "$work/env.txt"
	fi
	echo "slot-install: boot layer written to $dev (BOOT.BIN $size bytes, md5 $got)"
	;;
*)
	:
	;;
esac

exit 0
