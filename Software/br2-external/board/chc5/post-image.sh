#!/bin/sh
# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# post-image.sh - Buildroot post-image: build kernel.itb
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

set -e

BINARIES_DIR="$1"
BOARD_DIR="$(cd "$(dirname "$0")" && pwd)"
TOP="$(cd "${BR2_EXTERNAL_CHC5_PATH}/.." && pwd)"
KDIR="${TOP}/linux-xlnx"
MKIMAGE="${HOST_DIR}/bin/mkimage"
[ -x "${MKIMAGE}" ] || MKIMAGE="$(command -v mkimage)" || { echo "post-image: mkimage not found"; exit 1; }
ZIMAGE="${KDIR}/arch/arm/boot/zImage"
DTB="${KDIR}/arch/arm/boot/dts/xilinx/zynq-chc5.dtb"

[ -f "${ZIMAGE}" ] || { echo "post-image: missing ${ZIMAGE} (run 'make kernel' first)"; exit 1; }
[ -f "${DTB}" ] || { echo "post-image: missing ${DTB} (run 'make kernel' first)"; exit 1; }

WORK="$(mktemp -d)"
cp -f "${ZIMAGE}" "${WORK}/zImage"
cp -f "${DTB}" "${WORK}/zynq-chc5.dtb"
cp -f "${BOARD_DIR}/kernel.its" "${WORK}/kernel.its"
( cd "${WORK}" && "${MKIMAGE}" -f kernel.its kernel.itb )
cp -f "${WORK}/kernel.itb" "${BINARIES_DIR}/kernel.itb"
rm -rf "${WORK}"

if [ "${CHC5_GENIMAGE_CFG:-}" = "genimage-debug.cfg" ]; then
	echo "sd-debug" > "${BINARIES_DIR}/.medium"
else
	echo "sd" > "${BINARIES_DIR}/.medium"
fi
echo "post-image: kernel.itb ready in ${BINARIES_DIR} (medium $(cat "${BINARIES_DIR}/.medium"))"
