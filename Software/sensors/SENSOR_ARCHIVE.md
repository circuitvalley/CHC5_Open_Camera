# Sensor archive manual

A sensor archive tells the camera which image sensor is fitted. It holds:
- the sensor's Linux driver module;
- a device-tree overlay that describes the sensor and the FPGA pipeline it runs on;
- a manifest that describes the sensor to the camera software.

Upload it in the web interface's **Sensor Library**, activate it and reboot.

This folder holds the sources of the archives for the two Xilinx-IP pipelines:

```
<SENSOR>/<VARIANT>.dts      the overlay leaf (one per archive)
<SENSOR>/<VARIANT>.json     the manifest, authored fields only
<SENSOR>/sensor.dtsi        the sensor's I2C node, shared by its variants
<SENSOR>/supplies.dtsi      its clock and power rails
fabric/colour_xilinx.dtsi   the colour pipeline's FPGA blocks
fabric/mono_xilinx.dtsi     the mono pipeline's FPGA blocks
common/cam_power.dtsi       sensor power and reset lines
```

## Contents

1. [Names and pairing](#1-names-and-pairing)
2. [What is in an archive](#2-what-is-in-an-archive)
3. [The overlay](#3-the-overlay)
4. [The manifest](#4-the-manifest)
5. [Packing](#5-packing)
6. [Upload and activate](#6-upload-and-activate)
7. [Limits at a glance](#7-limits-at-a-glance)

## 1. Names and pairing

An archive is named `<SENSOR>_<lanes>Lane_<bits>bit[_mono]_<FAMILY>`, for example
`IMX565_4Lane_12bit_XLNXC`. The file is that name plus `.tar.xz`, and `manifest.name` is the same name.

| Family | Pipeline | Runs with the bitstream |
|---|---|---|
| `_XLNXC` | Xilinx-IP colour | `chc5_xilinx_fw` (`Vivado_FPGA_Project/RGB`) |
| `_XLNXM` | Xilinx-IP mono | `chc5_xilinx_mono_fw` (`Vivado_FPGA_Project/Mono`) |

**Use a sensor archive only with the bitstream of its family.** Its overlay declares the pipeline
blocks at fixed addresses. On another bitstream, applying it can crash the kernel.

## 2. What is in an archive

A flat `.tar.xz`:

| File | What it is |
|---|---|
| `manifest.json` | the manifest (section 4) |
| `overlay.dtbo` | the compiled overlay (section 3) |
| `<module>.ko` | the sensor driver; its name is `kernel.module` |
| `chc5-crop.ko` | the crop driver of the Xilinx-IP pipelines |

**Other files:**
- Also accepted: `gamma.bin`, `ccm.bin` and `default_config.json`. The Xilinx-IP pipelines use none of them.
- At most 4 `.ko` files.

**Modules load only into the kernel they were built from.** The camera checks each module's
version against its running kernel, so build the modules from the same kernel tree and
configuration as the camera's system.
- **Your own debug system:** take the modules that `make kernel` leaves in `Software/linux-xlnx` (see `Software/README.md`):
  - the sensor driver in `drivers/media/i2c/<module>.ko`;
  - the crop driver in `drivers/media/platform/xilinx/chc5-crop.ko`.
- **A camera running an official release** loads the official archives.

## 3. The overlay

**Build it** from this folder:

```sh
cd Software/sensors
cpp -nostdinc -I<SENSOR> -I. -undef -x assembler-with-cpp -o /tmp/<VARIANT>.pre <SENSOR>/<VARIANT>.dts
dtc -@ -q -I dts -O dtb -o overlay.dtbo /tmp/<VARIANT>.pre
```

**The leaf** (`<VARIANT>.dts`) holds a few macros and three fragments. Everything else is included:

```
#define CHC5_CAM_EMIO          /* sensor power and reset come from the processor (cam_power.dtsi) */
#define CHC5_EOF_FSYNC         /* the GigE path's end-of-frame sync block */
#define CHC5_PTP_EVENT_PIN     /* the camera I/O block's PTP event line to the Ethernet PHY */
#define CHC5_BAYER_FORMAT 0x2c /* CSI-2 data type of the pixels: 0x2c = RAW12 */
#define CHC5_DATA_LANES 1 2 3 4
#define CHC5_BAYER_WIDTH 12    /* bits per pixel */
#define CHC5_MONO_SENSOR 1     /* mono variants only: adds mono-mode to the sensor node */

fragment@0 -> &fpga_full : firmware-name = "chc5-active-bitstream.bit.bin"
fragment@1 -> &amba      : #include "supplies.dtsi"
                           #include "../fabric/colour_xilinx.dtsi"   (mono: mono_xilinx.dtsi)
fragment@2 -> &i2c0      : #include "sensor.dtsi"
```

**The macros and includes:**
- Both Xilinx-IP pipelines are built for **4 lanes and 12-bit RAW**: keep `CHC5_DATA_LANES`, `CHC5_BAYER_FORMAT` and `CHC5_BAYER_WIDTH` as shown.
- `supplies.dtsi`:
  - one `fixed-clock` with the module's real input clock;
  - one `regulator-fixed` per power rail, each with `CHC5_CAM_VIN`;
  - it includes `../common/cam_power.dtsi`.
- `sensor.dtsi`, the sensor's I2C node:
  - the driver's `compatible` and the I2C address;
  - the clock, the rails, and the reset line: `CHC5_CAM_RESET_XCLR` for an active-low reset such as Sony's XCLR, `CHC5_CAM_RESET_RESETB` for an active-high one;
  - the endpoint `sensor_out`, pointing at `&csiss_in`, with `data-lanes = <CHC5_DATA_LANES>` and `link-frequencies` in Hz (64-bit).
- `CHC5_MONO_SENSOR` only matters to a driver with a mono mode (for example `imx568`, through the `mono-mode` property).

## 4. The manifest

`<VARIANT>.json` holds the authored fields. Add five build fields when you pack:

| Field | Value |
|---|---|
| `manifest.name` | the archive name, e.g. `IMX565_4Lane_12bit_XLNXC` |
| `manifest.build_date` | UTC time, `YYYY-MM-DDThh:mm:ssZ` |
| `manifest.git_sha` | short sha of the source |
| `manifest.git_dirty` | `0`, or `1` if the source had uncommitted changes |
| `kernel.vermagic` | the module's kernel release without a trailing `-g<sha>` / `-dirty`, e.g. `6.6.40-xilinx` (at most 15 characters) |

```sh
VERMAGIC=$(modinfo -F vermagic <module>.ko | cut -d' ' -f1 | sed -E 's/-g[0-9a-fA-F]{7,}(-dirty)?$//')
jq --arg n "<archive name>" --arg d "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
   --arg s "$(git rev-parse --short=8 HEAD)" --arg v "$VERMAGIC" \
   '.manifest.name=$n | .manifest.build_date=$d | .manifest.git_sha=$s
    | .manifest.git_dirty=0 | .kernel.vermagic=$v' <SENSOR>/<VARIANT>.json > stage/manifest.json
```

### Required keys

The camera refuses an archive without these:

| Key | Range | Used for |
|---|---|---|
| `manifest_kind` = `"sensor_v1"`, `manifest_version` = `1` | | archive type |
| `manifest.version`, `manifest.description` | strings; description at least 5 characters, 63 shown | library, web interface |
| `kernel.module` | module name without `.ko` | loaded at boot; the `.ko` must be in the archive |
| `sensor.width`, `sensor.height` | 1 to 16384 | maximum image size |
| `sensor.width_inc`, `sensor.height_inc` | 1 to 256 | size step |
| `sensor.bit_depth` | 8 to 16 | pixel depth (12 for these pipelines) |
| `exposure.min_us`, `exposure.max_us` | 1 to 30000000 | exposure range, microseconds |
| `gain.min_db_x10`, `gain.max_db_x10` | 0 to 960 | gain range, tenths of a dB |
| `black_level.min`, `black_level.max` | 0 to 65535 | black level range |
| `fps.min_mhz`, `fps.max_mhz` | 1 to 10000000 | frame rate range, milli-Hz (60 fps = 60000) |
| `binning.max_h`, `binning.max_v` | 1 to 16 | binning factors offered (the camera uses 1, 2 and 4) |
| `decimation.max_h`, `decimation.max_v` | 1 to 16 | write `1` |

### Optional keys

| Key | Default | Effect |
|---|---|---|
| `manifest.vendor`, `manifest.model` | generic | DeviceVendorName / DeviceModelName |
| `sensor.lanes` (e.g. `"2,4"`), `sensor.lanes_wired` | unknown | lane counts the sensor supports, and the one the module is wired for (must equal the overlay's `data-lanes`) |
| `sensor.width_min`, `sensor.height_min` | 128 / 64 | smallest image size |
| `sensor.offset_x_inc`, `sensor.offset_y_inc` | the size step | region-of-interest position step |
| `sensor.chroma` | `"color"` | `"mono"` for a mono variant (needs the mono pipeline) |
| `sensor.color_filter`, `bayer_order` | undeclared | the colour filter order the driver delivers (`BayerRG` / `RGGB` ...; mono: `None`) |
| `bayer_flip_moves` | undeclared | which host flips change that order: `hv`, `h`, `v` or `none`; `binning.bayer_order` gives the binned order if it differs |
| `binning.width`, `binning.height` | size / factor | the binned readout's own maximum |
| `binning.width_min`, `height_min`, `width_inc`, `height_inc`, `offset_x_inc`, `offset_y_inc`, `mode` | unbinned values | the binned grid; `mode` is `sum`, `average` or `drop` |
| `orientation.rotate_180`, `flip_h`, `flip_v` | 0 | mounting correction |
| `pixel_formats[]` `{name, pfnc}` | a built-in list | the formats offered; see below |
| `test_patterns[]` `{name}` | a generic list | the test-pattern menu, **in the driver's menu order**, index 0 = off |
| `uvc_modes[]` `{w, h, fps, src}` | from the maximum size | the modes the UVC firmware offers |
| `camio.*` | unknown | the sensor's sync wiring: `has_xvs`, `xvs_in`, `xvs_out`, `has_xhs`, `has_xtrig`, `strobe` (`none` / `fstrobe` / `tout0`), `shutter` (`global` / `rolling`), `role` (`pin` / `i2c`), `trigger` (`xtrig` / `xvs` / `none`). Facts about the part, not settings. |

**Ignored by these pipelines:**
- `gamma.x10`, `ccm`, `ccm_ct`, `color_temp` and `awb.*`: they belong to pipelines with a white-balance and colour-matrix stage.
- `gain.mode`, `gain.ref`, `gain.reg_max`: retired.
- `kernel.lanes`: an old spelling of `sensor.lanes`.

**Pixel formats** the Xilinx-IP pipelines produce:

| Colour (`_XLNXC`) | PFNC | Mono (`_XLNXM`) | PFNC |
|---|---|---|---|
| `Mono8` | 17301505 | `Mono8` | 17301505 |
| `BayerRG8` | 17301513 | `Mono12` | 17825797 |
| `BayerRG12` | 17825809 | `Mono12p` | 17563719 |
| `BayerRG12p` | 17563737 | `YUV422_8` | 34603058 |
| `YUV422_8` | 34603058 | `YCbCr422_8` | 34603067 |
| `YCbCr422_8` | 34603067 | `RGB565p` | 34603061 |
| `RGB565p` | 34603061 | `BGR8` | 35127317 |
| `BGR8` | 35127317 | | |

Name the Bayer formats after the sensor's real order (`BayerRG`, `BayerGR`, `BayerGB`, `BayerBG`),
with that order's PFNC code.

### Rules that are not obvious from the schema

1. **The manifest describes; the driver decides.** Every size and step here must match what the
   driver accepts. The camera adopts the driver's answer, so a mismatch shows up at the host as a
   different image size than the one asked for.
2. **`gain.max_db_x10` is a bound, not a scale.** The camera limits gain to the driver's maximum;
   keep the two equal.
3. **The colour order must be the one the driver reports after the mounting flip.** Otherwise the
   raw Bayer formats come out with red and blue swapped at the host.
4. **Report the colour code of the image after the pipeline's crop.** The crop block only removes
   rows and columns (for example a sensor's optical-black rows); it never changes the colour code.
   A sensor whose readout needs an odd number of rows removed to get back to its normal colour order
   reports the order after that trim.
5. **`description` is one line in product words**, and the camera shows its first 63 characters,
   for example "IMX565 4-lane 12-bit, Xilinx colour pipeline (no AE/AWB, no 3x3 CCM)".

## 5. Packing

```sh
NAME=IMX565_4Lane_12bit_XLNXC
rm -rf stage && mkdir stage
cp overlay.dtbo <module>.ko chc5-crop.ko stage/
# write stage/manifest.json as in section 4
jq empty stage/manifest.json                           # valid JSON
LC_ALL=C grep -c -P '[^\x09\x0a\x0d\x20-\x7e]' stage/manifest.json   # must print 0 (ASCII only)
tar -C stage --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner -cf $NAME.tar .
xz -T1 -e -z -f $NAME.tar                              # -> $NAME.tar.xz
tar -tJvf $NAME.tar.xz
```

## 6. Upload and activate

1. **Sensor Library**: upload, then activate. The camera stages it.
2. **Reboot.** It takes effect at the next boot.
3. **Make sure the matching bitstream is active** (section 1).
4. **After activating a sensor of a different model, program the USB firmware again.** That way the USB controller's stored camera description follows the new sensor (`USB_Firmware/USB_FW_ARCHIVE.md`).

If an upload is refused, the web interface shows the field and the reason.

## 7. Limits at a glance

| Item | Limit |
|---|---|
| Archive | 8 MB compressed, 16 MB unpacked |
| `.ko` files | at most 4, each at most 4 MB |
| `manifest.json` | at most 16 KB; string values printable 7-bit ASCII; no duplicate keys, comments or trailing commas |
| `manifest.name` | at most 63 characters: letters, digits, `.` `+` `-` `_`, not starting with `.` or `-` |
| `manifest.description` | at least 5 characters; 63 shown |
| `kernel.vermagic` | at most 15 characters |
