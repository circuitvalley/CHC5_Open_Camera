# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# chc5_platformd.mk - Buildroot package: chc5_platformd (prebuilt)
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

CHC5_PLATFORMD_VERSION       = 1.0.0
CHC5_PLATFORMD_SITE          = $(BR2_EXTERNAL_CHC5_PATH)/../chc5_platformd
CHC5_PLATFORMD_SITE_METHOD   = local
CHC5_PLATFORMD_LICENSE       = CC-BY-NC-ND-4.0

CHC5_PLATFORMD_DEPENDENCIES = xz systemd libgpiod zlib openssl

CHC5_PLATFORMD_FACTORY_SRC = $(BR2_EXTERNAL_CHC5_PATH)/../firmware_store
CHC5_PLATFORMD_FAC_BS  = $(call qstrip,$(BR2_PACKAGE_CHC5_PLATFORMD_FACTORY_BITSTREAM))
CHC5_PLATFORMD_FAC_SN  = $(call qstrip,$(BR2_PACKAGE_CHC5_PLATFORMD_FACTORY_SENSOR))
CHC5_PLATFORMD_FAC_USB = $(call qstrip,$(BR2_PACKAGE_CHC5_PLATFORMD_FACTORY_USB_FW))
CHC5_PLATFORMD_FAC_NET = $(call qstrip,$(BR2_PACKAGE_CHC5_PLATFORMD_FACTORY_NETNAME))

CHC5_PLATFORMD_U3V_SRC = $(CHC5_PLATFORMD_SITE)

define CHC5_PLATFORMD_BUILD_CMDS
	@for a in $(CHC5_PLATFORMD_FAC_BS) $(CHC5_PLATFORMD_FAC_SN) $(CHC5_PLATFORMD_FAC_USB); do \
	    test -f "$(CHC5_PLATFORMD_FACTORY_SRC)/$$a" || { \
	        echo "chc5_platformd: factory archive $$a not found in firmware_store/." >&2; \
	        echo "  The factory archives are published in a later release; place them in firmware_store/." >&2; \
	        exit 1; }; \
	done
endef


define CHC5_PLATFORMD_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 755 $(@D)/chc5_platformd \
		$(TARGET_DIR)/usr/bin/chc5_platformd
	$(INSTALL) -D -m 755 $(@D)/chc5-sensor-load.sh \
		$(TARGET_DIR)/usr/sbin/chc5-sensor-load
	mkdir -p $(TARGET_DIR)/var/lib/chc5_platformd/usb_firmware
	mkdir -p $(TARGET_DIR)/var/lib/chc5_platformd/bitstreams
	mkdir -p $(TARGET_DIR)/var/lib/chc5_platformd/sensors
	mkdir -p $(TARGET_DIR)/lib/firmware
	$(CHC5_PLATFORMD_PKGDIR)/gen-factory-json.sh \
		$(CHC5_PLATFORMD_FACTORY_SRC)/$(CHC5_PLATFORMD_FAC_BS) \
		$(CHC5_PLATFORMD_FACTORY_SRC)/$(CHC5_PLATFORMD_FAC_SN) \
		$(CHC5_PLATFORMD_FACTORY_SRC)/$(CHC5_PLATFORMD_FAC_USB) \
		"$(CHC5_PLATFORMD_FAC_NET)" \
		$(TARGET_DIR)/usr/share/chc5/factory/factory.json
	$(INSTALL) -D -m 444 \
		$(CHC5_PLATFORMD_FACTORY_SRC)/$(CHC5_PLATFORMD_FAC_BS) \
		$(TARGET_DIR)/usr/share/chc5/factory/bitstream.tar.xz
	$(INSTALL) -D -m 444 \
		$(CHC5_PLATFORMD_FACTORY_SRC)/$(CHC5_PLATFORMD_FAC_SN) \
		$(TARGET_DIR)/usr/share/chc5/factory/sensor.tar.xz
	rm -rf $(@D)/.factory_usb && mkdir -p $(@D)/.factory_usb
	tar -xJf $(CHC5_PLATFORMD_FACTORY_SRC)/$(CHC5_PLATFORMD_FAC_USB) \
		-C $(@D)/.factory_usb
	xz -c -9 $(@D)/.factory_usb/fw.img > $(@D)/.factory_usb/factory.xz
	$(INSTALL) -D -m 444 $(@D)/.factory_usb/factory.xz \
		$(TARGET_DIR)/usr/share/chc5/factory/usb_fw/factory.xz
	$(INSTALL) -D -m 444 $(@D)/.factory_usb/factory.manifest.json \
		$(TARGET_DIR)/usr/share/chc5/factory/usb_fw/factory.manifest.json
	$(INSTALL) -D -m 644 $(CHC5_PLATFORMD_U3V_SRC)/u3v_camera.xml.template \
		$(TARGET_DIR)/etc/cam/u3v_camera.xml.template
	$(INSTALL) -D -m 644 $(CHC5_PLATFORMD_U3V_SRC)/u3v_camera.xml \
		$(TARGET_DIR)/etc/cam/u3v_camera.xml
endef

define CHC5_PLATFORMD_INSTALL_INIT_SYSTEMD
	$(INSTALL) -D -m 644 $(CHC5_PLATFORMD_PKGDIR)/chc5_platformd.service \
		$(TARGET_DIR)/usr/lib/systemd/system/chc5_platformd.service
	$(INSTALL) -D -m 644 $(CHC5_PLATFORMD_PKGDIR)/chc5-sensor-load.service \
		$(TARGET_DIR)/usr/lib/systemd/system/chc5-sensor-load.service
	$(INSTALL) -D -m 644 $(CHC5_PLATFORMD_PKGDIR)/chc5-netgen.service \
		$(TARGET_DIR)/usr/lib/systemd/system/chc5-netgen.service
	$(INSTALL) -D -m 644 $(CHC5_PLATFORMD_PKGDIR)/chc5-factory-check.service \
		$(TARGET_DIR)/usr/lib/systemd/system/chc5-factory-check.service
	mkdir -p $(TARGET_DIR)/etc/systemd/system/basic.target.wants
	ln -sf ../../../../usr/lib/systemd/system/chc5_platformd.service \
		$(TARGET_DIR)/etc/systemd/system/basic.target.wants/chc5_platformd.service
	ln -sf ../../../../usr/lib/systemd/system/chc5-sensor-load.service \
		$(TARGET_DIR)/etc/systemd/system/basic.target.wants/chc5-sensor-load.service
	ln -sf ../../../../usr/lib/systemd/system/chc5-factory-check.service \
		$(TARGET_DIR)/etc/systemd/system/basic.target.wants/chc5-factory-check.service
	mkdir -p $(TARGET_DIR)/etc/systemd/system/network-pre.target.wants
	ln -sf ../../../../usr/lib/systemd/system/chc5-netgen.service \
		$(TARGET_DIR)/etc/systemd/system/network-pre.target.wants/chc5-netgen.service
endef

$(eval $(generic-package))
