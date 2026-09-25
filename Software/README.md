# CHC5 camera software

## Folders

| Folder | What it is | Licence |
|---|---|---|
| `linux-xlnx/` | Linux 6.6 kernel (Xilinx tree) with the CHC5 drivers | GPL-2.0 |
| `camcfgd/` | camera configuration daemon (source, without auto exposure and auto white balance) | CC-BY-NC-ND-4.0 |
| `chc5_webd/` | web interface (binary and web page files) | CC-BY-NC-ND-4.0 |
| `chc5_platformd/` | platform daemon (binary) | CC-BY-NC-ND-4.0 |
| `gvcp_server/` | GigE Vision control server (binary) | CC-BY-NC-ND-4.0 |
| `br2-external/` | Buildroot configuration, board files and camera scripts | CC-BY-NC-ND-4.0 |
| `sensors/` | sensor device-tree overlays and manifests | CC-BY-NC-ND-4.0 |
| `scripts/` | build helpers | CC-BY-NC-ND-4.0 |
| `buildroot/`, `buildroot_debug/` | Buildroot 2025.02 | GPL-2.0 (Buildroot) |

Third-party files keep their own licence.

## What you can build

A **debug system** for the camera and its **update** (`.raucb`), signed with the
public dev key in `br2-external/board/chc5/rauc-keys/`. A debug update installs
only on a debug SD card. Production updates are signed by Circuit Valley and
cannot be built from this tree.

## Requirements

- A Linux PC with the packages Buildroot needs
  (https://buildroot.org/downloads/manual/manual.html#requirement)
- An ARM cross compiler for the kernel, `arm-linux-gnueabihf-gcc`
  (Debian/Ubuntu: `gcc-arm-linux-gnueabihf`)
- The factory archives in `firmware_store/`: one bitstream, one sensor and one
  USB firmware archive. They are published in a later release.

## Build

```
make kernel
make rootfs-debug BITSTREAM=<bitstream archive> SENSOR=<sensor archive> USB_FW=<usb firmware archive>
make bundle-debug
```

`BITSTREAM`, `SENSOR` and `USB_FW` are file names in `firmware_store/`; they
become the camera's factory defaults. `NETNAME=<name>` sets the camera's network
name (default `chc5-cam`). `make help` lists the targets.

The update is written to `buildroot_debug/output/images/chc5-debug-<date>.raucb`.
Install it from the camera's web interface.
