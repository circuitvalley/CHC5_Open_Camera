# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# camcfgd.mk - Buildroot package: camcfgd
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

CAMCFGD_VERSION     = 1.0.0
CAMCFGD_SITE        = $(BR2_EXTERNAL_CHC5_PATH)/../camcfgd
CAMCFGD_SITE_METHOD = local
CAMCFGD_LICENSE     = CC-BY-NC-ND-4.0
CAMCFGD_INSTALL_STAGING = YES

CAMCFGD_DEPENDENCIES = systemd

CAMCFGD_GIT_SHA   = $(shell git -C $(CAMCFGD_SITE) describe --always --abbrev=8 2>/dev/null || echo "unknown")
CAMCFGD_GIT_DIRTY = $(shell $(BR2_EXTERNAL_CHC5_PATH)/../scripts/git-dirty.sh $(CAMCFGD_SITE))
CAMCFGD_GIT_REV   = $(shell git -C $(CAMCFGD_SITE) rev-list --count HEAD 2>/dev/null || echo 0)

define CAMCFGD_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		AR="$(TARGET_AR)" \
		GIT_SHA="$(CAMCFGD_GIT_SHA)" \
		GIT_DIRTY="$(CAMCFGD_GIT_DIRTY)" \
		GIT_REV="$(CAMCFGD_GIT_REV)"
endef

define CAMCFGD_INSTALL_STAGING_CMDS
	$(INSTALL) -D -m 644 $(@D)/camcfgd_client.h \
		$(STAGING_DIR)/usr/include/camcfgd/camcfgd_client.h
	$(INSTALL) -D -m 644 $(@D)/common.h \
		$(STAGING_DIR)/usr/include/camcfgd/common.h
	$(INSTALL) -D -m 644 $(@D)/chc5-v4l2-controls.h \
		$(STAGING_DIR)/usr/include/camcfgd/chc5-v4l2-controls.h
	$(INSTALL) -D -m 644 $(@D)/libcamcfgd_client.a \
		$(STAGING_DIR)/usr/lib/libcamcfgd_client.a
endef

define CAMCFGD_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 755 $(@D)/camcfgd \
		$(TARGET_DIR)/usr/bin/camcfgd
endef

define CAMCFGD_INSTALL_INIT_SYSTEMD
	$(INSTALL) -D -m 644 $(CAMCFGD_PKGDIR)/camcfgd.service \
		$(TARGET_DIR)/usr/lib/systemd/system/camcfgd.service
	mkdir -p $(TARGET_DIR)/etc/systemd/system/multi-user.target.wants
	ln -sf ../../../../usr/lib/systemd/system/camcfgd.service \
		$(TARGET_DIR)/etc/systemd/system/multi-user.target.wants/camcfgd.service
endef

$(eval $(generic-package))
