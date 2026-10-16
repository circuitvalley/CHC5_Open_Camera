# USB firmware archive manual

The camera's USB 3 controller is a Cypress/Infineon FX3 (CYUSB3014). It boots from its own 256 KB
I2C EEPROM.

A **USB firmware archive** carries one FX3 firmware image. The camera keeps these archives in its
**USB Firmware Library** and programs one into that EEPROM.

There are two firmwares:

| Firmware | Protocol | Source |
|---|---|---|
| UVC | USB Video Class: works with any webcam application | `USB_Firmware/fx3_firmware_source` (this repository); ready-made archive `Software/firmware_store/USB_UVC_V1_12_1.tar.xz` |
| U3V | USB3 Vision: for GenICam applications | released as a ready-made archive only; not included yet |

## Contents

1. [Making an archive](#1-making-an-archive)
2. [What is in an archive](#2-what-is-in-an-archive)
3. [The manifest](#3-the-manifest)
4. [Limits at a glance](#4-limits-at-a-glance)
5. [Upload and program](#5-upload-and-program)

## 1. Making an archive

**1. Build the firmware** (UVC):

```sh
cd USB_Firmware/fx3_firmware_source
make FX3_INSTALL_PATH=<path>/cyfx3sdk
```

- It needs the FX3 SDK 1.3.5 and its `arm-2013.11` toolchain. `FX3_INSTALL_PATH` defaults to `../cyfx3sdk`; `FX3_LIB_VER` and `ARM_GCC_VER` select other versions.
- The build ends with `Binary : firmware.img (<size> bytes)`.
- Its version is `FW_VERSION_MAJOR` / `_MINOR` / `_PATCH` in `version.h`.
- The linker's `uses 2-byte wchar_t` notes come from the SDK's own libraries and are harmless.

**2. Name it after its version:** `USB_<UVC|U3V>_V<major>_<minor>_<patch>`, for example
`USB_UVC_V1_12_1`. The archive file is that name plus `.tar.xz`.

**3. Stage the two files:**

```sh
NAME=USB_UVC_V1_12_1
rm -rf stage && mkdir stage
cp firmware.img stage/fw.img                     # this exact name: anything else is refused
# write stage/factory.manifest.json (section 3)
```

**4. Check the manifest and pack:**

```sh
jq empty stage/factory.manifest.json                                       # valid JSON
LC_ALL=C grep -c -P '[^\x09\x0a\x0d\x20-\x7e]' stage/factory.manifest.json # must print 0 (ASCII only)
tar -C stage --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner -cf $NAME.tar .
xz -T1 -e -z -f $NAME.tar                                                  # -> $NAME.tar.xz
tar -tJvf $NAME.tar.xz                     # exactly factory.manifest.json and fw.img
```

## 2. What is in an archive

**Exactly two files**, at the top level. Any other file, or a missing one, gets the upload refused.

| File | What it is |
|---|---|
| `fw.img` | the FX3 boot image the build produced; its bytes 0-1 are `C` `Y` and byte 3 is `0xB0`, and it is **1 to 229,376 bytes** |
| `factory.manifest.json` | the manifest (section 3) |

**Why 229,376 bytes:**
- The EEPROM holds 262,144 bytes, and its top 32 KB, from address `0x38000`, is the camera's data partition.
- The camera writes its identity there, plus the GenICam description (U3V) or the video mode table (UVC).
- The image must end before that partition.

## 3. The manifest

```json
{
  "manifest_kind": "usb_fw_v1",
  "manifest_version": 1,
  "manifest": {
    "name": "USB_UVC_V1_12_1",
    "version": "1.12.1",
    "description": "UVC firmware",
    "build_date": "2026-10-06T19:03:10Z",
    "author": "circuitvalley",
    "git_sha": "00000000",
    "git_dirty": 0
  },
  "device": {
    "protocol": "uvc",
    "eeprom_layout": 2
  }
}
```

A U3V manifest is the same apart from its name and version, `"protocol": "u3v"`, and
`"xml_max_bytes": 12288`.

| Field | Required | Rule | What it does |
|---|---|---|---|
| `manifest_kind` | yes | `"usb_fw_v1"` | the archive type |
| `manifest_version` | yes | `1` | |
| `manifest.name` | yes | at most 63 characters: letters, digits, `.` `+` `-` `_`, not starting with `.` or `-` | the library entry's name; use the naming rule (section 1) |
| `manifest.version` | yes | shown up to 15 characters | the `version.h` numbers |
| `manifest.description` | yes | at least 5 characters; the library shows the first 63 | one line in product words |
| `manifest.build_date` | yes | UTC, `YYYY-MM-DDThh:mm:ssZ`; display only | |
| `manifest.author` | no | at most 63 characters; display only | |
| `manifest.git_sha` | yes | short sha of the source; display only | |
| `manifest.git_dirty` | yes | `0` or `1` | |
| `device.protocol` | yes | `"uvc"` or `"u3v"` | which firmware this is; decides what the camera writes into the data partition |
| `device.eeprom_layout` | no | `1` or `2`; absent means `1` | the data-partition layout the firmware reads. **Current firmwares use `2`.** The camera writes the partition in the declared layout, and a firmware handed a layout it cannot read starts without its stored camera description and serial number. |
| `device.xml_max_bytes` | no (U3V only) | `1024` to `65535`; absent means 10240 | the firmware's buffer for the GenICam description; the camera never writes a longer one. The released U3V firmware uses `12288`. |

Older manifests may also carry `device.sensor`, `max_res`, `bits` or `lanes`. These keys are ignored:
the firmware does not depend on the sensor, because the camera stores the sensor-specific data in the
data partition.

## 4. Limits at a glance

| Item | Limit | If broken |
|---|---|---|
| Archive | 1 MB compressed, 1 MB unpacked, exactly 2 files | upload refused |
| `fw.img` | 1 to 229,376 bytes; bytes 0-1 `C` `Y`, byte 3 `0xB0` | upload refused |
| `factory.manifest.json` | at most 16 KB; string values printable 7-bit ASCII; no duplicate keys, comments or trailing commas | upload refused |
| `manifest.name` | as in section 3; must differ from the factory firmware's name | upload refused |
| `manifest.description` | at least 5 characters | upload refused |
| `device.eeprom_layout` | 1 or 2 | upload refused |
| `device.xml_max_bytes` | 1024 to 65535 | upload refused |
| Library | 24 uploaded archives, plus the factory firmware | upload refused |

## 5. Upload and program

1. **USB Firmware Library**: upload the archive, then program it.
   - An archive with the same name as a stored one **replaces** it, and the web interface says what it replaced.
   - Uploading an identical archive changes nothing.
2. **Do not power off or unplug while it programs**: it rewrites the FX3's EEPROM.
3. **When programming finishes, power-cycle the camera before streaming over USB.** Until it restarts, the camera does not see the new firmware's start requests.
4. **After activating a sensor of a different model, program the USB firmware again.** The camera's description stored for the USB controller then follows the new sensor.

**Factory firmware of your own build:** a debug system takes its factory USB firmware from the
`USB_FW=<archive>` argument of `make rootfs-debug` (`Software/README.md`). The archive must be in
`firmware_store/`.

If an upload is refused, the web interface shows the field and the reason.
