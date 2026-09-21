# CHC5 FPGA projects (Vivado 2024.2)

Two Vivado projects for the CHC5 camera board (Zynq-7020, `xc7z020clg400-2`).
Use the one that matches your image sensor. Each project folder has its own sources and
scripts; the Vivado board files are shared.

| Folder | For | What it builds |
|---|---|---|
| [RGB/](RGB/) | Colour (Bayer) sensors | Colour ISP from AMD/Xilinx video IP: CSI-2 receive, crop, black level, demosaic, colour-space conversion, gamma, raw/RGB/YUV packing, HDMI preview, PTP camera sync |
| [Mono/](Mono/) | Mono sensors | Grey ISP from AMD/Xilinx video IP: CSI-2 receive, crop, black level, gamma, raw/grey RGB/YUV/mono packing, HDMI preview, PTP camera sync (no demosaic) |
| [CHC5_Vivado_Board_Files/](CHC5_Vivado_Board_Files/) | Both | Vivado board files for the CHC5 board, shared by both projects (found automatically; each project README shows how to install them into Vivado) |

Inside each folder:

| Item | What it is |
|---|---|
| `README.md` | How to build, install the board files and change the design |
| `BITSTREAM_ARCHIVE.md` | The camera's bitstream archive format and every manifest field |
| `CHC5_XILINX_FW/` (RGB), `CHC5_XILINX_MONO_FW/` (Mono) | Sources, `build.tcl`, `rebuild.tcl`, manifest, bundled Digilent HDMI IP |

## Build

Linux, Vivado 2024.2:

```sh
cd RGB/CHC5_XILINX_FW            # or: cd Mono/CHC5_XILINX_MONO_FW
vivado -mode batch -source build.tcl
```

The result is `output/<name>.tar.xz`: upload it to the camera's bitstream library.
Pair the RGB bitstream with colour sensor archives and the Mono bitstream with mono sensor
archives; the two are not interchangeable.

Licenses are listed at the end of each folder's `README.md`.
