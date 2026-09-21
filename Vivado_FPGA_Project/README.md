# CHC5 Xilinx-IP Colour ISP - Vivado 2024.2 project

FPGA design for the CircuitValley **CHC5** camera board (Zynq-7020, `xc7z020clg400-2`).
It receives a 4-lane, 12-bit MIPI CSI-2 image sensor, processes the image with
AMD/Xilinx video IP and delivers it to three outputs: GigE (through DDR), a USB 3
bridge (32-bit parallel bus) and an HDMI monitor.

**One command** takes a fresh checkout to a finished archive ready to upload to the camera:

```sh
cd CHC5_XILINX_FW
vivado -mode batch -source build.tcl
```

| Does | Does not |
|---|---|
| MIPI CSI-2 receive: 4 lanes, RAW12, D-PHY at 1250 Mb/s per lane | Auto-exposure / auto-white-balance statistics |
| Crop window on the raw sensor image, right after the CSI-2 receiver | White-balance gain |
| Black-level correction | |
| Demosaic (Bayer -> RGB) | A 3x3 colour-correction matrix through the stock driver |
| Colour-space conversion RGB -> RGB and RGB -> YUV 4:2:2 | |
| Gamma | |
| Pixel packing: Bayer, RGB, YUV, mono (8 formats) | |
| 1920x1080 @ 60 Hz HDMI preview | |
| Camera I/O (trigger in, strobe out, frame sync) and PTP-timed frame start | |

The archive format and every manifest field are described in
**[BITSTREAM_ARCHIVE.md](BITSTREAM_ARCHIVE.md)**.

---

## Contents

1. [Folder layout](#folder-layout)
2. [Requirements](#requirements)
3. [Installing the board files](#installing-the-board-files)
4. [Build everything: build.tcl](#build-everything-buildtcl)
5. [Working in the Vivado GUI](#working-in-the-vivado-gui)
6. [Expected results](#expected-results)
7. [Design overview](#design-overview)
8. [Clocks](#clocks)
9. [Resets used by the Linux drivers](#resets-used-by-the-linux-drivers)
10. [Changing the design](#changing-the-design)
11. [Starting over](#starting-over)
12. [Troubleshooting](#troubleshooting)
13. [License](#license)

---

## Folder layout

```
./                                repository root
+-- README.md                     this file
+-- BITSTREAM_ARCHIVE.md          archive format and manifest reference
+-- Circuitvalley_CHC5_Board/     Vivado board files (picked up automatically)
+-- CHC5_XILINX_FW/
    +-- build.tcl                 ONE STEP: project + bitstream + camera archive
    +-- rebuild.tcl               recreates the Vivado project, block design included
    +-- manifest.json             manifest for the bitstream archive
    +-- .gitignore                keeps generated files out of git
    +-- src/
    |   +-- hdl/                  Verilog modules used by the block design
    |   +-- constrs/CHC5.xdc      pin and timing constraints
    +-- third_party/
        +-- digilent-vivado-library/   rgb2dvi HDMI IP, unmodified (see License)
```

Generated, disposable and never committed:

- `CHC5_XILINX_FW/CHC5_XILINX_FW/`: the Vivado project, created by `rebuild.tcl`;
- `CHC5_XILINX_FW/output/`: the finished archive, written by `build.tcl`.

---

## Requirements

| What | Details |
|---|---|
| **Vivado 2024.2** | Exactly this version. The scripts pin IP versions (for example `mipi_csi2_rx_subsystem:6.0`, `v_proc_ss:2.3`); another Vivado version fails while creating the block design. |
| **AMD licenses** | License features `mipi_csi2_rx_ctrl` (MIPI CSI-2 RX Subsystem) and `v_proc_ss` (Video Processing Subsystem) must be in a license file Vivado can find, for example through `XILINXD_LICENSE_FILE`. |
| **Linux host** | `build.tcl` packs the archive with GNU `tar` and `xz`, both standard on Linux. `git` is optional: it stamps the source revision into the manifest. |
| **Digilent HDMI IP** | Included: `rgb2dvi` 1.4 and its TMDS interface, copied unmodified from Digilent's `vivado-library` (commit `f4613ff`) into `CHC5_XILINX_FW/third_party/`. Nothing to fetch. |
| **Board files** | Included in `Circuitvalley_CHC5_Board/`. The scripts point Vivado at them; nothing to install. |

A full build takes about 10 minutes with 8 or more parallel jobs.

**Windows:** not tested. `rebuild.tcl` and the GUI flow use only Vivado and relative paths;
`build.tcl`'s packing step needs GNU `tar` and `xz`, which Windows does not have by default.

---

## Installing the board files

You only need this to use the CHC5 board in **your own** Vivado projects. Building this
project does not need it: `rebuild.tcl` finds the board files in this repository by itself.

The board files are the folder `Circuitvalley_CHC5_Board/circuitvalley_chc5`. Installing
them means copying that folder into Vivado's board folder, then restarting Vivado.

### Linux

1. Close Vivado.
2. Copy the folder (replace `/tools/Xilinx` if Vivado is installed somewhere else):

   ```sh
   sudo mkdir -p /tools/Xilinx/Vivado/2024.2/data/xhub/boards/XilinxBoardStore/boards/Circuitvalley
   sudo cp -r Circuitvalley_CHC5_Board/circuitvalley_chc5 \
       /tools/Xilinx/Vivado/2024.2/data/xhub/boards/XilinxBoardStore/boards/Circuitvalley/
   ```

3. Start Vivado.

### Windows

1. Close Vivado.
2. In File Explorer, open (replace `C:\Xilinx` if Vivado is installed somewhere else):

   ```
   C:\Xilinx\Vivado\2024.2\data\xhub\boards\XilinxBoardStore\boards
   ```

3. Create a folder named `Circuitvalley` there.
4. Copy the folder `circuitvalley_chc5` (from `Circuitvalley_CHC5_Board` in this repository)
   into `Circuitvalley`. Windows may ask for administrator permission.
5. Start Vivado.

### Result

The board files end up here (on both systems):

```
<Vivado>/data/xhub/boards/XilinxBoardStore/boards/Circuitvalley/circuitvalley_chc5/1.0/1.0/board.xml
```

In Vivado, **File -> Project -> New -> ... -> Default Part -> Boards**, search for `CHC5`:
**CircuitValley CHC5 Board** is listed. Select it and continue as for any other board.

---

## Build everything: build.tcl

```sh
cd CHC5_XILINX_FW
vivado -mode batch -source build.tcl
```

The script works from any directory (`vivado -mode batch -source <path>/CHC5_XILINX_FW/build.tcl`)
and does, with no manual step:

| # | Step | Stops with an error if |
|---|---|---|
| 1 | Creates the Vivado project from `rebuild.tcl`, or opens it if it exists | the project cannot be created (Vivado version, licenses) |
| 2 | Runs synthesis, implementation and bitstream, only if the bitstream is missing or out of date | a run fails |
| 3 | Converts the `.bit` to `bitstream.bin` with `bootgen` (the Zynq FPGA-manager format) | `bootgen` fails, or the result is not a bitstream the camera accepts |
| 4 | Fills `build_date`, `git_sha`, `git_dirty` and `vivado_version` into a copy of `manifest.json` | - |
| 5 | Validates the manifest against every rule the camera applies | any field breaks a camera rule (the message names the field) |
| 6 | Packs `output/<manifest name>.tar.xz` and checks its contents and size | the archive is not exactly `bitstream.bin` + `manifest.json`, or the camera would not accept it |

Result:

```
CHC5_XILINX_FW/output/chc5_xilinx_fw.tar.xz
```

Upload it in the camera's web interface, in the bitstream library.

Options (after `-tclargs`):

| Option | Effect |
|---|---|
| `--jobs N` | Parallel synthesis/implementation jobs (default: CPU count, at most 8). |
| `--clean` | Rebuild even if the bitstream is up to date. |
| `--help` | Show the options. |

```sh
vivado -mode batch -source build.tcl -tclargs --jobs 16 --clean
```

Run it again after any change. It rebuilds only what is out of date, and repacks.

---

## Working in the Vivado GUI

- **Create or open the project, build, and pack** from the GUI: **Tools -> Run Tcl Script...** ->
  `CHC5_XILINX_FW/build.tcl`. It does the same six steps. The GUI waits while the build runs.
- **Only create the project** (for example to edit the block design first): in the Tcl Console,
  `cd <path>/CHC5_XILINX_FW`, then **Tools -> Run Tcl Script...** -> `rebuild.tcl`. From a shell:
  `cd CHC5_XILINX_FW && vivado -source rebuild.tcl`.
- **Open an existing project:** `vivado CHC5_XILINX_FW/CHC5_XILINX_FW/CHC5_XILINX_FW.xpr`,
  or **File -> Open Project...**.
- **Build from the GUI:** Flow Navigator -> **Generate Bitstream**, then run `build.tcl` to check
  and package. It sees the bitstream is up to date and does not rebuild.

The block design is `design_1` (**Flow Navigator -> IP INTEGRATOR -> Open Block Design**).

---

## Expected results

The build meets all timing constraints with the default strategies (*Vivado Synthesis Defaults*,
*Vivado Implementation Defaults*).

These critical warnings are expected and harmless:

| Message | Why |
|---|---|
| `PSU-1`, `PSU-2` negative `DQS_TO_CLK_DELAY` | Board DDR trace-length data; the PS DDR controller handles it. |
| `Common 17-55` "set_property expects at least one object" (x96), `Common 17-142` `CONFIG_VOLTAGE` | `CHC5.xdc` covers board pins this design does not use. |
| `BD 41-1343`, `BD 41-967` | Reset and clock association on the USB bridge path. |

---

## Design overview

All camera-side processing runs at 4 pixels per clock.

```
MIPI CSI-2 RX Subsystem (4 lanes, RAW12, 1250 Mb/s per lane)
  +-> Crop -> Black-level corrector --+-> Pixel packer 0 (Bayer)
                                      +-> Pixel packer 1 (Bayer)
                                      +-> Demosaic (12-bit) -> 12->8-bit -> CSC RGB->RGB -> Gamma LUT --+
                                                                                                        |
   +----------------------------------------------------------------------------------------------------+
   +-> Pixel packer 0 / 1 (RGB)
   +-> CSC RGB->YUV 4:2:2 -> YUV repack --+-> Pixel packer 0 / 1 (YUV)
                                          +-> Crop / frame decimation -> VDMA -> DDR
                                              DDR -> VDMA -> YUV->RGB -> Video out -> rgb2dvi -> HDMI

Pixel packer 0 -> VDMA -> DDR           (GigE path)
Pixel packer 1 -> FIFO -> 32-bit parallel bus with frame/line sync   (USB 3 bridge)

Camera I/O + PTP time base: trigger input, strobe output, frame sync, PTP-scheduled frame start.
```

**Crop** (`chc5_axis_crop`) cuts a window out of the raw sensor image before any processing, so every
output gets the cropped frame; the typical use is removing the sensor's optical-black border. After
reset it passes the whole frame. Its Linux driver matches the device-tree compatible
`circuitvalley,chc5-crop-1.0`.

**Pixel-packer formats** (chosen at run time by a register write, between frames):
RAW8, RAW12 packed, RAW12 unpacked (16-bit), RGB565, YUV 4:2:2, MONO8,
XRGB8888 (BGRa8) and BGR24 (BGR8).

Each block's register address is in the block design's **Address Editor**.

---

## Clocks

| Source | Frequency | Drives |
|---|---|---|
| `clk_wiz_0` out 1 | 200 MHz | MIPI D-PHY reference |
| `clk_wiz_0` out 2 | 150 MHz | Video pipeline, PS AXI ports, register interfaces |
| `clk_wiz_0` out 3 | 100 MHz | USB 3 bridge parallel bus |
| `clk_wiz_1` out 1 | 133.333 MHz | HDMI pixel clock: VDMA read, YUV->RGB, video out, `rgb2dvi` |
| `rgb2dvi` internal | 666.667 MHz | HDMI serializer (5 x pixel clock) |

---

## Resets used by the Linux drivers

The four Xilinx HLS cores are reset through PS **EMIO GPIO**, because their Linux drivers
require a `reset-gpios` property and pulse it on every stream start. All four are active-low.

| EMIO bit | PS GPIO line | Block |
|---|---|---|
| 0 | 54 | `v_demosaic_0` |
| 1 | 55 | `v_gamma_lut_0` |
| 2 | 56 | `v_proc_ss_0` (CSC RGB->RGB) |
| 3 | 57 | `v_proc_ss_1` (CSC RGB->YUV) |

These cores stay in reset while nothing is streaming. Reading their registers then can
hang the AXI bus, so only access them during streaming.

---

## Changing the design

| You change | How |
|---|---|
| A Verilog module | Edit the file in `src/hdl/`; the project uses it in place. In the block design, accept **Refresh Changed Modules** when Vivado offers it. Then run `build.tcl`. |
| Constraints | Edit `src/constrs/CHC5.xdc` in place, then run `build.tcl`. |
| Manifest fields (name, version, description, features, ...) | Edit `manifest.json`, then run `build.tcl`. It repacks without rebuilding. |
| The block design, project settings, or the list of files | Make the change in Vivado, then **save it back to `rebuild.tcl`** (below). |

> **The block design exists only inside `rebuild.tcl`.** A change you do not save back
> is lost as soon as the generated project folder is deleted.

### Saving your changes back to `rebuild.tcl`

In the Vivado Tcl Console, with the project open:

```tcl
cd <path>/CHC5_XILINX_FW
write_project_tcl -force -paths_relative_to [pwd] -origin_dir_override "." rebuild.tcl
```

Then edit the new `rebuild.tcl`:

1. Search it for your own absolute path (for example `/home/...`) and replace it with
   `${::origin_dir}`. `write_project_tcl` still writes absolute paths for some sources.
2. Append this line. `write_project_tcl` does not export it, and without it synthesis
   looks for a checkpoint left in the old project folder:
   ```tcl
   set_property AUTO_INCREMENTAL_CHECKPOINT 0 [get_runs synth_1]
   ```
3. Put the license header back at the top.

Rules:

- **Do not add `-use_bd_files`.** Reopening a saved `.bd`/`.xci` in a new project silently
  reset an IP setting in this design (the YUV repack byte mapping). Keep the block design as Tcl.
- Keep every source file in `src/`. Files inside the generated project folder are deleted with it.

Check your export by starting over (below) and running `build.tcl`.

---

## Starting over

```sh
cd CHC5_XILINX_FW
rm -rf CHC5_XILINX_FW output .Xil
vivado -mode batch -source build.tcl
```

`rebuild.tcl` stops if the generated project folder already exists. **Never run
`create_project -force` on a folder that contains your sources**, because it deletes them.

---

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `digilentinc.com:ip:rgb2dvi:1.4` not found, or the IP is locked | `third_party/digilent-vivado-library/` is missing or was moved. It must stay inside `CHC5_XILINX_FW/`. |
| License error for `mipi_csi2_rx_ctrl` or `v_proc_ss` | Vivado cannot find a license with those features. Point `XILINXD_LICENSE_FILE` at your license file. |
| Board part `circuitvalley.com:circuitvalley_chc5:part0:1.0` not found | `Circuitvalley_CHC5_Board/` is missing or not next to `CHC5_XILINX_FW/`. For your own projects, see [Installing the board files](#installing-the-board-files). |
| IP version errors while creating the block design | Not Vivado 2024.2. |
| "Project already exists" when running `rebuild.tcl` directly | The generated folder is still there. Use `build.tcl`, which opens it, or see [Starting over](#starting-over). |
| `build.tcl: unknown option` | Only `--jobs N`, `--clean` and `--help` are accepted, after `-tclargs`. |
| `build.tcl: build failed: synth_1 ..., impl_1 ...` | A run failed. Read `CHC5_XILINX_FW/CHC5_XILINX_FW.runs/synth_1/runme.log` or `impl_1/runme.log`. |
| `build.tcl: bootgen failed` | `bootgen` ships with Vivado in its `bin/` folder; check the Vivado installation. |
| `build.tcl: manifest.json rejected` | One or more fields break a camera rule. Each problem is listed; the rules are in [BITSTREAM_ARCHIVE.md](BITSTREAM_ARCHIVE.md#manifest-reference). |
| `build.tcl: manifest.json contains non-ASCII characters` or `... has an escape sequence` | Only plain printable ASCII is allowed in `manifest.json` strings: no accented letters, no `\n` or `\t`. |
| `build.tcl: manifest.json must contain exactly one "..." field` | `build_date`, `git_sha`, `git_dirty` and `vivado_version` must each appear once; the script fills them in. |
| `build.tcl: tar failed (GNU tar is required)` | Run the build on Linux, which has GNU `tar` and `xz`. |
| The build does not pick up a change | Run with `-tclargs --clean` to force a full rebuild. |
| The camera rejects the upload | The message names the field. `build.tcl` checks the same rules before packing, except JSON types (for example a number where a string is expected). |
| HDMI monitor shows no picture | The HDMI output is 1920 x 1080 at 60 Hz with CVT reduced-blanking v2 timing. Use a monitor that accepts it. |

---

## License

Source files (`src/`, `build.tcl`, `rebuild.tcl`) are (c) 2026 Circuit Valley, author Gaurav Singh,
licensed [CC BY-NC-ND 4.0](https://creativecommons.org/licenses/by-nc-nd/4.0/) as stated in each
file header. The board files carry their own `LICENSE`. AMD/Xilinx IP is covered by AMD's license
terms and ships with Vivado; it is referenced by name, not included.

**Third-party code included:** `CHC5_XILINX_FW/third_party/digilent-vivado-library/` contains the
`rgb2dvi` IP and the TMDS interface definition from [Digilent vivado-library](https://github.com/Digilent/vivado-library)
(commit `f4613ff`), unmodified:

- the repository is (c) 2017 Digilent, MIT License (`License.txt`, included);
- the `rgb2dvi` VHDL sources are (c) 2014 Digilent Incorporated, BSD 3-Clause License (in each file header).

Bitstreams built from this project contain `rgb2dvi`, so documentation shipped with such a
bitstream must reproduce Digilent's BSD copyright notice, conditions and disclaimer.
