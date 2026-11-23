# Persistent identity and STM32 HAL migration

Historical V3.0.0 development checkpoint. The current release is C11, uses
230400-baud RS485 and queried errors rather than the replies shown here.
Use the [current manual](manual.md) and [update guide](updating-firmware.md).

V3.0.0 is an unreleased development candidate on `stm32-hal-migration`.
The published V2.1.3 release/tag remain unchanged. Deferred interface features
in issue #1 were not added.

## Serial identity

A unit's four-digit instrument serial now lives in the same CRC-protected,
alternating two-page flash journal as calibration, address and request-ID
high-water marks. The record grows from 56 to 60 bytes; application and page
addresses do not change. Valid V2.1.x records are migrated in RAM and committed
without discarding their calibration/address/ID history. Their initial serial
is `UNASSIGNED`; there is no serial number compiled into the common image.

USB-only `SYST:SER:SET NNNN` validates exactly four decimal digits. It writes
both recoverable copies before replying `OK`, can repair an interrupted mirror
by retrying the same number, and refuses a different stored number with
`ERROR:IDENTITY LOCKED`. Calibration initialization/save/reset, address saves
and application updates retain the serial. It is a software write-once policy;
a mass erase or deliberately changed firmware can remove it.

`tools/provision_identity.py` verifies the USB UID before opening the port,
accepts only the intended serial or `UNASSIGNED`, writes the serial, reads
`*IDN?`, sends `*RST` and reads identity again. The updater's explicit
`--provision-serial` flag additionally preserves the verified physical path
for the first transition from a compiled serial. Ordinary updates omit it.
The intermediate V2.2.0 common image was provisioned and uploaded twice on 0003
before changing platforms, isolating identity migration from HAL migration.

In the inspected old Arduino source, the EEPROM write/read paths were
commented out, and `*IDN?` printed a compiled `SERIAL`
macro rather than the stored getter. A sketch-local preprocessor definition
also cannot configure a separately compiled library `.cpp`. This was not
reproduced as an Arduino IDE or flash-hardware defect. The new getter formats
the stored value on every read; host power-loss tests and hardware updates
exercise the actual persistence path.

## Platform and architecture

The native CMake/Ninja target builds selected, pinned STM32CubeF1 v1.8.7
sources. Provenance and component licenses are in `vendor/STM32CubeF1`.
The build uses a standalone Arm GNU toolchain and a project Python virtual
environment for CMake/Ninja/uploader tooling. Arduino and PlatformIO are not
required. A clean build with PATH restricted to the virtual environment,
standalone compiler and Windows tools produced the identical application hash.

C owns startup, clocks, GPIO, I2C, USART3, USB CDC and hardware reset. The
application, calibration/settings/transport modules and reusable SCPI library
remain small C++11 modules. There is no application allocation, exception
handling, RTTI, RTOS or generalized service framework. The C library's numeric
parser may use its bounded heap; the USB class uses one static allocation.
No generated CubeMX project or bootloader binary was introduced.

The app remains linked at `0x08002000` with VTOR set to that address. The
linker rejects overlap with settings at `0x0800F800`; flash capacity is the
specified 64 KiB, independent of reports of extra flash on some F103 parts.
USB UID conversion and VID/PID remain unchanged, so0003 returns as COM10.
Clocking is HSE 8 MHz, SYSCLK 72 MHz, USB 48 MHz; ADC I2C is 100 kHz and RS485
USART3 is 57600 8N1. Pin mapping remains PB6/PB7 I2C, PB10/PB11 USART3, PB1 DE,
PC13 active-low LED and PA11/PA12 USB.

CDC uses a separate 64-byte packet buffer and 512-byte receive ring. OUT is
armed only with room for a full packet, and remains NAK while full; it never
arms a zero-length bulk receive. DTR edges abort/clear old endpoint completions
and software queues, increment a session generation and cancel old forwarded
transactions. Replies retain the 127-byte limit and 100 ms bounded wait.
`SYST:BOOT` still disconnects D+ for 500 ms before reset into the existing Maple
loader. No raw `1EAF` trigger, bootloader rewrite or driver replacement was added.
The application corrects ST's default self-powered configuration attribute to
bus-powered, consistent with the USB GET_STATUS response and existing 100 mA
configuration budget. Vendor sources remain unchanged.

## Flash size and the C question

| Build | Compiler | Application binary bytes | Free of 54 KiB |
| --- | --- | ---: | ---: |
| Published V2.1.3 Arduino | GCC12.3.1 | 54,688 | 608 |
| V2.2.0 Arduino common identity | GCC12.3.1 | 55, 280 | 16 |
| V3.0.0 HAL, hardware candidate | GCC10.3.1 standalone | 41,908 | 13,388 |
| V3.0.0 HAL, compiler comparison only | GCC12.3.1 | 41,572 | 13,724 |

The tested candidate is 24.2% smaller than the Arduino common-identity image.
With the same GCC12 compiler family, the HAL build is 24.8% smaller. These are
whole-platform/build comparisons, not a controlled C-versus-C++ experiment.
The lightweight application is still C++; a language rewrite is not needed
for this saving. Floating-point conversion, parsing and USB/HAL code account
for substantial retained code and can also be present in a C-only firmware.

GCC10 candidate RAM allocation is 8,496 bytes including the linker's reserved
1 KiB heap and2 KiB stack region. Actual `.data` is 852 bytes and `.bss` 4568
bytes, with alignment padding. This is not a measured maximum stack watermark.
The candidate SHA256 is
`ffc9ee0c396954d76d8e43e4cc9afecd040495c39412386880145dab0341dd29`.
The GCC12 comparison image was not flashed.

## Qualification

Hardware qualification uses only 0003 (`4977386D3659`) on COM10 through hub
`1-10.4.1.1.1.4`. Unit 0001 is excluded. Inputs are unconnected and RS485 terminals
are disconnected, as confirmed by the user. Full raw evidence is in
`../Validation/2026-11-22_Identity_HAL`.

Final results are summarized in `hal-migration-qualification-2026-11-22.json`.
The host suite covers application commands, actual CDC buffering/session code
with fake hardware, calibration/flash services, serial power loss at every
flash operation, legacy journal migration and the copied standalone SCPI
package. Python tests cover uploader/provisioner identity/path guards and
common-image checks. Real-device tests include malformed traffic, reset/state
canaries, packet boundaries, session changes, persistence and repeated USB updates.

The final image passed 34 command checks, 280 packet/coalescing checks, 315
USB session checks, 33 persistent-identity checks, 23 persistence checks and 10
measurement checks, including 60 samples per input and all 12 gain/rate settings.
Mean calibrated readings were 0, 0, 0 and 0.443 mV on the unconnected inputs.
Non-default calibration/address and serial 0003 survived a real hub update;
a further common-image hub upload also passed.

Before the descriptor correction, a 600.078-second soak passed10, 333 state
cycles, 20,666 measurement vectors, 414 discarded 64 KiB frames and 12 unread-reply
backpressure rounds. That image and its 100,000-frame fuzz run are retained
separately. The final descriptor-corrected binary passed a fresh 100,000-frame
run with a different seed; its additional soak is recorded separately in JSON.
No reset or unintended state change was detected by the volatile canaries.

Unconnected inputs verify acquisition/formatting/configuration behavior; they
do not establish absolute accuracy. No second RS485 peer, manufacturing SWD
programming, actual interrupted-update power test, different PC/driver setup,
or calibrated reference source was qualified in this migration. The installed
Maple bootloader and its existing recovery constraints are retained.

Do not downgrade a provisioned unit to V2.1.x: that firmware cannot read the
new serial-bearing journal. V2.2.0 understands the new journal and is retained
as the common-image recovery checkpoint in the validation folder.
