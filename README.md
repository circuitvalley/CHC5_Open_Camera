# CHC5 Open Camera System

![alt text](https://raw.githubusercontent.com/circuitvalley/CHC5_Open_Camera/refs/heads/main/Hardware/Images/Hardware/chc5_camera_system_circuitvalley_publis.JPG)
![alt text](https://raw.githubusercontent.com/circuitvalley/CHC5_Open_Camera/refs/heads/main/Hardware/Images/Hardware/chc5_open_camer_u3v_gige_vision_fpga_camera_custom.JPG)

# CHC5 Camera Specifications

A well-thought-out design, four years of direct development effort, and multiple years of experience building cameras and video stacks.

## Interchangeable Lens Mount

- Minimum flange distance of 8.75mm makes it possible to connect almost any lens
- Currently available lens mounts:
  - C-Mount
  - CS-Mount
  - Canon RF-Mount (Manual Controls)
  - Sony E-Mount
  - M42 Mount
  - M43 or Custom Lens Mount

## Frame Rate

- Virtually no frame rate limit
- 600 Mpixel/second ISP processing rate
- ISP max:
  - 640×480 ~2000 FPS
  - 1080p ~170 FPS
  - 4K ~40 FPS

## Supported Sensor Specs

| Spec | Details |
|---|---|
| Sensors | Supports all sensors that meet sensor specs |
| Interface | All MIPI D-PHY / LVDS / Sub-LVDS / Low Voltage LVDS / SLVS sensors |
| Resolution | ~128 Mpixel max, no min limit |
| Shutter | Global or rolling shutter |
| Number of Data Lanes | Up to 8 lanes - 4 lanes × 1 channel or 4 lanes × 2 channels |
| Number of Sensors | 2 (4 lanes), 3 (2 lanes) |
| Max Data Rate | 1.25 Gbps/lane, max 10 Gbps with 8 lanes |
| Pixel Format | RAW8, RAW12, RAW14, PWL HDR |
| Frame Rate | Freely user-configurable; sensor- and streaming-interface-dependent (see individual sensor specs) |

## Currently Available Sensors

### Rolling Shutter

| Sensor | Resolution | Notes |
|---|---|---|
| IMX477 | 12.3 MP | Generic, high quality |
| IMX485 | 8.4 MP | Low light, high sensitivity, large pixel/sensor |
| IMX678 | 8.4 MP | Low light, very high sensitivity |
| IMX585 (Color) | 8.4 MP | Low light, even higher sensitivity, larger pixel/sensor |
| IMX585 (Mono) | 8.4 MP | Even higher sensitivity than IMX585 Color |
| IMX283 | 20.3 MP | Large 1" sensor size |
| IMX294 | 10.7 MP | Massive 4/3 format, low light, high sensitivity |
| OX08B40 | 8.3 MP | 140 dB extreme dynamic range, PWL HDR, automotive sensor |

### Global Shutter

| Sensor | Resolution | Notes |
|---|---|---|
| IMX568 | 5.1 MP | Global shutter sensor |
| IMX565 | 12.3 MP | Global shutter, large sensor size |

## Filter

- Internal, user-changeable IR cut filter
- External, user-changeable standard 43mm filter mount

## Camera Interface

- USB3 5 Gbps - USB UVC, USB3 Vision
- Ethernet 1 Gbps - GigE Vision
- HDMI 1080p
- Simultaneous streaming over all 3 interfaces

## Streaming Protocols

| Interface | Protocol(s) |
|---|---|
| USB3 | UVC (standard webcam), USB3 Vision |
| Ethernet | GigE Vision |
| HDMI | Standard |

## Aux I/O

- 1× Isolated input, user-configurable
- 1× Isolated output, user-configurable



<a href="https://www.youtube.com/watch?v=F9n1e-QN0zk">
<img src="https://raw.githubusercontent.com/circuitvalley/CHC5_Open_Camera/refs/heads/main/Hardware/Images/chc5_open_camera_youtube.png" alt="OpenSourceCamera" width="720" height="400">
</a>


Shield: [![CC BY-NC-ND 4.0][cc-by-nc-nd-shield]][cc-by-nc-nd]

This work is licensed under a
[Creative Commons Attribution-NonCommercial-NoDerivs 4.0 International License][cc-by-nc-nd].

[![CC BY-NC-ND 4.0][cc-by-nc-nd-image]][cc-by-nc-nd]

[cc-by-nc-nd]: http://creativecommons.org/licenses/by-nc-nd/4.0/
[cc-by-nc-nd-image]: https://licensebuttons.net/l/by-nc-nd/4.0/88x31.png
[cc-by-nc-nd-shield]: https://img.shields.io/badge/License-CC%20BY--NC--ND%204.0-lightgrey.svg


![alt text](https://raw.githubusercontent.com/circuitvalley/CHC5_Open_Camera/refs/heads/main/Hardware/Images/chc5_open_source_camera_primary_thumb.jpeg)



