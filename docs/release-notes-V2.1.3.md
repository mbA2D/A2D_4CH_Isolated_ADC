# V2.1.3 release

## Changes since V1.0.2

This release moves development to PlatformIO/VS Code with pinned STM32 core and
toolchain versions, while retaining the installed Maple bootloader for USB field
updates. The firmware is split into small measurement, calibration, settings,
protocol, transport and platform modules. `lib/A2D_Scpi` is a reusable C++11 parser
and bounded line reader with no Arduino or ADC dependency.

The measurement path checks I2C/configuration/readiness failures, uses signed ADC
normalization and returns complete errors instead of plausible failed readings.
Settings use a recoverable two-page CRC journal; RS485 transactions use correlated
request IDs reserved across resets. USB reception/output is bounded and recovers
from packet-boundary, oversized-input and backpressure cases. Calibration checks
limit scale to nominal 2.818181818 +/-10% and stored raw-ADC offset to +/-0.25 V.

**Upgrade compatibility:** V2 RS485 framing and settings are incompatible with V1.
Update all peers in a chain together. A first V1-to-V2 settings migration uses
nominal calibration and address 0; record existing settings and recalibrate with
known references. The one-time upload option allowing that settings reset must
then be removed. V2.1.x repeat updates preserve valid calibration and address.
See [`updating-firmware.md`](updating-firmware.md) for the upload workflow.

## USB fixes in V2.1.3

Implements the two remaining USB failures reproduced in the V2.1.2 adversarial
retest. Independent review and physical qualification on unit 0003 through its
original hub are complete. Evidence is in
`../Validation/2026-11-22_USB_Boot_Sessions/` and
`docs/usb-boot-session-qualification-2026-11-22.json`.

## Bootloader entry

`SYST:BOOT` with LF or CRLF is a USB-only command with no arguments or query
form. It requests the existing deferred 500 ms disconnect/reset sequence.
The legacy raw `1EAF` interrupt trigger and DTR-update counter are disabled.
Packet fragmentation cannot turn a longer malformed line into a boot request.
The installed Maple bootloader, flash layout and calibration limits are retained.

The guarded uploader reads the running version and uses the old handshake only
to transition a verified V2.1.2-or-earlier application. A running V2.1.3-or-later
application receives `SYST:BOOT\n`. The COM handle remains open until DFU is
observed (or detection fails), preventing a DTR-close from discarding the command.
Target identity, physical path, sole-DFU-device and returning-settings checks remain.
Old Arduino/Maple reset helpers cannot enter the bootloader from V2.1.3; use
the updated project uploader (`pio run -e usb_unit -t upload`). USB field updates
continue through the installed bootloader without SWD access.

## USB sessions

The STM32 USB adapter wraps the CDC callbacks to record DTR changes and USB
initialization/deinitialization. It discards queued RX/TX and pending endpoint
completions on DTR edges, re-arms a full OUT packet, and ignores bytes while DTR
is low. The application clears its partial/overflow line state and cancels a
previous USB client's outstanding forwarding transaction before processing new
input. Late replies from old sessions are suppressed; durable RS485 request IDs
and instrument settings are preserved.

Short interrupt guards cover queue reads and enqueue operations. Delays, flash,
ADC operations and command execution remain outside those guards. The shared
installed framework is unchanged; the existing environment-local F1 receive
backpressure patch remains in use.

DTR is a host-controlled readiness signal, not a direct view of an operating
system's COM handles. Clients should keep DTR asserted while communicating and
clear it before closing. Software that leaves DTR unchanged cannot provide an
unambiguous session boundary. For explicit input resynchronization with DTR high,
send a NUL byte followed by LF; it rejects any unfinished line without completing
an old setter. Consume the resulting `ERROR:LINE TOO LONG` reply before a query.

The reusable SCPI library remains 1.0.0. Calibration/settings layout and the
deferred interface proposals are unchanged. Future bootloader choices are in
[`bootloader-options.md`](bootloader-options.md).

## Qualification

76 host groups/checks pass, including actual USB adapter callback models and
the uploader's COM-handle lifecycle. The independent sub-agent found two races
in the first patch: closing before boot-command dispatch and a pending PMA OUT
packet crossing a DTR edge. Both were fixed, regression-tested and re-reviewed
without a remaining concrete blocker. See `usb-boot-session-review-2026-11-22.json`.

Physical tests passed 123 boot/session checks (including 100 immediate
closed-write/open cycles), 164 command-range cases, 100,000 malformed/blank USB
frames with 3,127 state-marker checks, and 64 changing settings commits.
Four application uploads through the hub passed: the one-time transition,
two same-image updates using the new uploader, and fragmented SCPI boot entry
followed by a guarded same-image transfer. Saved calibration/address survived.

The 600.047-second soak completed 11,284 configuration cycles and 22,568 hardware
measurement vectors, discarded 452 oversized 64 KiB frames, and recovered from
12 unread-response floods without losing the volatile state markers. Final
verification passed 34 command/USB smoke checks, 23 persistence checks and
10 measurement checks with 60 samples per unconnected input. Channels 1–3
reported 0.0000 V; channel 4 averaged 0.00044 V. These are functional readings,
not an accuracy qualification. Nominal saved calibration/address 0 were restored.

USB and ST-Link builds pass; ST-Link was build-only. The unit 0003 binary is
54,688 bytes of the 55,296-byte application budget, leaving 608 bytes. Static RAM
is 6,412 of 20,480 bytes. The earlier V2.1.2 candidate remains on hold; these results
apply to V2.1.3. Unit 0001 was excluded from all hardware operations.

The attached binary carries serial 0003 and is intended only for that unit.
Build with the intended device identity for other units. The qualification JSON
records the pre-commit validation snapshot; the release manifest binds its
unchanged firmware sources and binary to the published commit.
Known-reference accuracy, physical RS485 peer
behavior, interrupted application update recovery and cross-PC driver packaging
are outside this qualification.
