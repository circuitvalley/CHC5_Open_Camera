# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# Makefile - build a CHC5 debug system and its signed RAUC update
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

.PHONY: help kernel defconfig-debug rauc-keyring-debug rootfs-debug bundle-debug clean-debug

help:
	@echo "CHC5 debug build (SD card, signed with the public dev key)"
	@echo ""
	@echo "  make kernel         build zImage, device tree and modules"
	@echo "  make rootfs-debug BITSTREAM=<file> SENSOR=<file> USB_FW=<file> [NETNAME=<name>]"
	@echo "                      build the debug root filesystem; the three files are the"
	@echo "                      factory archives, taken from firmware_store/"
	@echo "  make bundle-debug   sign the RAUC update from the last rootfs-debug"
	@echo "  make clean-debug    remove the Buildroot output"

kernel:
	$(MAKE) -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) $(KERNEL_DEFCONFIG)
	$(MAKE) -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -j$(JOBS) \
	    zImage $(KERNEL_DTS).dtb modules

defconfig-debug:
	@for v in "BITSTREAM=$(BITSTREAM)" "SENSOR=$(SENSOR)" "USB_FW=$(USB_FW)"; do \
	    n=$${v%%=*}; a=$${v#*=}; \
	    if [ -z "$$a" ] || [ ! -f "$(FIRMWARE_STORE)/$$a" ]; then \
	        echo "defconfig-debug: $$n must name a factory archive in firmware_store/ (got '$$a')" >&2; \
	        echo "  available:" >&2; ls -1 $(FIRMWARE_STORE) 2>/dev/null | sed 's/^/    /' >&2; \
	        exit 1; \
	    fi; \
	done
	@mkdir -p $(BR_DIR)
	@{ for f in $(FRAGS); do cat "$$f"; echo ""; done; \
	   echo 'BR2_PACKAGE_CHC5_PLATFORMD_FACTORY_BITSTREAM="$(BITSTREAM)"'; \
	   echo 'BR2_PACKAGE_CHC5_PLATFORMD_FACTORY_SENSOR="$(SENSOR)"'; \
	   echo 'BR2_PACKAGE_CHC5_PLATFORMD_FACTORY_USB_FW="$(USB_FW)"'; \
	   echo 'BR2_PACKAGE_CHC5_PLATFORMD_FACTORY_NETNAME="$(NETNAME)"'; } > $(GEN_DEFCONFIG)
	$(BR_MAKE) defconfig BR2_DEFCONFIG=$(GEN_DEFCONFIG)

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
