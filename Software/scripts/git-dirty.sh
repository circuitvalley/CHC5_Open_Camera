#!/bin/sh
# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# git-dirty.sh - print -dirty when a git tree has local changes
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

set -eu

CHECK=0
if [ "${1:-}" = "--check" ]; then
	CHECK=1
	shift
fi

DIR=${1:?usage: git-dirty.sh [--check] <dir>}

if ! git -C "$DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
	[ "$CHECK" = 1 ] && exit 0
	printf ''
	exit 0
fi

STATUS=$(git -C "$DIR" status --porcelain 2>/dev/null || true)

if [ "$CHECK" = 1 ]; then
	[ -z "$STATUS" ] && exit 0
	echo "$STATUS" | sed 's/^/    /'
	exit 1
fi

[ -n "$STATUS" ] && printf -- '-dirty' || printf ''
exit 0
