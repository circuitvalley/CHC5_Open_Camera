# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# external.mk - include the CHC5 packages
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

include $(sort $(wildcard $(BR2_EXTERNAL_CHC5_PATH)/package/*/*.mk))
