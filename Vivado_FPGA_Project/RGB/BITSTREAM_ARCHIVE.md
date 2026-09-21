# Bitstream archive manual

The camera loads the FPGA design from a **bitstream archive**: a `.tar.xz` file that you
upload to the camera. `build.tcl` makes it for you. This manual explains what it contains,
what you can set in `manifest.json`, and the limits the camera enforces.

## Contents

1. [Making an archive](#making-an-archive)
2. [What is in an archive](#what-is-in-an-archive)
3. [Editing manifest.json](#editing-manifestjson)
4. [Manifest reference](#manifest-reference)
5. [Feature tokens](#feature-tokens)
6. [Limits at a glance](#limits-at-a-glance)
7. [Doing it by hand](#doing-it-by-hand)

---

## Making an archive

```sh
cd CHC5_XILINX_FW
vivado -mode batch -source build.tcl
```

The result is `CHC5_XILINX_FW/output/<manifest name>.tar.xz`, for example
`output/chc5_xilinx_fw.tar.xz`. Upload it in the camera's web interface, in the bitstream
library. The camera unpacks it, applies every check in this manual and adds it to the library.
An archive whose `manifest.name` is already in the library replaces that entry.

`build.tcl` refuses to write an archive that the camera would reject. See [README.md](README.md#build-everything-buildtcl) for its steps and options.

---

## What is in an archive

A flat archive (no sub-folders) of at most three regular files:

| File | Required | Content |
|---|---|---|
| `bitstream.bin` | yes | The FPGA bitstream in the Zynq FPGA-manager format: the `.bit` without its header, byte-swapped per 32-bit word (what `bootgen -process_bitstream bin` produces). |
| `manifest.json` | yes | Identifies and describes the bitstream. |
| `overlay.dtbo` | no | A device-tree overlay. If present, it must be a valid compiled device tree (the camera checks its header). This project does not use one. |

The camera looks for the Xilinx sync word in the first 256 bytes of `bitstream.bin`, in raw
(`AA 99 55 66`) or byte-swapped (`66 55 99 AA`) order. The Linux FPGA manager loads the
byte-swapped form, which is why `bootgen` is used rather than Vivado's `write_bitstream -bin_file`.

---

## Editing manifest.json

`CHC5_XILINX_FW/manifest.json` is the template `build.tcl` packs:

```json
{
  "manifest_kind": "bitstream_v1",
  "manifest_version": 1,
  "manifest": {
    "name": "chc5_xilinx_fw",
    "version": "1.1.0",
    "description": "Xilinx-IP colour ISP for 4-lane 12-bit MIPI CSI-2 sensors ...",
    "build_date": "2026-09-21",
    "git_sha": "4aa4f029",
    "git_dirty": 1
  },
  "fpga": {
    "vivado_version": "2024.2",
    "features": "crop,blkc,demosaic,gamma,csc,hdmi,camio,ptp,rgb888,xlnx_isp,xlnx_csi2rx",
    "max_res": "5568x3672",
    "bits": 12,
    "lanes": 4
  }
}
```

| You set | `build.tcl` fills in automatically |
|---|---|
| `name`, `version`, `description`, `features`, `max_res`, `bits`, `lanes` | `build_date` (UTC time of the build), `git_sha` and `git_dirty` (from git, if the folder is in a git repository; otherwise `00000000` and `1`), `vivado_version` |

Change `version` for every release. Change `name` only if the camera should keep the new
build as a separate library entry.

---

## Manifest reference

**Rules for the whole file** (`build.tcl` checks the size and the characters; it does not check nesting depth or duplicate keys)

- Valid JSON, **16 KB (16384 bytes) at most**, nested no more than 4 levels deep, no duplicate keys.
- Every string must be **printable ASCII** (characters `0x20`-`0x7E`): no newlines, tabs,
  escape sequences such as `\n`, or accented or other non-ASCII characters.

**Fields**

| Key | Type | Required | Limits | What it does |
|---|---|---|---|---|
| `manifest_kind` | string | yes | exactly `"bitstream_v1"` | Tells the camera this is a bitstream archive (not a sensor or USB-firmware archive). |
| `manifest_version` | number | yes | 1 or more; use `1` | Version of the manifest format. Recorded, not enforced. |
| `manifest.name` | string | yes | 1-63 characters from `A-Z a-z 0-9 . _ + -`; must not start with `.` or `-` | **Identity** of the library entry and the archive's file name. Uploading the same name replaces the entry. Sensor archives and factory settings refer to the bitstream by this name, so renaming breaks those references. |
| `manifest.version` | string | yes | shown up to 15 characters; longer is cut | Your version label, for example `1.0.2`. Display only. |
| `manifest.description` | string | yes | at least 5 characters; **the library shows only the first 63** | One-line summary. Keep the important part in the first 63 characters. |
| `manifest.build_date` | string | yes (auto) | shown up to 23 characters | Build time, for example `2026-09-21T10:00:00Z`. Display only. |
| `manifest.git_sha` | string | yes (auto) | first 8 characters shown | Source revision the bitstream was built from. Display only. |
| `manifest.git_dirty` | number | yes (auto) | `0` or `1` | `1` means the source had uncommitted changes. |
| `fpga.vivado_version` | string | yes (auto) | up to 11 characters | Vivado version used, for example `2024.2`. |
| `fpga.features` | string | no | comma-separated tokens, **95 characters at most** (the camera cuts longer lists; `build.tcl` refuses them) | What the fabric contains. Some tokens change camera behaviour (see [Feature tokens](#feature-tokens)). |
| `fpga.max_res` | string | no | `"WIDTHxHEIGHT"`, up to 15 characters | Largest frame the fabric handles. Activating a sensor whose full frame is larger gives a **warning** (not an error): the frame is clamped. Keep it equal to the IP maximum and the device tree's `xlnx,max-width/height`. |
| `fpga.bits` | number | no | 0-32; 0 or absent = not checked | Sensor bit depth the datapath was built for. Activating a sensor with a deeper bit depth gives a **warning**. |
| `fpga.lanes` | number or string | no | a number (`1`, `2`, `3`, `4` or `8`) or a set as a string (`"2,4"`); absent = not checked | CSI-2 lane counts the receiver can run. If the sensor board's wired lane count is not in the set, activation gives a **warning**. Any other value makes the upload fail. |

A missing required field, a wrong type, a non-ASCII string or an invalid `name` makes the
camera **reject the upload** with a message naming the field. `build.tcl` catches these before
packing, except a wrong JSON type (for example a number where a string is required).

---

## Feature tokens

Tokens in `fpga.features` for this design:

| Token | Meaning | Effect on the camera |
|---|---|---|
| `rgb888` | Pixel packers support XRGB8888 and BGR24 | **Required** for the BGR8 and BGRa8 pixel formats to be offered to the host. Without it they are left out. |
| `crop` | Raw-image crop window after the CSI-2 receiver | Informational |
| `blkc` | Black-level corrector | Informational |
| `demosaic` | Demosaic present (colour fabric) | Informational; marks the fabric as colour |
| `gamma` | Gamma stage | Informational |
| `csc` | Colour-space conversion | Informational |
| `hdmi` | HDMI output | Informational |
| `camio` | Camera I/O: trigger, strobe, frame sync | Informational |
| `ptp` | PTP time base and scheduled frame start | Informational |
| `xlnx_isp` | Colour pipeline built from Xilinx video IP | Informational |
| `xlnx_csi2rx` | Xilinx MIPI CSI-2 RX Subsystem receiver | Informational |

Informational tokens tell people and tools what the fabric contains. Keep the list accurate.

---

## Limits at a glance

| Limit | Value | If exceeded | `build.tcl` checks |
|---|---|---|---|
| Archive file (`.tar.xz`) | 16 MB | upload rejected | yes |
| Unpacked content | 32 MB | upload rejected | yes (via the two below) |
| Files in the archive | 3, regular files only | upload rejected | yes |
| `bitstream.bin` | 8 MB (this design: 4,045,568 bytes) | upload rejected | yes |
| `bitstream.bin` sync word | within the first 256 bytes | upload rejected | yes |
| `manifest.json` | 16384 bytes, printable ASCII | upload rejected | yes |
| `manifest.name` | 63 characters, `A-Z a-z 0-9 . _ + -` | upload rejected | yes |
| `manifest.description` | at least 5 characters | upload rejected | yes |
| `manifest.description` display | 63 characters | cut when shown | prints a note |
| `fpga.features` | 95 characters | cut, warning logged | refuses |
| `fpga.lanes` | 1, 2, 3, 4, 8 or a set of them | upload rejected | yes |
| Sensor larger than `fpga.max_res` / deeper than `fpga.bits` / lanes not in `fpga.lanes` | - | warning at sensor activation | - |

---

## Doing it by hand

`build.tcl` is the supported way. For troubleshooting, these are the equivalent commands,
run from `CHC5_XILINX_FW/` after a successful build:

<details>
<summary>Manual commands</summary>

```sh
IMPL=CHC5_XILINX_FW/CHC5_XILINX_FW.runs/impl_1
mkdir -p output/work/pkg && cp $IMPL/design_1_wrapper.bit output/work/design.bit
cd output/work
printf 'all:\n{\n  design.bit\n}\n' > bit.bif
bootgen -image bit.bif -arch zynq -process_bitstream bin -w on     # -> design.bit.bin
cp design.bit.bin pkg/bitstream.bin
cp ../../manifest.json pkg/manifest.json     # then set build_date, git_sha, git_dirty, vivado_version
tar -C pkg --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner -cf chc5_xilinx_fw.tar .
xz -T1 -e -z -f chc5_xilinx_fw.tar                                # -> chc5_xilinx_fw.tar.xz
tar -tvJf chc5_xilinx_fw.tar.xz                                   # ./  ./bitstream.bin  ./manifest.json
```

</details>
