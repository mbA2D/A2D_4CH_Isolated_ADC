# RS485 baud and stress qualification — 2026-11-22

Units 0001 and 0003 completed the production-rate baseline and final soak in both
master directions. Both were restored to standard V5.0.0 at 57,600 baud, original
address 0, original ADC settings and unchanged calibration readback.

## Setup and scope

- Both units connected by USB through the shared hub. Every upload checks the
  expected board serial, USB UID and USB hub location; units upload sequentially.
- RS485 cable: approximately 50 cm, one twisted pair removed from UTP CAT5e cable.
  Termination enabled at both ends, as confirmed by the user.
- One master at a time; roles reversed in every qualifying run. The peer uses
  live address 1, and boundary checks temporarily use address 31.
- Software capacity is 32 boards including the master (addresses 0–31), or
  128 channels. This experiment contains two physical nodes and does not qualify
  the electrical loading or reliability of a 32-node installation.

## Production firmware at 57,600 baud

| Phase | Requested active duration | Total elapsed | Forwarded queries | Maximum-length payloads |
| --- | ---: | ---: | ---: | ---: |
| Baseline | 15 min | 900.495 s | 70,534 | 7,592 |
| Final soak | 30 min | 1800.538 s | 140,889 | 15,161 |

Each phase spends half its requested duration in each master role. Elapsed time
also includes preparation and restoration. Queries to missing devices are
deliberate negative tests, followed by verified recovery; they are not counted
as successful measurement replies.

The two production runs total **211,423
successful forwarded queries over 45 active minutes**. In the final soak,
0003 forwarded 73,368 queries and 0001
forwarded 67,521; both exceeded the
65,536-request allocation block size without a reset/settings-canary failure.
This checks continued operation across another durable allocation, although
the request IDs themselves were not independently captured on the wire.
All 10 peer USB pressure rounds
in the final soak blocked host writes while the accompanying RS485 checks passed.

## Temporary baud builds

Each passing variant completed a functional test (466 forwarded queries,
72 checked configuration setter variations, both master directions), followed
by at least 60 active seconds of stress. Failed variants stopped during their
functional run, before the timed stress test. Latency is host-observed per-query
round-trip time; the mix includes maximum-length padded commands and is not
a bus-only timing measurement.

| Baud | UART transmit timeout | Result | Stress queries | Stress median / maximum latency (ms) |
| ---: | --- | --- | ---: | ---: |
| 9,600 | Original 100 ms | Expected error 1102 on 200-byte frame | — | — |
| 19,200 | Original 100 ms | Expected error 1102 on 200-byte frame | — | — |
| 9,600 | Baud-aware | PASS | 831 | 47.279 / 242.266 |
| 19,200 | Baud-aware | PASS | 1,618 | 24.257 / 122.151 |
| 38,400 | Original 100 ms | PASS | 3,119 | 12.699 / 62.431 |
| 115,200 | Original 100 ms | PASS | 7,988 | 5.008 / 22.545 |
| 230,400 | Original 100 ms | PASS | 13,156 | 3.104 / 12.645 |
| 460,800 | Original 100 ms | FAIL | Not run | — / — |
| 1,000,000 | Original 100 ms | FAIL | Not run | — / — |

The stock UART transmit timeout is 100 ms. A 200-byte 8N1 frame needs 208.33 ms
at 9,600 baud and 104.17 ms at 19,200 baud; the original-timeout diagnostic images
reproduced error 1102, `RS485 write failed`, at both rates. The separate baud-aware
test images allow at least wire time plus 20 ms, retaining a 100 ms minimum.
These are isolated qualification builds, not a new production baud command.
The normal V5.0.0 image remains unchanged.

### Higher-rate failures

At 460,800 baud, unit 0003 timed out waiting for peer 0001 after 163 successful
forwarded queries and 55 successful concurrent USB/RS485 rounds in the first
master direction. Unit 0001 then stopped accepting USB writes and Windows serial
port configuration (error 31). A software PnP restart was denied. The user
reconnected only 0001; it recovered with its original calibration readback.

At 1 Mbaud, 108 forwarded queries completed before the first simultaneous
peer-USB/RS485 check timed out. Both USB ports remained responsive, and the
normal firmware restoration completed automatically. Neither high-rate build
completed both directions or the timed stress phase. Their failures are recorded
as failures, not successful rate qualification. The physical cause has not been
established; the MAX3485 component speed rating alone does not qualify this
interrupt-driven firmware and its bus turnaround timing at those rates.

## Stress checks and preservation

Runs exercise all six forwarded query families, all four channels, exact
configuration/calibration reply checks, finite ADC readings, ordered USB bursts,
peer USB activity during RS485 requests, maximum-length forwarding payloads,
missing-peer timeout recovery, USB DTR session recovery with a 300 ms quiet gap,
and peer USB output backpressure while RS485 continues.

Non-default live gains, rates and peer address act as reset/wrong-action
canaries. These checks detect an unexpected return to startup defaults; there
is no independent reset-counter or logic-analyzer capture. Setter stress
alternates channel 1's gain and verifies restoration; the functional runs cover
all channels and setter combinations. The tests do not write calibration,
saved addresses or serials. Normal durable request-ID allocation does write
its existing journal as designed.

Combined successful forwarded queries across qualifying runs:
**240,465**. Detailed counts, per-master durations,
latencies, binary checksums and final readings are in the companion
[machine-readable record](rs485-baud-stress-2026-11-22.json).

## Device count and installation limits

Adding receivers increases electrical/capacitive load; longer wiring and stubs
can change reflections and noise margins. Only the two physical cable ends
should be terminated. Idle peers add no protocol response slot to a query for
another board; a full scan grows with the number of serial requests, and each
missing peer can consume the approximately one-second response timeout.

The board's [MAX3485](https://www.analog.com/en/products/max3485.html) supports
up to 32 transceivers under its specified conditions. That component rating and
the firmware's address range do not establish a qualified 32-board system.
See also [TI's RS485 design guide](https://www.ti.com/lit/an/slla272d/slla272d.pdf)
for loading, termination and topology guidance. Address-31 tests verify routing,
not the loading of 32 connected receivers.

## Final state and evidence

Standard firmware SHA-256: `10bd9c9fddc0aaba833d4808af189d25a8f1b8f3ea1e5bc8dfe5c5de915d5806`.
Final checks read both identities, stored settings after `*RST`, calibration,
ADC configuration, all four input readings and empty error queues. Both original
addresses are 0; assign distinct addresses before later normal bus use.

Raw evidence is in `../Validation/2026-11-22_RS485_Baud_Stress/` relative to the
firmware repository: build manifest and binaries, isolated test source, per-unit
upload logs and snapshots, per-rate summaries, sampled transaction traces,
baseline and soak summaries, and final verification. Each stress run hashes all
records generated by its harness but saves only sampled records. Checked burst
replies are counted without individual records; this is not a complete USB or
RS485 wire capture. Without a known
voltage reference or electrical noise injection, these results establish neither
absolute voltage accuracy nor physical-layer bit-error rate.
