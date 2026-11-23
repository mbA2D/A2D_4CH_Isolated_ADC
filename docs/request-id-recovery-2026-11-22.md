# Unit 0001 request-ID recovery and RS485 qualification

**2026-11-22 · Final firmware on both units: normal V5.0.0**

Unit 0001 could receive and answer RS485 requests but could not act as master.
Forwarded reads and writes immediately queued `-320,"Storage fault"` before
UART transmission. It had just migrated from V1.0.2, which did not use the
current durable request-ID journal. Unit 0003 already worked as master.

## Confirmed cause

Raw reads of both CRC-valid settings records confirmed `nextRequest=0` on
unit 0001. This deliberately disables allocation; zero also represents an
exhausted/unknown namespace, so the normal firmware must not restart it
automatically. Serial provisioning and calibration restoration had preserved
this disabled state. The journal was readable and the problem was not an
RS485 wiring or transceiver failure.

## Repair performed

A one-off service image, V5.0.901, was built in an isolated copy of the source.
Its service operations required USB, provisioned serial 0001 and the MCU identity
that generates USB serial `4976386E3659`. The production sources and standard
V5.0.0 binary were not changed.

The service image provided two temporary operations: reading a complete 60-byte
settings record as hex, and repairing this known migration case. Both original
records were backed up before any repair write. Host-side CRC checks confirmed
the records were valid and selected the current record by sequence number.

The repair copied that active record, changed only the request-ID starting
point to `0x80000001` (2147483649), and committed/verified both journal pages
through the existing CRC/commit writer. This uses a fresh upper-half range for
this unit's known history, rather than restarting at 1. The routine allowed a
retry at that exact initial value after an interrupted mirror write, but refused
to change a counter that had already advanced. Invalid identity, serial, active
record or CRC caused refusal without writes.

Calibration bytes, stored address and provisioned serial were compared
**byte-for-byte** against the active pre-repair record. They were unchanged in
both repaired records. This operation preserved the current full-precision
calibration; it did not reconstruct coefficients from rounded text or recalibrate.

The normal V5.0.0 image was then reinstalled over the hub. A further normal
firmware upload/restart and another bidirectional test confirmed persistence.
The temporary service command was verified unavailable afterward.

This was a controlled repair for one identified migration case, not a general
counter-reset feature. The service image is an audit artifact, not a field
release or a procedure for exhausted counters. Normal unknown-namespace and
request-ID exhaustion protections remain unchanged.

## Verification

The service build passed the 17 existing board-service groups plus its recovery
test. Recovery was interrupted at every one of 63 flash-operation boundaries.
Each case preserved calibration/address/serial, could resume safely, and then
allocated IDs correctly. Tests also checked wrong-device refusal, damaged-record
refusal, refusal after counter advancement, and valid fallback after allocation.

Physical qualification used both units on their USB hub with RS485 connected:

| Check | Result |
| --- | --- |
| 0003 master → 0001 peer | Passed |
| 0001 master → 0003 peer | Passed |
| Main run, 500 comparison cycles per direction | 1,418 forwarded queries; 72 setter variations verified |
| USB queries while the peer served RS485 | 200 rounds in the main run |
| Coalesced remote/local reply ordering | 96 replies checked in the main run |
| Peer address 31, global channels 125–128 | All six query types on all four channels passed |
| Missing-peer timeout and recovery | About 1.006 s in each direction; next real-peer query passed |
| Repeat after another firmware upload/restart | 466 additional forwarded queries passed in both directions |
| Calibration, serial and saved/live addresses | Preserved |
| Final ADC settings | Original settings restored: gain 1,15SPS, continuous |
| Final addresses, as requested | 0001=0 and 0003=0 |
| Final error queues | Empty on both units |

The main run's median forwarded-query time was 8.849 ms and maximum 68.347 ms.
Actual raw/calibrated readings were compared with direct peer USB readings;
calibration/configuration replies were compared exactly. No external voltage
reference, line-noise qualification or simultaneous multi-master test was used.
To operate a chain after this test, assign distinct addresses and use one master.

Normal image: 43,884 bytes. SHA-256:
`10bd9c9fddc0aaba833d4808af189d25a8f1b8f3ea1e5bc8dfe5c5de915d5806`.

The complete backups, raw journal records, service source/image, host fault
injection results, upload logs and both RS485 runs are in the project's
`Validation/2026-11-22_0001_Request_ID_Recovery/` directory. The compact record is
[request-id-recovery-2026-11-22.json](request-id-recovery-2026-11-22.json).
