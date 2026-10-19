# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# Makefile - build the CHC5 debug and QSPI dev systems and their signed RAUC updates
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

TOP            := $(CURDIR)
BR_DIR         := $(TOP)/buildroot_debug
BR_EXT         := $(TOP)/br2-external
KDIR           := $(TOP)/linux-xlnx

ARCH           := arm
CROSS_COMPILE  ?= arm-linux-gnueabihf-
KERNEL_DEFCONFIG ?= xilinx_chc5_defconfig
KERNEL_DTS     ?= xilinx/zynq-chc5
JOBS           := $(shell nproc)

FRAG_DIR       := $(BR_EXT)/configs/fragments
FRAGS          := $(FRAG_DIR)/base.frag $(FRAG_DIR)/medium-sd.frag $(FRAG_DIR)/flavour-debug.frag
FIRMWARE_STORE := $(TOP)/firmware_store
BITSTREAM      ?=
SENSOR         ?=
USB_FW         ?=
NETNAME        ?= chc5-cam
GEN_DEFCONFIG  := $(BR_DIR)/.chc5-defconfig

BR_MAKE        := $(MAKE) -C $(BR_DIR) BR2_EXTERNAL=$(BR_EXT)
BR_ENV         := CHC5_KDIR=$(KDIR)

IMAGES         := $(BR_DIR)/output/images
TARGET_DIR     := $(BR_DIR)/output/target
HOST_RAUC      := $(BR_DIR)/output/host/bin/rauc

RAUC_KEYDIR    := $(BR_EXT)/board/chc5/rauc-keys
RAUC_KEY       := $(RAUC_KEYDIR)/dev-key.pem
RAUC_CERT      := $(RAUC_KEYDIR)/dev-cert.pem
RAUC_KEYRING   := $(BR_EXT)/board/chc5/rootfs-overlay/etc/rauc/keyring.pem
RAUC_FLAVOR    := $(BR_EXT)/board/chc5/rootfs-overlay/etc/chc5-flavor
RAUC_HOOK      := $(BR_EXT)/board/chc5/rauc-hook.sh
GEN_MANIFEST   := $(TOP)/scripts/gen-bundle-manifest.sh
RAUC_COMPATIBLE := chc5
BUNDLE_VERSION ?= $(shell date -u +%Y.%m.%d-%H%M)
BUNDLE_OUT     := $(IMAGES)/chc5-debug-$(BUNDLE_VERSION).raucb

RAUC_PROD_CERT := $(RAUC_KEYDIR)/prod-cert.pem
BR_DIR_QSPI    := $(TOP)/buildroot
FRAGS_QSPI     := $(FRAG_DIR)/base.frag $(FRAG_DIR)/medium-qspi.frag
GEN_DEFCONFIG_QSPI := $(BR_DIR_QSPI)/.chc5-defconfig
BR_MAKE_QSPI   := $(MAKE) -C $(BR_DIR_QSPI) BR2_EXTERNAL=$(BR_EXT)
IMAGES_QSPI    := $(BR_DIR_QSPI)/output/images
TARGET_DIR_QSPI := $(BR_DIR_QSPI)/output/target
HOST_RAUC_QSPI := $(BR_DIR_QSPI)/output/host/bin/rauc
QSPI_BUNDLE_OUT := $(IMAGES_QSPI)/chc5-qspi-dev-$(BUNDLE_VERSION).raucb
QSPI_ROOTFS_MAX := 31457280
QSPI_KERNEL_MAX := 8388608

.PHONY: help kernel defconfig-debug rauc-keyring-debug rootfs-debug bundle-debug clean-debug \
        defconfig-qspi rauc-keyring-dev rootfs-qspi qspi-bundle-dev clean-qspi

help:
	@echo "CHC5 debug build (SD card, signed with the public dev key)"
	@echo ""
	@echo "  make kernel         build zImage, device tree and modules"
	@echo "  make rootfs-debug BITSTREAM=<file> SENSOR=<file> USB_FW=<file> [NETNAME=<name>]"
	@echo "                      build the debug root filesystem; the three files are the"
	@echo "                      factory archives, taken from firmware_store/"
	@echo "  make bundle-debug   sign the RAUC update from the last rootfs-debug"
	@echo "  make clean-debug    remove the Buildroot output"
	@echo ""
	@echo "CHC5 QSPI dev update (for QSPI cameras running a dev image)"
	@echo ""
	@echo "  make rootfs-qspi BITSTREAM=<file> SENSOR=<file> USB_FW=<file> [NETNAME=<name>]"
	@echo "                      build the QSPI root filesystem, dev flavour"
	@echo "  make qspi-bundle-dev sign the QSPI RAUC update from the last rootfs-qspi"
	@echo "  make clean-qspi     remove the QSPI Buildroot output"

kernel:
	$(MAKE) -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) $(KERNEL_DEFCONFIG)
	$(MAKE) -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -j$(JOBS) \
	    zImage $(KERNEL_DTS).dtb modules

define gen_defconfig
	@for v in "BITSTREAM=$(BITSTREAM)" "SENSOR=$(SENSOR)" "USB_FW=$(USB_FW)"; do \
	    n=$${v%%=*}; a=$${v#*=}; \
	    if [ -z "$$a" ] || [ ! -f "$(FIRMWARE_STORE)/$$a" ]; then \
	        echo "$(1): $$n must name a factory archive in firmware_store/ (got '$$a')" >&2; \
	        echo "  available:" >&2; ls -1 $(FIRMWARE_STORE) 2>/dev/null | sed 's/^/    /' >&2; \
	        exit 1; \
	    fi; \
	done
	@mkdir -p $(3)
	@{ for f in $(2); do cat "$$f"; echo ""; done; \
	   echo 'BR2_PACKAGE_CHC5_PLATFORMD_FACTORY_BITSTREAM="$(BITSTREAM)"'; \
	   echo 'BR2_PACKAGE_CHC5_PLATFORMD_FACTORY_SENSOR="$(SENSOR)"'; \
	   echo 'BR2_PACKAGE_CHC5_PLATFORMD_FACTORY_USB_FW="$(USB_FW)"'; \
	   echo 'BR2_PACKAGE_CHC5_PLATFORMD_FACTORY_NETNAME="$(NETNAME)"'; } > $(4)
	$(MAKE) -C $(3) BR2_EXTERNAL=$(BR_EXT) defconfig BR2_DEFCONFIG=$(4)
endef

defconfig-debug:
	$(call gen_defconfig,defconfig-debug,$(FRAGS),$(BR_DIR),$(GEN_DEFCONFIG))

defconfig-qspi:
	$(call gen_defconfig,defconfig-qspi,$(FRAGS_QSPI),$(BR_DIR_QSPI),$(GEN_DEFCONFIG_QSPI))

rauc-keyring-debug:
	@mkdir -p $(dir $(RAUC_KEYRING))
	@cp -f $(RAUC_CERT) $(RAUC_KEYRING)
	@echo debug > $(RAUC_FLAVOR)
	@echo "rauc-keyring-debug: keyring = dev certificate only"

rootfs-debug: kernel defconfig-debug rauc-keyring-debug
	$(BR_ENV) $(BR_MAKE) chc5_platformd-reinstall
	$(BR_ENV) $(BR_MAKE) -j$(JOBS)

$(HOST_RAUC):
	$(BR_MAKE) host-rauc

bundle-debug: $(HOST_RAUC)
	@test -f "$(IMAGES)/rootfs.squashfs" || { echo "bundle-debug: no rootfs.squashfs, run 'make rootfs-debug' first" >&2; exit 1; }
	@test -f "$(IMAGES)/kernel.itb" || { echo "bundle-debug: no kernel.itb, run 'make rootfs-debug' first" >&2; exit 1; }
	@test "$$(cat $(IMAGES)/.medium 2>/dev/null)" = "sd-debug" || { echo "bundle-debug: $(IMAGES) does not hold a finished debug build, run 'make rootfs-debug'" >&2; exit 1; }
	@rm -rf "$(IMAGES)/bundle" && mkdir -p "$(IMAGES)/bundle"
	@cp -f "$(IMAGES)/rootfs.squashfs" "$(IMAGES)/bundle/"
	@cp -f "$(IMAGES)/kernel.itb" "$(IMAGES)/bundle/kernel.img"
	@sed 's/@MEDIUM@/sd/' "$(RAUC_HOOK)" > "$(IMAGES)/bundle/hook.sh"
	@chmod +x "$(IMAGES)/bundle/hook.sh"
	@"$(GEN_MANIFEST)" "$(TOP)" "$(TARGET_DIR)" "$(RAUC_COMPATIBLE)" \
	    rootfs.squashfs kernel.img "$(IMAGES)/bundle/manifest.raucm" sd
	@rm -f "$(BUNDLE_OUT)"
	$(HOST_RAUC) bundle --cert="$(RAUC_CERT)" --key="$(RAUC_KEY)" "$(IMAGES)/bundle" "$(BUNDLE_OUT)"
	@rm -rf "$(IMAGES)/bundle"
	@"$(HOST_RAUC)" info --keyring="$(TARGET_DIR)/etc/rauc/keyring.pem" "$(BUNDLE_OUT)" >/dev/null 2>&1 || { \
	    echo "bundle-debug: the bundle is not trusted by the keyring in its own rootfs" >&2; rm -f "$(BUNDLE_OUT)"; exit 1; }
	@echo "bundle-debug: signed update -> $(BUNDLE_OUT)"

clean-debug:
	$(BR_MAKE) clean

rauc-keyring-dev:
	@mkdir -p $(dir $(RAUC_KEYRING))
	@{ cat $(RAUC_PROD_CERT); echo ""; cat $(RAUC_CERT); } > $(RAUC_KEYRING)
	@echo dev > $(RAUC_FLAVOR)
	@echo "rauc-keyring-dev: keyring = production certificate + dev certificate"

rootfs-qspi: kernel defconfig-qspi rauc-keyring-dev
	$(BR_ENV) $(BR_MAKE_QSPI) chc5_platformd-reinstall
	$(BR_ENV) $(BR_MAKE_QSPI) -j$(JOBS)

$(HOST_RAUC_QSPI):
	$(BR_MAKE_QSPI) host-rauc

qspi-bundle-dev: $(HOST_RAUC_QSPI)
	@test -f "$(IMAGES_QSPI)/rootfs.squashfs" || { echo "qspi-bundle-dev: no rootfs.squashfs, run 'make rootfs-qspi' first" >&2; exit 1; }
	@test -f "$(IMAGES_QSPI)/kernel.itb" || { echo "qspi-bundle-dev: no kernel.itb, run 'make rootfs-qspi' first" >&2; exit 1; }
	@test "$$(cat $(IMAGES_QSPI)/.medium 2>/dev/null)" = "qspi" || { echo "qspi-bundle-dev: $(IMAGES_QSPI) does not hold a finished QSPI build, run 'make rootfs-qspi'" >&2; exit 1; }
	@test "$$(cat $(TARGET_DIR_QSPI)/etc/chc5-flavor 2>/dev/null)" = "dev" || { echo "qspi-bundle-dev: the QSPI rootfs is not the dev flavour, run 'make rootfs-qspi'" >&2; exit 1; }
	@s=$$(wc -c < "$(IMAGES_QSPI)/rootfs.squashfs"); test $$s -le $(QSPI_ROOTFS_MAX) || { echo "qspi-bundle-dev: rootfs.squashfs is $$s bytes, the QSPI rootfs slot holds $(QSPI_ROOTFS_MAX)" >&2; exit 1; }
	@s=$$(wc -c < "$(IMAGES_QSPI)/kernel.itb"); test $$s -le $(QSPI_KERNEL_MAX) || { echo "qspi-bundle-dev: kernel.itb is $$s bytes, the QSPI kernel slot holds $(QSPI_KERNEL_MAX)" >&2; exit 1; }
	@rm -rf "$(IMAGES_QSPI)/bundle-qspi" && mkdir -p "$(IMAGES_QSPI)/bundle-qspi"
	@cp -f "$(IMAGES_QSPI)/rootfs.squashfs" "$(IMAGES_QSPI)/bundle-qspi/rootfs.img"
	@cp -f "$(IMAGES_QSPI)/kernel.itb" "$(IMAGES_QSPI)/bundle-qspi/kernel.img"
	@sed 's/@MEDIUM@/qspi/' "$(RAUC_HOOK)" > "$(IMAGES_QSPI)/bundle-qspi/hook.sh"
	@chmod +x "$(IMAGES_QSPI)/bundle-qspi/hook.sh"
	@"$(GEN_MANIFEST)" "$(TOP)" "$(TARGET_DIR_QSPI)" "$(RAUC_COMPATIBLE)" \
	    rootfs.img kernel.img "$(IMAGES_QSPI)/bundle-qspi/manifest.raucm" qspi
	@rm -f "$(QSPI_BUNDLE_OUT)"
	$(HOST_RAUC_QSPI) bundle --cert="$(RAUC_CERT)" --key="$(RAUC_KEY)" "$(IMAGES_QSPI)/bundle-qspi" "$(QSPI_BUNDLE_OUT)"
	@rm -rf "$(IMAGES_QSPI)/bundle-qspi"
	@"$(HOST_RAUC_QSPI)" info --keyring="$(TARGET_DIR_QSPI)/etc/rauc/keyring.pem" "$(QSPI_BUNDLE_OUT)" >/dev/null 2>&1 || { \
	    echo "qspi-bundle-dev: the bundle is not trusted by the keyring in its own rootfs" >&2; rm -f "$(QSPI_BUNDLE_OUT)"; exit 1; }
	@echo "qspi-bundle-dev: signed update -> $(QSPI_BUNDLE_OUT)"

clean-qspi:
	$(BR_MAKE_QSPI) clean
