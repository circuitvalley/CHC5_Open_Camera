# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# medium-qspi.frag - QSPI NOR medium (update bundles only)
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

BR2_ROOTFS_OVERLAY="$(BR2_EXTERNAL_CHC5_PATH)/board/chc5/rootfs-overlay $(BR2_EXTERNAL_CHC5_PATH)/board/chc5/rootfs-overlay-qspi"
BR2_ROOTFS_POST_IMAGE_SCRIPT="$(BR2_EXTERNAL_CHC5_PATH)/board/chc5/post-image-qspi.sh"
BR2_PACKAGE_MTD=y
BR2_PACKAGE_HOST_UBOOT_TOOLS=y
BR2_PACKAGE_HOST_UBOOT_TOOLS_FIT_SUPPORT=y
