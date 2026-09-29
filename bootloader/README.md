# OpenPixelOSD STM32G431 UART updates

This optional build supports updates through a Betaflight FC using MSP command
245 with payload `[0xFD, serialPortIdentifier]`. The FC bridges USB to its existing
UART. OpenPixelOSD uses USART1, **PA9 TX / PA10 RX, 115200 8N1**. No second USB
connector is required. This is a custom bootloader, not the STM32 ROM protocol.

The standard OpenPixelOSD build is unchanged unless `UART_BOOTLOADER=ON` is set.
Only STM32G431 devices with 128 KiB flash (device ID `0x468`) are supported.

## Build

Install Arm GNU Toolchain, CMake and Ninja, with their executables on PATH.
From the repository root:

```sh
cmake -S bootloader -B build/bootloader -G Ninja
cmake --build build/bootloader
cmake -S . -B build/uart -G Ninja -DCMAKE_BUILD_TYPE=Release -DTARGET_MCU=STM32G431 -DTARGET_BOARD=GENERIC -DUART_BOOTLOADER=ON
cmake --build build/uart
```

Choose the actual board target and clock variant as for your regular OpenPixelOSD
build. For a board without an external oscillator, add `-DBUILD_VARIANT=NO_OSC`.
The bootloader itself always uses HSI16 and needs no external oscillator.
`BLINKY` and STM32G474 are intentionally rejected for the UART application build.

Outputs for the commands above:

- `build/bootloader/OpenPixelOSD_Bootloader.hex`: one-time ST-Link installation.
- `build/uart/OpenPixelOSD_STM32G431_UART.hex`: select in Configurator for updates.

The bootloader image includes erased-value padding through `0x08001FFF`, so an
installation that erases the image's sectors also clears the old commit marker.
The linker prevents boot code from overflowing its 6 KiB region. The application
linker prevents executable/data content from spilling into the fixed logo region.

## One-time installation

1. Connect ST-Link to the G431 SWDIO, SWCLK, GND and target voltage reference.
   Use the existing board power supply. Select the **G431**, not the F405.
2. Back up the G431 flash if you need to retain the existing firmware/settings.
   Settings occupy `0x0801F000–0x0801FFFF`.
3. In STM32CubeProgrammer, erase **only G431 pages 0–3** (the first 8 KiB), then
   program and verify `OpenPixelOSD_Bootloader.hex`. Do not perform a full-chip
   erase if you want to retain settings. Do not change protection/boot option bytes.
4. Disconnect the debugger and reset the G431. Without a committed application,
   the bootloader waits indefinitely for the first UART upload.

The temporary echo firmware must be replaced by this bootloader. The standard
OpenPixelOSD HEX starts at `0x08000000`; it cannot be relocated by the uploader.
Use the `_UART.hex` application built with the option above.

## Program through the flight controller

Wiring: FC TX to PA10, FC RX to PA9, common ground; power both boards.

1. Connect the F405 to Configurator over USB, with its DisplayPort/MSP UART already
   configured at **115200 8N1**. MSP passthrough preserves the current UART framing.
2. Open **Firmware Flasher → Flash → OpenPixelOSD / External UART device**.
3. Select that UART by name, set expected baud to **115200**, and click **Load
   OpenPixelOSD HEX**. Choose `OpenPixelOSD_STM32G431_UART.hex`.
4. Click **Start UART passthrough**, then **Program STM32G431**. The Configurator
   stops normal MSP traffic and uses only raw bootloader frames. Do not run the
   echo test with this firmware.
5. Wait for programming and flash CRC verification to complete. The bootloader
   acknowledges RUN, drains UART TX, and resets into the application after its
   1.5-second boot window. This acknowledgment confirms the restart request, not
   that the application's video output is functioning.
6. Click **Disconnect** and reset the **F405** to exit passthrough and restore MSP
   DisplayPort. Verify OpenPixelOSD video and settings.

Later uploads automatically reboot the UART-enabled application into its resident
bootloader using a CRC-checked ENTER packet. ST-Link is not required again for
normal updates. An application crash can prevent ENTER from being processed;
reset the G431 while the uploader is connecting to catch the 1.5-second window,
or use ST-Link for recovery.

Cancellation, loss of power or loss of connection before COMMIT leaves no valid
commit marker. Reconnect and restart the **whole** upload. No command resumes an
uncertain partial write, and the uploader never automatically retries erase,
write or commit. A verified, committed image can boot after a reset even if its
final acknowledgment was lost.

## Flash layout and protocol v1

| Region | Address range | UART updater access |
|---|---|---|
| Bootloader | `08000000–080017FF` | Never writable |
| Commit metadata | `08001800–08001FFF` | Dedicated erase/commit only |
| Application code/data | `08002000–08018FFF` | Update |
| Logo | `08019000–0801AFFF` | Update if included in image span |
| Font | `0801B000–0801EFFF` | Update if included in image span |
| Settings | `0801F000–0801FFFF` | Never writable |

The uploaded image is contiguous from `0x08002000` to the highest HEX byte,
padded to eight bytes. Gaps are `0xFF`. Pages covering this span are erased;
fonts/logo within the span are replaced. No settings records are accepted.

Every frame is `magic[4], command:u8, sequence:u16, length:u16, payload[length],
crc32:u32`, with little-endian integers. Requests use ASCII `OPBL`; replies use
`OPBR`, `command | 0x80`, the same sequence, and a status byte followed by data.
CRC-32/ISO-HDLC covers command through payload (reflected polynomial `EDB88320`,
initial/final XOR `FFFFFFFF`, check value `CBF43926` for `123456789`). Maximum
request payload is 260 bytes. An incomplete packet expires after a 100 ms gap.

| Command | Request payload | Successful response data (after status) |
|---|---|---|
| `01 INFO` | Empty | `OPG4`, version:u16=1, device:u16=0468, app start:u32, app end exclusive:u32, page bytes:u32=2048, write bytes:u16=256, flash KiB:u16=128 |
| `02 BEGIN` | image bytes:u32, image CRC32:u32 | Empty |
| `03 WRITE` | relative offset:u32, 8–256 bytes, multiple of 8 | Empty |
| `04 COMMIT` | Empty | Empty |
| `05 RUN` | Empty | Empty, then system reset |
| `10 ENTER` | ASCII `BOOTG431` | No reply; application sets TAMP backup register 2 and resets |

Statuses: 0 success, 1 command/length unsupported, 2 range/alignment/order error,
3 invalid state, 4 flash error, 5 image CRC/vectors invalid, 6 unsupported chip.
Malformed packets/CRC errors receive no response. INFO must succeed before BEGIN.
WRITE offsets must be strictly sequential. COMMIT verifies flash CRC and vectors,
then writes `3147504F CEB8AFB0` to the metadata doubleword. Metadata is erased
**before** any application page. RUN requires a committed image. A full image CRC
is not repeated on every boot because fonts/logo can legitimately change at runtime.
An ECC double error from an interrupted flash program prevents boot/commit and
leaves the bootloader available for a new erase/upload.

## Validation

The bootloader builds with `-Wall -Wextra -Werror`. Both standard and relocated
application builds compile. The existing application RWX linker warning also
occurs in the standard build.

The emulator tests execute the built ARM binaries, including the application's
ENTER handler. Install the optional test dependency and run from the repo root:

```sh
python -m pip install unicorn==2.1.4
python bootloader/test_bootloader.py
```

They cover protected regions, protocol corruption, chip identity, write ordering,
flash errors, CRC verification, incomplete-image recovery, and application entry.
The MMIO model does not validate real flash timing, electrical behavior, or ECC
hardware. Before relying on this updater, verify one physical upload, a second
upload from the running OSD, and recovery after interrupting an upload. Keep
ST-Link available for this first hardware validation. Compare a post-upload flash
read with the selected image and confirm that the settings region is unchanged.
