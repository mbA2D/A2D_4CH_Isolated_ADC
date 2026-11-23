# V3.1.0 C rewrite

Historical V3.1.0 development checkpoint. Later syntax, error handling and
UART/DMA changes are described by the [current manual](manual.md) and
[V5.0.1 release review](V5.0.1-review-2026-11-23.md).

V3.1.0 is an unreleased development candidate. All production application,
portable SCPI, HAL and glue code is C11, with the vendor startup assembly.
CMake selects only C and ASM and links using `arm-none-eabi-gcc`; no C++
constructors, virtual classes, templates or runtime symbols are required.
The Python update tools and Windows C++ host harnesses are test/host tooling,
not target dependencies. `lib/A2D_Scpi` is now the reusable C11 2.0.0 package.

## Design

Small state structs and prefixed functions replace the existing classes;
there is no new object framework, allocator or service container. Callback
functions plus a context pointer represent ports and the I2C bus. Calibration,
measurement, settings, framing and transport retain separate modules.
Application scheduling and command dispatch are stateless functions around
the initialized transport/board state. Buffers are fixed and caller-owned.

`firmware/app.c` statically binds hardware adapters and explicitly initializes
calibration/settings/measurement/transport before polling. Reader storage must
outlive its context; transport owns its internal line storage and must not be
copied after initialization. The reusable SCPI code has no ADC/HAL dependencies,
uses no explicit allocation and is proved by a copied standalone C example.
The numeric parser still uses libc `strtof`, including libc-dependent internal
allocation and syntax/locale behavior; the bounded libc heap remains.

The command grammar, error classifications, channel mapping, calibration units
and limits, request IDs, 60-byte settings layout and persistent four-digit
serial are retained. The installed Maple loader, application origin 0x08002000,
settings boundary 0x0800F800 and USB chip identity are retained. No recalibration
or serial reprovisioning is needed when updating from V3.0.0.

## Corrections made after review

- Invalid calibrated API channel indices return NaN, preserving the read error.
- Numeric output rounds once using a decimal scale and uint64 quotient/remainder,
  with half-away-from-zero ties at the supported 0..5 places. Exact midpoint
  examples 0.09375/4 digits and 0.046875/5 digits no longer round down.
- A pending complete USB reply is retained in 127 bytes of owned storage.
  USB command consumption pauses until it can enqueue, while addressed RS485
  requests can still run. DTR/session changes explicitly discard old traffic.
  This replaces the 100 ms response-drop policy with nonblocking backpressure.
- Completed requests alternate source priority so complete USB traffic cannot
  monopolize command scheduling. The one outstanding forwarded query still
  preserves USB reply order.
- Forwarding drains at most 512 bytes of old RS485 input; a permanently readable
  source returns `ERROR:RS485 BUSY` rather than trapping the main loop.
- RS485 replies are assembled as one bounded frame and written once. Local
  forward write failures return `ERROR:RS485 WRITE`, restore receive direction,
  and do not start a response wait or retry a possibly executed command.
- The uploader checks path continuity between its two application identifications
  before reset, preventing a stale location from selecting another DFU target.
- The updater saves a live snapshot, reloads saved settings on the same verified
  connection, then compares against that persisted baseline after the update.
  It does not implicitly save volatile calibration/address changes.

Six sub-agents reviewed separate modules and the full system. Their findings,
parent assessment, fixes and retained limitations are in
[c-rewrite-review.md](c-rewrite-review.md). The record distinguishes host-model
results from actual tests on 0003; sub-agents did not access attached hardware.

## Size

| Same standalone GNU Arm 10.3.1 build | Application bytes | Free of 54 KiB |
| --- | ---: | ---: |
| V3.0.0 HAL C++ checkpoint | 41,908 | 13,388 |
| V3.1.0 C with review fixes | 43,020 | 12,276 |

The C result is 1,112 bytes larger. This is not a controlled language benchmark:
it includes the corrected wide-integer formatter, pending-output/fair scheduling
and additional transport checks. The change was chosen for the requested C code
base, not a claimed flash reduction. RAM allocation is 8,664 bytes including
1 KiB heap and 2 KiB stack reservation; this is not a measured stack watermark.

## Verification

The current qualification summary is
[c-rewrite-qualification-2026-11-22.json](c-rewrite-qualification-2026-11-22.json).
Full images, source manifests, raw reports and logs are retained under
`../Validation/2026-11-22_C_Rewrite/`.

Production units were compiled separately as C11 in the host tests. Baseline
C++ differential probes compared calibration results, signed ADC scaling,
parser/numeric results, framing and reader behavior. Target build warnings
are errors for application code; a separate fresh native build and symbol
inspection verify the C-only target and preserve the flash boundary.

Hardware tests select only 0003, USB UID 4977386D3659, COM10, original hub
1-10.4.1.1.1.4. Unit 0001 is excluded. Tests include exact ordered replies after
a 500 ms delayed reader, positive/negative midpoint formatting, command/packet/
session/range checks, malformed traffic, a mixed-load soak, actual unconnected
input readings, stored serial/calibration/address and repeat hub updates.

Inputs are unconnected, so readings do not establish absolute accuracy or a
changing-input freshness claim. RS485 is disconnected, so no second physical
peer or electrical timing is qualified. Host fault injection is not a real
power-interruption test. Manufacturing SWD, different PCs/drivers, interrupted
application update recovery, signatures and rollback remain unqualified here.
Forwarded setter acknowledgements and conservative unknown-ID-namespace recovery
remain explicit limitations, described in the review/update guides.
