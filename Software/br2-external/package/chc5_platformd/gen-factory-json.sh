#!/bin/sh
# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# gen-factory-json.sh - write the per-model factory baseline manifest
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

set -eu

bs_archive="$1"
sn_archive="$2"
usb_archive="$3"
netname="$4"
out="$5"

name_of() {
	arch="$1"
	if [ ! -f "$arch" ]; then
		echo "gen-factory-json: archive not found: '$arch'" \
		     "(is the FACTORY_* option set in the defconfig?)" >&2
		return 1
	fi
	nm=$(tar -xJOf "$arch" --wildcards '*manifest.json' 2>/dev/null | \
		python3 -c 'import json,sys; print(json.load(sys.stdin)["manifest"]["name"])' \
		2>/dev/null) || true
	if [ -z "$nm" ]; then
		echo "gen-factory-json: could not read manifest.name from '$arch'" >&2
		return 1
	fi
	printf '%s\n' "$nm"
}

flat_or_die() {
	arch="$1"; shift
	for re in "$@"; do
		if ! tar -tJf "$arch" 2>/dev/null | sed 's#^\./##' | grep -qE "^${re}\$"; then
			echo "gen-factory-json: '$arch' has no top-level file matching '${re}'" \
			     "-- factory masters must be flat archives (tar -cJf <name>.tar.xz -C <folder> .)" >&2
			exit 1
		fi
	done
}
flat_or_die "$bs_archive"  'manifest\.json' 'bitstream\.bin'
flat_or_die "$sn_archive"  'manifest\.json' '[^/]+\.ko'
flat_or_die "$usb_archive" 'factory\.manifest\.json' 'fw\.img'

bs_name="$(name_of "$bs_archive")"
sn_name="$(name_of "$sn_archive")"
usb_name="$(name_of "$usb_archive")"

if [ -z "$bs_name" ] || [ -z "$sn_name" ] || [ -z "$usb_name" ]; then
	echo "gen-factory-json: could not read a manifest name " \
	     "(bitstream='$bs_name' sensor='$sn_name' usb_fw='$usb_name')" >&2
	exit 1
fi

mkdir -p "$(dirname "$out")"
rm -f "$out"
cat > "$out" <<EOF
{
  "schema": "chc5-factory-v1",
  "bitstream": "$bs_name",
  "sensor": "$sn_name",
  "usb_fw": "$usb_name",
  "network": { "mode": "dhcp", "user_name": "$netname" },
  "reset_credentials": true
}
EOF
chmod 444 "$out"

echo "gen-factory-json: wrote $out" \
     "(bitstream=$bs_name sensor=$sn_name usb_fw=$usb_name net=$netname)"
