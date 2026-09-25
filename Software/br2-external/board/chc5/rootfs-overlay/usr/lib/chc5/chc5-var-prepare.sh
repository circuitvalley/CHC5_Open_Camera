#!/bin/sh
# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# chc5-var-prepare.sh - prepare and mount the data partition
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

set -u

MEDIUM="${1:-sd}"
FLAG=/run/chc5-var-degraded
log() { echo "chc5-var-prepare: $*"; }

if mountpoint -q /var; then
	log "/var already mounted"
	exit 0
fi

REFORMAT=0
if [ "$(fw_printenv -l /run -n chc5_var_reformat 2>/dev/null)" = "1" ]; then
	REFORMAT=1
	log "a factory reset asked for /var to be re-created"
fi

reformat_done() {
	fw_setenv -l /run chc5_var_reformat 2>/dev/null ||
		log "WARNING: could not clear chc5_var_reformat -- /var is re-created again at the next boot"
}

mounted() {
	log "/var mounted ($1)"
	exit 0
}

degraded() {
	log "ERROR: $1 -- running with /var in RAM (nothing is kept across a reboot)"
	mount -t tmpfs -o mode=0755,size=48m chc5-var /var ||
		log "ERROR: tmpfs on /var failed too"
	echo "$1" > "$FLAG"
	exit 0
}

prepare_qspi() {
	mtdnum=""
	for d in /sys/class/mtd/mtd*; do
		[ -f "$d/name" ] || continue
		if [ "$(cat "$d/name")" = "data" ]; then
			mtdnum=$(basename "$d" | sed 's/^mtd//')
			break
		fi
	done
	[ -n "$mtdnum" ] || degraded "no MTD partition labelled 'data'"

	if ! ubiattach -m "$mtdnum" -d 0 >/dev/null 2>&1; then
		log "data MTD $mtdnum is not UBI -- formatting (first boot)"
		ubiformat "/dev/mtd$mtdnum" -y -q || degraded "ubiformat of mtd$mtdnum failed"
		ubiattach -m "$mtdnum" -d 0 || degraded "ubiattach of mtd$mtdnum failed after formatting"
	fi

	if [ "$REFORMAT" = 1 ] && ubinfo /dev/ubi0 -N data >/dev/null 2>&1; then
		log "re-creating the UBIFS 'data' volume"
		reformat_done
		ubirmvol /dev/ubi0 -N data || degraded "removing the 'data' volume failed"
	fi
	if ! ubinfo /dev/ubi0 -N data >/dev/null 2>&1; then
		log "creating the UBIFS 'data' volume"
		ubimkvol /dev/ubi0 -N data -m || degraded "creating the 'data' volume failed"
	fi

	mount -t ubifs -o noatime ubi0:data /var && mounted "ubi0:data on mtd$mtdnum"
	degraded "UBIFS on ubi0:data (mtd$mtdnum) does not mount"
}

prepare_sd() {
	dev=/dev/mmcblk0p8
	[ -b "$dev" ] || degraded "no data partition ($dev)"
	if [ "$REFORMAT" = 1 ]; then
		log "re-creating ext4 on $dev"
		reformat_done
		mkfs.ext4 -q -F -L CHC5DATA "$dev" || degraded "mkfs.ext4 on $dev failed"
	else
		e2fsck -p "$dev"
		rc=$?
		if [ "$rc" -ge 4 ]; then
			log "e2fsck -p left errors (exit $rc) -- running e2fsck -y"
			e2fsck -y "$dev"
		fi
	fi
	mount -t ext4 -o noatime "$dev" /var && mounted "$dev"
	degraded "ext4 on $dev does not mount"
}

case "$MEDIUM" in
	qspi) prepare_qspi ;;
	*)    prepare_sd ;;
esac
