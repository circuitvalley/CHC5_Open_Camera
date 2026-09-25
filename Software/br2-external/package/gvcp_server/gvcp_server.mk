# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# gvcp_server.mk - Buildroot package: gvcp_server (prebuilt)
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

GVCP_SERVER_VERSION     = 1.0.0
GVCP_SERVER_SITE        = $(BR2_EXTERNAL_CHC5_PATH)/../gvcp_server
GVCP_SERVER_SITE_METHOD = local
GVCP_SERVER_LICENSE     = CC-BY-NC-ND-4.0

GVCP_SERVER_DEPENDENCIES = systemd

define GVCP_SERVER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 755 $(@D)/gvcp_server \
		$(TARGET_DIR)/usr/bin/gvcp_server
	$(INSTALL) -D -m 644 $(@D)/device.xml \
		$(TARGET_DIR)/etc/cam/device.xml
	$(INSTALL) -D -m 644 $(@D)/device.xml.template \
		$(TARGET_DIR)/etc/cam/device.xml.template
endef

define GVCP_SERVER_INSTALL_INIT_SYSTEMD
	$(INSTALL) -D -m 644 $(GVCP_SERVER_PKGDIR)/gvcp_server.service \
		$(TARGET_DIR)/usr/lib/systemd/system/gvcp_server.service
	mkdir -p $(TARGET_DIR)/etc/systemd/system/multi-user.target.wants
	ln -sf ../../../../usr/lib/systemd/system/gvcp_server.service \
		$(TARGET_DIR)/etc/systemd/system/multi-user.target.wants/gvcp_server.service
endef

$(eval $(generic-package))
