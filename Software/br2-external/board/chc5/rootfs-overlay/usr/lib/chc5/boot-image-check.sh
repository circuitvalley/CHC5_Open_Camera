#!/bin/sh
# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# boot-image-check.sh - record which boot image copy was used
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

OUT=/run/chc5-boot-image
mb=$(devmem 0xF800702C 32 2>/dev/null) || { echo "unknown" > "$OUT"; exit 0; }
bm=$(devmem 0xF800025C 32 2>/dev/null) || bm=0
mb=$(( mb & 0x1FFF )); bm=$(( bm & 0xF ))
case "$bm" in 1) medium=qspi ;; 5) medium=sd ;; *) medium="mode$bm" ;; esac
if [ "$mb" -eq 0 ]; then
	echo "primary ($medium)" > "$OUT"
	exit 0
fi
off=$(( mb * 32768 ))
printf 'golden (%s, MULTIBOOT_ADDR=%d, offset 0x%X)\n' "$medium" "$mb" "$off" > "$OUT"
logger -t chc5-boot-image -p daemon.warning \
	"booted from the GOLDEN BOOT.BIN copy at 0x$(printf %X "$off") (MULTIBOOT_ADDR=$mb): the working copy at offset 0 is damaged -- re-flash the boot layer"
exit 0
