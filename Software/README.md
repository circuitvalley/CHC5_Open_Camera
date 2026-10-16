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

```
make kernel
make rootfs-debug BITSTREAM=<bitstream archive> SENSOR=<sensor archive> USB_FW=<usb firmware archive>
make bundle-debug
```

`BITSTREAM`, `SENSOR` and `USB_FW` are file names in `firmware_store/`; they
become the camera's factory defaults. `NETNAME=<name>` sets the camera's network
name (default `chc5-cam`). `make help` lists the targets.

A colour system from the ready-made archives:

```
make rootfs-debug BITSTREAM=chc5_xilinx_fw.tar.xz SENSOR=IMX565_4Lane_12bit_XLNXC.tar.xz USB_FW=USB_UVC_V1_12_1.tar.xz
```

The update is written to `buildroot_debug/output/images/chc5-debug-<date>.raucb`.
Install it from the camera's web interface.
