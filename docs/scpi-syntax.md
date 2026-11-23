# Query and separator syntax — V4.0.0

This update changes query-marker placement and argument separators. Literal,
case-sensitive names and one command per LF-terminated line are retained.
There is no new error queue or status implementation. Existing error strings,
numeric conversion/range checks and replies are retained. The interface still
implements a limited subset of SCPI syntax rather than full SCPI/IEEE 488.2.

## Client migration

| Before | V4.0.0 |
| --- | --- |
| `MEAS:VOLT 1?` or `MEAS:VOLT 1` | `MEAS:VOLT? 1` |
| `MEAS:VOLT:ADC 1?` or `MEAS:VOLT:ADC 1` | `MEAS:VOLT:ADC? 1` |
| `CAL 0?` or `CAL 0` | `CAL? 0` |
| `INSTR:GAIN 4 1` | `INSTR:GAIN 4,1` |

The question mark is the final character of the header, immediately before
the whitespace that separates it from parameters. Configuration queries
`INSTR:GAIN? 1`, `INSTR:DR? 1`, `INSTR:MODE? 1`, address query `INSTR:RS485?`
and `*IDN?` already use this form and retain their spelling. Channel0 still
selects the local four-channel group over USB. Return values and their order
are unchanged.

Multiple parameters require commas. For example:

```text
INSTR:GAIN 4,1
INSTR:DR 60,0
CAL:VOLT 1,0,0,2.75,1
MEAS:VOLT? 0
MEAS:VOLT:ADC? 0
CAL? 0
```

Spaces and tabs around a comma are permitted: `INSTR:GAIN 4 , 1` is valid.
Whitespace alone cannot separate arguments. Leading/trailing commas, repeated
commas and empty fields are rejected. Question marks in parameters are rejected.

The parser rejects a semicolon anywhere in a line before dispatching any part
of it. `INSTR:GAIN 8,0;*IDN?` therefore cannot change gain. Multiple separate
LF-terminated lines may still arrive in one USB packet and execute in order.
No abbreviations, case folding, optional leading colon, strings, binary blocks
or semicolon path tracking have been added.

The same grammar applies to USB and addressed RS485 command payloads. The RS485
`address requestID payload` envelope keeps its existing spaces and matching-ID
rules. Update host clients and all boards in a chain together; V4.0.0 does not
accept the old measurement/calibration query spellings. The bundled updater
can read the old spelling before upgrading and the new spelling afterwards.

Calibration, address and provisioned serial keep the existing 60-byte flash
format. This update requires no recalibration or serial provisioning.

## Reusable parser

`lib/A2D_Scpi` is version3.0.0. `A2D_ScpiCommandSpec` now contains only
`name`, `id` and `argument_count`; `allow_trailing_query` has been removed.
Register a query using a literal name ending in `?`. Parse output is still
published only after the complete syntax and argument count validate.
Framing and numeric conversion APIs are unchanged. The standalone C11 example
and package tests compile without ADC, HAL or C++ dependencies.

## Verification on 2026-11-22

The common V4.0.0 image is 43,404 bytes, leaving 11,892 bytes of the 54 KiB
application region. This is 40 bytes larger than V3.2.0. Linker RAM usage is
8,664 bytes including the configured heap/stack reservation.

SHA-256: `99412bdba04cf5890bfed730964ac39a3b89dea311538c4fd3bd684c979ec2d9`.

Host checks passed: 15 portable parser groups, 45 application groups, 17 board
service groups, 3 CDC groups, 4 deferred bootloader checks, 16 numeric formatting
cases, the copied-package standalone C example, and 23 Python uploader/identity
tests. These cover addressed RS485 parsing/routing as well as USB.

Physical testing opened only unit0003, USB UID `4977386D3659`, COM10, hub path
`1-10.4.1.1.1.4`. Unit0001 was untouched. Results:

- Initial V3.2.0→V4.0.0 upload and repeat V4.0.0 upload through the hub passed,
  with stored calibration/address/identity preserved.
- 43 malformed query/separator/compound/case commands were rejected with RAM
  calibration/address and ADC configuration canaries intact. Valid comma
  whitespace, query replies, fragmentation and separate coalesced lines passed.
- A canonical remote measurement query reached the existing missing-peer
  timeout in 1.053 seconds; the old spelling was rejected locally.
- 20,000 malformed/blank frames, 280 packet/coalescing checks and 51 boot/session
  checks passed without an unexpected action, detected reset or hang.
- End-to-end measurement checks passed across gain/rate/mode changes and
  temporary calibration. Thirty samples per input with all inputs unconnected
  averaged 0, 0, 0 and 0.000437 V; channel4 ranged 0.0004–0.0005 V. These are
  functional readings, not a reference-voltage accuracy qualification.

No second RS485 peer was connected; physical peer-to-peer interoperability
was not tested. The initial grammar-test harness selected a non-master address
for its forwarding check, which correctly stayed silent. The harness was
corrected to use address0 and rerun; no firmware change was needed.

Detailed records and the tested image are in
`../Validation/2026-11-22_SCPI_Syntax/` relative to the firmware repository.
The compact machine-readable record is [scpi-syntax-tests-2026-11-22.json](scpi-syntax-tests-2026-11-22.json).
