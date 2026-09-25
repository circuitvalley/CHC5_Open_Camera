#!/bin/sh
# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# ro-root-rwlayers.sh - writable layers on the read-only root
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

set -e

DATA=/var/lib/chc5-rw

mkdir -p \
	"${DATA}/firmware/upper" "${DATA}/firmware/work" \
	"${DATA}/modules/upper"  "${DATA}/modules/work" \
	"${DATA}/etc-chc5"

if ! mountpoint -q /lib/firmware; then
	mount -t overlay chc5-fw \
		-o lowerdir=/lib/firmware,upperdir=${DATA}/firmware/upper,workdir=${DATA}/firmware/work \
		/lib/firmware
fi

if ! mountpoint -q /lib/modules; then
	mount -t overlay chc5-mod \
		-o lowerdir=/lib/modules,upperdir=${DATA}/modules/upper,workdir=${DATA}/modules/work \
		/lib/modules
fi

if ! mountpoint -q /etc/chc5; then
	[ -d /etc/chc5 ] && cp -an /etc/chc5/. "${DATA}/etc-chc5/" 2>/dev/null || true
	mount --bind "${DATA}/etc-chc5" /etc/chc5
fi
