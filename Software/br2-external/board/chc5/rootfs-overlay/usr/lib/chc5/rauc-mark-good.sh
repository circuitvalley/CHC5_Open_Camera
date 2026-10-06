#!/bin/sh
# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# rauc-mark-good.sh - boot confirmation after an update
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

set -u

HEALTH_TIMEOUT=300
CONTROL_DAEMONS="chc5_platformd chc5_webd"

all_active() {
	for svc in ${CONTROL_DAEMONS}; do
		systemctl is-active --quiet "${svc}" || return 1
	done
	return 0
}

sync_card_script() {
	src=/usr/share/chc5/chc5sd.scr
	dst=/boot/chc5sd.scr
	case " $(cat /proc/cmdline) " in *" root=/dev/mmcblk0p"*) ;; *) return 0 ;; esac
	[ -f "${src}" ] && mountpoint -q /boot || return 0
	cmp -s "${src}" "${dst}" && return 0
	if cp "${src}" /boot/chc5sd.new && sync && mv -f /boot/chc5sd.new "${dst}" && sync \
	   && cmp -s "${src}" "${dst}"; then
		echo "chc5-mark-good: the card's chc5sd.scr updated from this slot"
	else
		rm -f /boot/chc5sd.new
		echo "chc5-mark-good: could NOT update the card's chc5sd.scr (old one kept)" >&2
	fi
}

i=0
while ! all_active; do
	i=$((i + 1))
	if [ "${i}" -ge "${HEALTH_TIMEOUT}" ]; then
		probation=$(fw_printenv -n upgrade_available 2>/dev/null || echo 0)
		if [ "${probation}" = "1" ]; then
			echo "chc5-mark-good: control daemons not up after ${HEALTH_TIMEOUT}s in" \
			     "the post-update window -- marking this update BAD and rebooting to" \
			     "roll back to the previous slot" >&2
			rauc status mark-bad || \
				echo "chc5-mark-good: 'rauc status mark-bad' failed -- relying on the" \
				     "boot-attempt counter to fall back" >&2
			sync
			systemctl reboot
			exit 0
		fi
		echo "chc5-mark-good: control daemons not up after ${HEALTH_TIMEOUT}s" \
		     "(normal operation, no pending update) -- NOT rebooting; leaving the" \
		     "system up for remote/manual recovery" >&2
		exit 1
	fi
	sleep 1
done

if rauc status mark-good; then
	if fw_setenv upgrade_available 0; then
		echo "chc5-mark-good: current slot confirmed good (probation cleared)"
	else
		echo "chc5-mark-good: slot good but could NOT clear upgrade_available --" \
		     "boot attempts will keep counting; check fw_setenv / fw_env.config" >&2
	fi
	sync_card_script
else
	rc=$?
	echo "chc5-mark-good: 'rauc status mark-good' failed (rc=${rc})" >&2
	exit "${rc}"
fi
