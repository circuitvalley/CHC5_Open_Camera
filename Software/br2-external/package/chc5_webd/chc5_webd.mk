# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# chc5_webd.mk - Buildroot package: chc5_webd
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

CIVETWEB_CONF_OPTS += WITH_WEBSOCKET=1

CIVETWEB_INSTALL_OPTS += LIBS="$(CIVETWEB_LIBS)"

CHC5_WEBD_VERSION     = 2.2.0
CHC5_WEBD_SITE        = $(BR2_EXTERNAL_CHC5_PATH)/../chc5_webd
CHC5_WEBD_SITE_METHOD = local
CHC5_WEBD_LICENSE     = CC-BY-NC-ND-4.0

CHC5_WEBD_DEPENDENCIES = civetweb systemd libxcrypt



define CHC5_WEBD_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 755 $(@D)/chc5_webd \
		$(TARGET_DIR)/usr/bin/chc5_webd
	$(INSTALL) -d -m 755 $(TARGET_DIR)/usr/share/chc5_webd/static
	$(INSTALL) -m 644 $(@D)/index.html \
		$(TARGET_DIR)/usr/share/chc5_webd/static/index.html
	$(INSTALL) -m 644 $(@D)/app.js \
		$(TARGET_DIR)/usr/share/chc5_webd/static/app.js
	$(INSTALL) -m 644 $(@D)/style.css \
		$(TARGET_DIR)/usr/share/chc5_webd/static/style.css
	$(INSTALL) -m 644 $(@D)/circuitvalley.svg \
		$(TARGET_DIR)/usr/share/chc5_webd/static/circuitvalley.svg
	$(INSTALL) -m 644 $(@D)/circuitvalley_favicon.svg \
		$(TARGET_DIR)/usr/share/chc5_webd/static/circuitvalley_favicon.svg
endef

define CHC5_WEBD_INSTALL_INIT_SYSTEMD
	$(INSTALL) -D -m 644 $(CHC5_WEBD_PKGDIR)/chc5_webd.service \
		$(TARGET_DIR)/usr/lib/systemd/system/chc5_webd.service
	mkdir -p $(TARGET_DIR)/etc/systemd/system/multi-user.target.wants
	ln -sf ../../../../usr/lib/systemd/system/chc5_webd.service \
		$(TARGET_DIR)/etc/systemd/system/multi-user.target.wants/chc5_webd.service
endef

$(eval $(generic-package))
