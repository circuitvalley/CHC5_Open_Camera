#!/bin/sh
# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# chc5-provision.sh - first-boot per-unit provisioning
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

set -e

DATA=/var/lib/chc5-rw
MARK="${DATA}/.provisioned"
[ -e "${MARK}" ] && exit 0
mkdir -p "${DATA}"

if [ ! -s /etc/machine-id ]; then
	systemd-machine-id-setup 2>/dev/null || \
		tr -d - < /proc/sys/kernel/random/uuid > /etc/machine-id
fi

IFACE=$(ls /sys/class/net | grep -E '^(end0|eth0)$' | head -1)
MAC=$(cat "/sys/class/net/${IFACE}/address" 2>/dev/null || echo "00:00:00:00:00:00")
SERIAL="chc5-$(echo "${MAC}" | tr -d ':' | tr 'A-F' 'a-f' | tail -c 7)"
hostname "${SERIAL}" 2>/dev/null || true
printf '%s\n' "${SERIAL}" > "${DATA}/hostname"

FACTORY_CRED="${DATA}/factory-cred"

cred_hash=""
cred_src="${FACTORY_CRED}"
if [ -s "${FACTORY_CRED}" ]; then
	cred_mac=$(sed -n 's/^mac=//p'  "${FACTORY_CRED}" | head -1)
	cred_hash=$(sed -n 's/^hash=//p' "${FACTORY_CRED}" | head -1)
	if [ -n "${cred_mac}" ] && [ "${cred_mac}" != "${MAC}" ]; then
		echo "CHC5-PROVISION: factory-cred belongs to ${cred_mac}, this board is ${MAC} -- ignoring it"
		cred_hash=""
	fi
fi

reserve_dev() {
	for d in /sys/class/mtd/mtd*; do
		case "$d" in *ro) continue ;; esac
		[ -f "$d/name" ] || continue
		if [ "$(cat "$d/name")" = "reserve" ]; then
			echo "/dev/$(basename "$d")"
			return 0
		fi
	done
	if grep -q '^/dev/mmcblk0p8 /var ' /proc/mounts && [ -b /dev/mmcblk0p5 ]; then
		echo /dev/mmcblk0p5
	fi
	return 0
}
if [ -z "${cred_hash}" ]; then
	RDEV=$(reserve_dev) || RDEV=""
	if [ -n "${RDEV}" ]; then
		rec=$(head -c 1024 "${RDEV}" 2>/dev/null | tr -d '\000\377') || rec=""
		r_mac=$(printf '%s\n' "${rec}" | sed -n 's/^mac=//p' | head -1)
		r_hash=$(printf '%s\n' "${rec}" | sed -n 's/^hash=//p' | head -1)
		if [ "$(printf '%s\n' "${rec}" | head -1)" = "CHC5-FACTORY-CRED v1" ] &&
		   printf '%s\n' "${rec}" | grep -qx 'end' &&
		   [ "${r_mac}" = "${MAC}" ]; then
			case "${r_hash}" in
				'$'*'$'*'$'*)
					cred_hash="${r_hash}"
					cred_src="the reserve copy on ${RDEV}"
					umask 077
					printf 'mac=%s\nhash=%s\n' "${MAC}" "${cred_hash}" > "${FACTORY_CRED}.tmp"
					sync
					mv -f "${FACTORY_CRED}.tmp" "${FACTORY_CRED}"
					umask 022
					;;
			esac
		fi
	fi
fi

mkdir -p /etc/chc5
if [ -n "${cred_hash}" ]; then
	printf 'admin:%s:admin\n' "${cred_hash}" > /etc/chc5/webd.passwd
	chmod 600 /etc/chc5/webd.passwd
	echo "CHC5-PROVISION: restored the factory password from ${cred_src}"
else
	PW=$(tr -dc 'A-Za-z0-9' < /dev/urandom | head -c 12)
	HASH=$(mkpasswd -m sha512 "${PW}" 2>/dev/null || true)

	case "${HASH}" in
		'$6$'*) ;;
		*)
			echo "CHC5-PROVISION: FAILED - mkpasswd did not produce a SHA-512 (\$6\$) hash" >&2
			exit 1
			;;
	esac
	printf 'admin:%s:admin\n' "${HASH}" > /etc/chc5/webd.passwd
	chmod 600 /etc/chc5/webd.passwd

	umask 077
	printf 'mac=%s\nhash=%s\n' "${MAC}" "${HASH}" > "${FACTORY_CRED}.tmp"
	chmod 600 "${FACTORY_CRED}.tmp"
	sync
	mv -f "${FACTORY_CRED}.tmp" "${FACTORY_CRED}"
	umask 022

	echo "CHC5-PROVISION serial=${SERIAL} mac=${MAC} admin_pw=${PW}" \
		| tee /run/chc5-provision.record
	printf '%s,%s,%s\n' "${SERIAL}" "${MAC}" "${PW}" >> "${DATA}/provision-log.csv"
fi

touch "${MARK}"
echo "CHC5-PROVISION: complete for ${SERIAL}"
