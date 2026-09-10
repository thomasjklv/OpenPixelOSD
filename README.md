# OpenPixelOSD

OpenPixelOSD is an open-source project for generating and overlaying pixel graphics onto a video signal (OSD), based on the **STM32G431CBUx/** microcontroller.
The project aims to create a software monochrome On-Screen Display (OSD) for VPV to phase out the obsolete *MAX7456* chip which has been discontinued

## Repository Structure
```pgsql
.github/workflows/                   - CI build for STM32G431/G474
doc/                                 - diagrams and images
cmake/                               - toolchain & stm32 library CMake scripts
python/                              - helper scripts (font/logo conversion, font upload)
USB_Device/, Drivers/, Middlewares/  - STM32Cube generated code
src/                                 - firmware source (C code)
  hardware/                          - peripheral initialization, UART and USB
  render/                            - video generation, overlay and DisplayPort
  stm32g4xx/                         - device startup, HAL configuration
  msp/                               - MSP protocol parser/handler
  fonts/, logo/                      - embedded font/logo data and updater
CMakeLists.txt                       - top‑level build file
```

#### Key Components

- `src/main.c` – program entry. Initializes hardware modules, then continually processes **MSP** messages and blinks an **LED**.

- `src/render/video_overlay.c` – core video overlay logic. Sets up **DACs**, timers and comparators to mix the generated OSD pixels with the incoming video signal.

- `src/msp/` – implements the **MultiWii Serial Protocol** (MSP) for interacting with flight controllers.

- `src/render/canvas_char.*` – double‑buffered character canvas used for text rendering onto the video overlay.

- `python/` – scripts to convert fonts or bitmaps into C arrays for embedding. For example, `convert_logo.py` reads an image, maps colors to 2‑bpp pixels, and outputs a header file. `font_updater.py` sends font data over serial using **MSP** commands.

#### Build and Continuous Integration

The GitHub workflow in `.github/workflows/build.yml` checks out the repo, installs the **Arm GNU toolchain**, builds the firmware for both MCU variants, and uploads the `.hex` and `.bin` artifacts.

Build with CMake 3.22+, Ninja and Arm GNU Toolchain on PATH:

```sh
cmake -S . -B build -G Ninja -DTARGET_MCU=STM32G431 -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
python python/check_includes.py build/compile_commands.json
```

Use a separate build directory with `-DTARGET_MCU=STM32G474` for the larger MCU.
CMake selects the matching startup file, linker script and `LOW_RAM`/`HIGH_RAM`
defines. Each build produces HEX, BIN, ELF, MAP and a ZIP archive.
The include checker also verifies exact capitalization on Windows; CI runs it
for both MCU targets. Project includes use paths relative to `src/` when crossing
module directories, such as `hardware/usb.h` and `render/canvas_char.h`.
VS Code reads include paths and defines from `build/compile_commands.json`.

The main-loop switches are in `src/main.h`: `USE_MSP` is currently commented out,
`DEBUG_LED_BLINK` is enabled, and `PAUSE_ON_FAILED_INIT` is set to 1.

#### What to Explore Next

* **Hardware setup** – Review `README.md` and the diagrams in `doc/` to understand signal routing and timing requirements.

* **Build system** – Read `CMakeLists.txt` and `cmake/` scripts to learn how the project is compiled and linked for different microcontrollers.

* **Video overlay internals** – Study `video_overlay.c` and `video_gen.c` to see how DMA and timers generate the analog video waveform.

* **MSP protocol** – Examine `src/msp/` to understand communication with flight controllers.

* **Python utilities** – The scripts in `python/` show how fonts and logos are converted to C data for flashing onto the device.

The project is licensed under GPL‑2.0, as noted in the LICENSE file. With these parts in mind, a newcomer can navigate the firmware, experiment with modifying fonts/logo data, and extend the OSD functionality.

*Important configuration options live in the main `CMakeLists.txt`. The `TARGET_MCU` variable switches between `STM32G431` and `STM32G474` builds.*

## How It Works

### Hardware Architecture

- The **STM32G431CBUx/G474 microcontroller** handles control and pixel data generation.
- The input video signal is fed into the **OPAMP1 multiplexer input - PA7**, operating in **follower mode**, and to the comparator positive input `vin+` **COMP3 - PA0** for synchronization.
- **DAC1 Channel 1** is used as the negative reference `vref-` for the comparator.
- If no input video signal is present, **TIM17** generates a PWM signal on pin **PB5** to create the video signal.
- To simplify the video signal detection logic and the generation of the internal video signal **TIM17** generates **reference video** signal by producing **PWM** on pin **PB5**, supporting interlaced or progressive scanning.
- Due to the lack of internal synchronization between **TIM17**, **DMA1 Channel 6**, and **TIM1** for precise line start synchronization of the generated video signal, **COMP4** is used, connected via a **1:10** resistive divider to the `vin+` input of **COMP4** and to the **PA3** input of the **OPAMP1** multiplexer.
- Comparators **COMP3** and **COMP4** are used for video signal "parsing" and line start detection.
- The output video signal is formed by mixing (fast switching) signals from the internal **DAC3 Channel 1** and the input video **PA7** via the built-in multiplexer.
- **TIM2** and **TIM3** provide precise synchronization for line and frame start:
  - TIM2 and TIM3 track line start and trigger pixel rendering accordingly.
  - TIM1 handles pixel rendering in the line by transferring two buffers via **DMA1 Channel 1** and **DMA1 Channel 2**.
  - DMA1 Channel 1 transfers the buffer containing precise timing information for switching the multiplexer connected to the OPAMP1 input.
  - DMA1 Channel 2 transfers the buffer containing brightness values for each pixel to **DAC3 Channel 1** for pixel formation in the line.

### Block Diagram
![OpenPixelOSD Block Diagram](doc/pic/internal-block-diagram.png)

### Wiring Diagram
Wiring diagram for quick project launch on WeActStudio:
- https://github.com/WeActStudio/WeActStudio.STM32G431CoreBoard
- https://github.com/WeActStudio/WeActStudio.STM32G474CoreBoard

![OpenPixelOSD Wiring Diagram](doc/pic/openpixelosd-wiring.png)

### Operating Principle

1. **Video Synchronization:** Comparators detect the start of a video line, enabling synchronization of rendering with the input signal.
2. **Timing:** Timers generate precise timing for the start of each pixel display in the line and manage DMA transactions for data transfer.
3. **Rendering:** The processor generates pixel data as a monochrome image with grayscale shades in buffers, which DMA then transfers to the DAC and OPAMP.
4. **Signal Formation:** Using the DAC and internal multiplexer OPAMP, the output video signal with the overlaid **OSD**.

### Reference for PAL/NTSC Standards

For detailed information on PAL and NTSC video standards, please refer to the comprehensive article by Martin Hinner:
https://martin.hinner.info/vga/pal.html

This resource provides authoritative timing diagrams, signal structures, and technical explanations essential for accurate video signal generation and processing.

# Key Features

- No external chips required for video signal detection and generation.
- Utilizes hardware timers and DMA to minimize CPU load.
- CCMRAM is used for fast access to critical data and code.
- Precise synchronization is supported via hardware comparators.
- Software scalability: real-time pixel rendering.
- Using the DAC to control pixel brightness.

## Connection and Setup

TODO:


### YouTube Video

[![YouTube](doc/pic/screenshot.png)](https://youtu.be/GXBrZya5-nY)

## License

Open source software — see LICENSE in the repository.

---
