# A2D 4CH Isolated ADC firmware

V5.0.1 uses STM32F1 HAL and CMake/Ninja, with no Arduino
or PlatformIO build dependency. One common firmware image serves every unit;
the four-digit device serial is provisioned once in persistent flash. The
installed Maple bootloader and USB field-update workflow are retained.

For setup, all 23 commands, calibration, errors and host examples, use the
**[User and Programming Manual](docs/manual.md)** for V5.0.1.

RS485 defaults to **230400 baud, 8N1**. Update every board on a bus to the
same rate. The [V5.0.1 release](https://github.com/mbA2D/A2D_4CH_Isolated_ADC/releases/tag/V5.0.1)
provides a common image and update package. Older V2.1.3 binaries remain specific
to unit0003; the newer image reads each unit's provisioned serial from flash.

## Build

Install Arm GNU Toolchain (`arm-none-eabi-gcc` on PATH), Python 3, and optionally
VS Code with the recommended C/C++ and CMake Tools extensions. On Windows:

```powershell
py -3.14 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-dev.txt
.\tools\build.ps1
```

The script uses the project virtual environment's CMake and Ninja. Set
`ARM_GCC_BIN` if the compiler is not on PATH. The tested standalone compiler is
Arm GNU 10.3.1 (2021.10); dependencies and license/provenance files are vendored
under `vendor/STM32CubeF1`. No dependency downloads occur during the build.
With CMake and Ninja already on PATH, the equivalent commands are:

```powershell
cmake --preset release
cmake --build --preset release
```

Outputs are `build/release/firmware.bin`, `.elf`, `.hex` and `.map`. The VS Code
**Build firmware** task runs the same build. Pin/clock configuration is explicit
in `hal/src/main.c`; this is a CMake project, not a generated CubeMX `.ioc` project.
All production application, HAL and reusable SCPI sources are C11, plus the
startup assembly. The target build uses the C compiler/linker and no C++ runtime.

## USB update and manufacturing identity

Copy `upload.local.json.example` to `upload.local.json`, enter the target COM
port, four-digit instrument serial, USB chip UID and local DFU tool paths.
The ignored local file identifies the target; it does not change the binary.
Close serial monitors, build, then run:

```powershell
.\.venv\Scripts\python.exe tools/usb_upload.py
```

The VS Code **Upload firmware** task builds first and runs this uploader. It
checks USB UID and `*IDN?` before reset, matches the same physical USB path in
DFU, refuses ambiguous Maple devices, and checks identity/calibration/address
after the update. It records live settings, reloads persisted settings with `*RST`,
and checks that persisted baseline after flashing; unsaved RAM changes are discarded.
See [USB update instructions](docs/updating-firmware.md).

At manufacturing, use SWD to install the existing Maple bootloader plus the
application, then explicitly provision identity over USB:

```powershell
.\.venv\Scripts\python.exe tools/provision_identity.py --port COM_TARGET --serial NNNN --usb-serial TARGET_USB_UID
```

`SYST:SER:SET NNNN` accepts exactly four digits and is USB-only. It is silent,
like other setters. After it completes, query `SYST:ERR?` for success/failure
and `*IDN?` to read back the provisioned serial. Repeating the same number is
safe; a different number queues error1003 (identity locked). Calibration reset/save, address changes and application updates
preserve the serial. A mass erase can remove it; this is a software lock, not
OTP hardware protection. See [migration details](docs/hal-migration.md).

Never infer an update target from its COM number alone. Check both its USB UID
and instrument serial. The [update guide](docs/updating-firmware.md) describes
the one-time transition from older compiled serials and the stored-settings
compatibility limits. The earlier [device update record](docs/device-updates-2026-11-22.json)
documents the initial V5.0.0 update and unit0001's legacy calibration recovery.

## Configuration queries

`INSTR:GAIN? <channel>`, `INSTR:DR? <channel>` and `INSTR:MODE? <channel>` read
confirmed ADC settings without changing them. USB channel 0 returns four values
in channel order; individual channels return one integer. Mode 0 is single-shot
and 1 is continuous. See [syntax and examples](docs/configuration-queries.md).
Other interface improvements remain deferred.

## Command syntax in V4.0.0

Names remain exact and case-sensitive. Each LF-terminated line contains one
command. Query markers belong at the end of the header, before parameters:

| Previous query | V4.0.0 query |
| --- | --- |
| `MEAS:VOLT 1?` | `MEAS:VOLT? 1` |
| `MEAS:VOLT:ADC 1?` | `MEAS:VOLT:ADC? 1` |
| `CAL 0?` | `CAL? 0` |

These reads require `?`; their old spellings are rejected. Existing queries such
as `INSTR:GAIN? 1` and `*IDN?` retain their spelling. Separate multiple arguments
with commas: `INSTR:GAIN 4,1` or `CAL:VOLT 1,0,0,2.75,1`. Spaces/tabs around a
comma are allowed. Missing commas, empty fields and leading/trailing commas are
rejected. Semicolon command lists are rejected as a whole, without executing a
prefix. V5.0.0 adds the queried error handling below; this remains a limited
SCPI-like interface. See [migration and verification](docs/scpi-syntax.md).

## Queried errors in V5.0.0

The firmware sends data only in response to queries. Failed setters and failed
queries queue an error without sending an automatic error line. Setters also
send no `OK` acknowledgement. After a failed query times out, use `SYST:ERR?`
to retrieve its error. Each read removes the oldest entry; an empty queue returns
`0,"No error"`. `*CLS` clears the queue without changing instrument settings.

The eight-entry queue uses RAM only and survives USB close/open and `*RST`.
Overflow preserves the oldest seven errors and records `-350,"Queue overflow"`
as the eighth. The queue belongs to the board and is shared by USB and addressed
RS485 access. The USB master's `SYST:ERR?` does not query other boards' queues.
Status registers, completion commands and RS485 setter acknowledgements remain
deferred. See [error codes, host migration and tests](docs/scpi-errors.md).

## Architecture

Fixed buffers, static state structs, no RTOS or application heap allocation.
Peripheral I/O uses `A2D_HAL.h` and small C callback/context adapters; the flash adapter calls HAL directly.

| Module | Responsibility |
| --- | --- |
| `hal/src/main.c` | Clock/GPIO, I2C, UART, interrupts, main loop, bootloader disconnect/reset |
| `hal/src/usb.c` | Bounded CDC buffers, DTR session generations, receive backpressure |
| `hal/src/usbd_conf.c`, `usbd_desc.c` | ST USB low-level glue and stable USB identity |
| `firmware/app.c` | Initialize explicit C state and adapt HAL I/O |
| `A2D_Application` | Schedule commands and preserve reply order |
| `lib/A2D_Scpi` | Portable parser and bounded line reader; no HAL/ADC dependency |
| `A2D_Protocol` | Command table, arguments, ranges and channel rules |
| `A2D_Dispatcher` | Execute commands and format complete replies |
| `A2D_Transport` | USB reply bounds, RS485 addressing/request IDs/timeouts |
| `A2D_Measurement` | MCP3425 conversions and configuration checks |
| `A2D_Calibration` | Calibration math and range checks |
| `A2D_SettingsStore`, `A2D_Flash` | CRC journal, calibration/address/serial and durable request IDs |
| `A2D_IO`, `A2D_USB_Bootloader` | Small numeric writer and deferred bootloader request |
| `hal/src/uart.c` | Circular DMA receive, UART fault recovery and RS485 turnaround |

## Verification

```powershell
.\.venv\Scripts\python.exe tools/run_tests.py --compiler C:/TDM-GCC-32/bin/g++.exe
.\.venv\Scripts\python.exe -m unittest discover -s tests -p "test_*.py"
```

Tests cover portable SCPI, application behavior, the actual CDC buffer/session
backend with fake hardware, persistent serial migration and interrupted flash
operations. Hardware qualification, review findings and measured size/readings are recorded in
[the C rewrite report](docs/c-rewrite.md) and [review dispositions](docs/c-rewrite-review.md).
Unconnected inputs cannot establish absolute measurement accuracy. The first
[two-board RS485 test](docs/rs485-link-tests-2026-11-22.json) identified a disabled
request-ID counter on 0001 after its legacy migration. A controlled
[unit0001 recovery](docs/request-id-recovery-2026-11-22.md) preserved calibration
and identity and restored normal V5.0.0. Both master directions now pass,
including configuration writes, address31, USB/RS485 concurrency and a repeat
firmware restart. Original addresses and ADC settings were restored afterward.
The extended [baud and stress test record](docs/rs485-baud-stress-2026-11-22.md)
records the 15-minute baseline, 30-minute production-rate soak and failures at
460,800 baud and 1 Mbaud. Both boards finished on normal V5.0.0 with their original
addresses, ADC settings and calibration readbacks.
The subsequent [V5.0.1 investigation and fixes](docs/rs485-high-baud-fix-2026-11-22.md)
correct receive overruns, UART error recovery and RS485 turnaround. Both boards
passed mixed USB/ADC/RS485 stress from 115,200 through 1 Mbaud, then returned to
normal 57,600-baud firmware with their original settings and calibration readbacks.
That historical bench image predates the release's 230400-baud default.
The [release review](docs/V5.0.1-review-2026-11-23.md) records independent
UART/DMA and system findings and their dispositions. The final release's
four-hour qualification and repeat hub updates are recorded in
[the release qualification](docs/V5.0.1-qualification-2026-11-23.md).

The separate SCPI/RS485 improvements remain deferred in
[issue #1](https://github.com/mbA2D/A2D_4CH_Isolated_ADC/issues/1).
[Earlier PlatformIO history](docs/platformio-history.md) and tagged releases
retain the previous build instructions and test records.

The earlier [V3.0.0 HAL migration report](docs/hal-migration.md) records the C++
checkpoint. V3.1.0 preserves its commands and 60-byte settings/serial journal;
no recalibration or provisioning is needed for the C update.
