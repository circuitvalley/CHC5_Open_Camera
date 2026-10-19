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

Two **updates** (`.raucb`), both signed with the public dev key in
`br2-external/board/chc5/rauc-keys/`:

| Update | Installs on | Make targets |
|---|---|---|
| `chc5-debug-<date>.raucb` | a debug SD card | `rootfs-debug`, `bundle-debug` |
| `chc5-qspi-dev-<date>.raucb` | a QSPI camera running a dev image | `rootfs-qspi`, `qspi-bundle-dev` |

- The debug system has extra tools (debugger, network capture, video tools) and only
  fits the larger slots of a debug SD card.
- A dev image accepts official updates and updates signed with the dev key. A
  production image accepts official updates only, so it refuses both of these.
- A system built from this tree runs the bitstreams in this repository (see Archives).
  Bitstreams that come only with the official camera releases need the official update.
- This tree builds updates only, no SD card image and no full QSPI flash image.
  Production updates are signed by Circuit Valley and cannot be built from this tree.

## Requirements

- A Linux PC with the packages Buildroot needs
  (https://buildroot.org/downloads/manual/manual.html#requirement)
- An ARM cross compiler for the kernel, `arm-linux-gnueabihf-gcc`
  (Debian/Ubuntu: `gcc-arm-linux-gnueabihf`)
- The factory archives in `firmware_store/`: one bitstream, one sensor and one
  USB firmware archive. Ready-made ones are included (see Archives).

## Archives

The camera loads three kinds of archive. Each manual says how to make one and what goes in its
manifest:

| Archive | Manual |
|---|---|
| Sensor | `sensors/SENSOR_ARCHIVE.md` |
| Bitstream | `../Vivado_FPGA_Project/RGB/BITSTREAM_ARCHIVE.md` (colour), `../Vivado_FPGA_Project/Mono/BITSTREAM_ARCHIVE.md` (mono) |
| USB firmware | `../USB_Firmware/USB_FW_ARCHIVE.md` |

**Ready-made archives** in `firmware_store/`, each built from this repository by the steps in
its manual:

| File | What it is | Version |
|---|---|---|
| `chc5_xilinx_fw.tar.xz` | colour bitstream (`../Vivado_FPGA_Project/RGB`) | 1.5.0 |
| `chc5_xilinx_mono_fw.tar.xz` | mono bitstream (`../Vivado_FPGA_Project/Mono`) | 1.4.0 |
| `IMX565_4Lane_12bit_XLNXC.tar.xz` | IMX565 sensor, for the colour bitstream | 1.14.4 |
| `USB_UVC_V1_12_1.tar.xz` | UVC USB firmware | 1.12.1 |

- Upload them in the camera's web interface, or use them as factory archives (next section).
- The sensor archive's modules are built for the kernel of the official camera release. They
  also load on a kernel built from this repository: the symbol versions are the same.
- The mono bitstream needs a mono (`_XLNXM`) sensor archive. None is included yet;
  `sensors/SENSOR_ARCHIVE.md` says how to make one.

## Build

Debug SD update:

```
make kernel
make rootfs-debug BITSTREAM=<bitstream archive> SENSOR=<sensor archive> USB_FW=<usb firmware archive>
make bundle-debug
```

QSPI dev update:

```
make kernel
make rootfs-qspi BITSTREAM=<bitstream archive> SENSOR=<sensor archive> USB_FW=<usb firmware archive>
make qspi-bundle-dev
```

`BITSTREAM`, `SENSOR` and `USB_FW` are file names in `firmware_store/`; they
become the camera's factory defaults. `NETNAME=<name>` sets the camera's network
name (default `chc5-cam`). `make help` lists the targets.

The two systems build in separate Buildroot trees (`buildroot_debug/` and
`buildroot/`), so both can be kept. Build them one after the other, not at the
same time: both write the update keyring in `br2-external/` before they build.

A colour system from the ready-made archives:

```
make rootfs-debug BITSTREAM=chc5_xilinx_fw.tar.xz SENSOR=IMX565_4Lane_12bit_XLNXC.tar.xz USB_FW=USB_UVC_V1_12_1.tar.xz
make rootfs-qspi  BITSTREAM=chc5_xilinx_fw.tar.xz SENSOR=IMX565_4Lane_12bit_XLNXC.tar.xz USB_FW=USB_UVC_V1_12_1.tar.xz
```

The updates are written to:

| Update | File |
|---|---|
| debug SD | `buildroot_debug/output/images/chc5-debug-<date>.raucb` |
| QSPI dev | `buildroot/output/images/chc5-qspi-dev-<date>.raucb` |

Install an update from the camera's web interface. Each update checks that it
matches the camera's medium (SD or QSPI) and refuses to install otherwise.
`qspi-bundle-dev` refuses to sign a root filesystem or kernel that does not fit the
camera's QSPI slots.
