# V2.1.1 release candidate

This candidate includes the PlatformIO migration, modular firmware, reusable
C++11 SCPI parser and previously qualified V2 fixes. It adds two corrections
found by adversarial testing of the V2.1.0 development image on unit0003.

## Corrections since V2.1.0

- A malformed command beginning with `1EAF` after USB control-line activity
  could request a CPU reset. The bootloader hook now requires an exact four-byte
  reset packet. Longer invalid packets reach the normal command parser and are
  rejected. The existing USB upload handshake remains supported.
- Extreme finite calibration coefficients could make Arduino Print emit `ovf`
  inside a numeric reply. The dispatcher validates every value in a reply before
  printing any of them. Finite values outside the formatter's range now produce
  `ERROR:NUMERIC RANGE`; failed/nonfinite ADC reads retain `ERROR:ADC READ`.

The settings layout, calibration calculation and command names are unchanged
from V2.1.0. No recalibration or storage migration is required for that update.
The SCPI parser library remains version1.0.0.

## Build and update

Use the pinned PlatformIO setup and a unit-specific `platformio.local.ini`.
The hardware qualification image is built for serial0003; it is not a universal
image for other serial numbers. Build the application with the intended device
serial, port and USB chip identity before using the guarded updater.

Application address:0x08002000. Maximum binary:55296 bytes. Settings pages:
0x0800f800 and0x0800fc00. Field updates retain the existing Maple bootloader and
the hub-safe disconnect/reset sequence. Initial manufacturing uses SWD.

## Qualification

Stress qualification completed on2026-11-21 (Toronto). The V2.1.1 candidate
passed the following on unit0003 through its original hub:

- 62 host groups/checks, plus250000 parser cases,78376 invalid USB dispatches
  and100000 malformed/foreign RS485 frames using the actual application code.
- 20000 malformed/blank USB frames and5000 invalid calibration-number cases.
- 600 initial gain/rate/mode cycles and100 serial-port reopen/line-setting cycles.
- A600.11-second mixed-load soak:11414 state cycles,22828 measurement vectors,
  457 discarded64KiB frames and12 unread-reply flood/recovery rounds. Each flood
  attempted5000 identity requests; host write timeouts under backpressure were
  expected and every recovery/canary check passed.
- Three successful firmware uploads through the hub, including non-default
  saved calibration/address surviving a full MCU reset/update and applying to
  the real input, followed by restoration of saved nominal calibration/address0.
- 23 persistence/query checks,15 update-cycle checks,34 final command checks
  and10 final measurement checks with60 samples per channel and all12 gain/rate
  combinations. No detected unintended action, reset or hang after the fixes.

USB and ST-Link builds pass; ST-Link was build-only. The binary is53768 bytes of
55296 allowed, with1528 bytes remaining. Static RAM is6380 of20480 bytes.
Final results, before/after records, source/image snapshots and checksums are
under `Validation/2026-11-21_Release_Stress` in the hardware project directory.
The development worktree remains uncommitted; this is a qualified candidate,
not a published/tagged release.

## Known limits

- Measurements used unconnected inputs. Accuracy and recalibration need known
  reference voltages.
- No physical RS485 peer or physical power-cut fixture was attached. Malformed
  frames, correlation, CRC fallback and interrupted flash commits are exercised
  with host fault injection; real missing-peer timeouts are tested on the unit.
- Sustained USB priority and the other proposed SCPI/RS485 enhancements remain
  deferred in [issue #1](https://github.com/mbA2D/A2D_4CH_Isolated_ADC/issues/1).
- Hardware validation is for unit0003 through its hub. Unit0001 is excluded.
- V1-to-V2 storage/protocol migration was not repeated in this run. Unknown or
  corrupt request-ID metadata intentionally disables forwarding; service must
  preserve the deployed ID namespace. The no-migration update qualified here is
  V2.1.0 toV2.1.1.
