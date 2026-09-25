#!/bin/sh
# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# chc5-ptp.sh - PTP mode: ptp4l client on the Ethernet port
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

set -eu

CFG=/run/chc5_platformd/ptp4l.cfg
[ -r "$CFG" ] || CFG=/etc/linuxptp/chc5-hw-client.cfg

iface() {
	for n in /sys/class/net/*; do
		if readlink "$n/device/driver" 2>/dev/null | grep -q '/macb$'; then
			echo "${n##*/}"
			return 0
		fi
	done
	return 1
}

ptp_dev() {
	for c in /sys/class/ptp/ptp*; do
		if [ "$(cat "$c/clock_name" 2>/dev/null)" = "$1" ]; then
			echo "/dev/${c##*/}"
			return 0
		fi
	done
	return 1
}

if [ "${1:-}" = phc2sys ]; then
	phy=$(ptp_dev "Marvell PHY") || exit 0
	if [ "$(cat /sys/module/marvell_ptp/parameters/follow_mac_clock 2>/dev/null)" = Y ]; then
		exit 0
	fi
	if ! gem=$(ptp_dev "macb-zynq-ptp"); then
		echo "chc5-ptp: no Zynq GEM clock (interface down?)" >&2
		exit 1
	fi
	exec /usr/sbin/phc2sys -s "$phy" -c "$gem" -O 0 -S 0.001 \
		-P 0.05 -I 0.002 -R 2 -N 10 -m
fi

if [ "${1:-}" = stop ]; then
	if n=$(iface); then
		exec /usr/sbin/hwstamp_ctl -i "$n" -t 0 -r 0
	fi
	exit 0
fi

if n=$(iface); then
	echo "chc5-ptp: config $CFG" >&2
	if ptp_dev "Marvell PHY" >/dev/null; then
		exec /usr/sbin/ptp4l -f "$CFG" -i "$n" -m
	fi
	echo "chc5-ptp: no PHY timestamping (GEM fallback): PTP over Layer 2" >&2
	exec /usr/sbin/ptp4l -f "$CFG" -i "$n" -2 -m
fi

echo "chc5-ptp: no macb network interface found" >&2
exit 1
