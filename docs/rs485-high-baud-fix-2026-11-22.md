# RS485 receive and turnaround fixes — V5.0.1

Historical bench record from 2026-11-22. Its normal 57600-baud image predates
the V5.0.1 release candidate, whose default is 230400 baud. The release uses a
different binary hash and a separate four-hour qualification record. See the
[current manual](manual.md) and [release review](V5.0.1-review-2026-11-23.md).

The corrected firmware passed the mixed USB/RS485 qualification at all tested
rates from 115,200 through 1 Mbaud on units 0001 and 0003. Both units finished on
normal V5.0.1 at 57,600 baud with original address 0, ADC settings, serial identity
and unchanged calibration readback. The normal baud rate was not changed.

## Findings and fixes

The previous [V5.0.0 sweep](rs485-baud-stress-2026-11-22.md) failed at 460,800 baud
and 1 Mbaud. Isolated diagnostic builds reproduced receive overruns on the peer.
The original USB path masked all interrupts for about 30 µs in the measured case;
1-Mbaud 8N1 bytes arrive every 10 µs. Narrowing that mask helped but did not remove
all receive failures. The HAL I2C driver's necessary interrupt-masked receive
sequence could still overlap incoming UART data during simultaneous USB ADC reads.

The original UART error callback rearmed reception without explicitly clearing
hardware error flags. An overrun with RXNE clear can therefore leave the high
priority UART interrupt pending and starve lower priority work, including USB.
This is a source-derived explanation for a possible hang path; the original
460,800-baud halt was not captured with a debugger. ST documents the required
SR-then-DR clearing sequence in [RM0008](https://www.st.com/resource/en/reference_manual/cd00171190.pdf).

The receive path now uses one 512-byte circular DMA buffer in `hal/src/uart.c`.
Hardware continues filling it during CPU interrupt pauses. The driver handles
wraps, pending completion events, unread overflow, UART errors and DMA transfer
errors. On damage or uncertain progress it discards through a line boundary and
invalidates any partial SCPI line, preventing execution of a truncated command.
It never attempts to reconstruct lost bytes. USB critical sections mask only USB.

Final review added a check for a pending USART error interrupt even when DMA has
already cleared the status flag by reading DR. A native regression reproduces
acceptance of damaged buffered data in the earlier DMA driver and passes with
the correction. The first timed campaign was deliberately interrupted for this
fix and is excluded from the qualification totals; all table results below use
the corrected V5.99 test builds.

A faster non-DMA receiver removed the initial overruns but exposed a reverse
direction burst failure without UART error flags. An explicit transmit turnaround
guard made that functional test pass; the final driver retains the guard. It
waits at least 100 µs or one character time, whichever is longer, before asserting
DE, and still waits for the final stop bit before releasing DE. The benefit is
demonstrated by before/after tests; exact electrical contention/edge timing was
not measured with an oscilloscope.

The fixed 100 ms UART transmit timeout was also too short for a 200-byte frame at
9,600 and 19,200 baud. It now allows calculated wire time plus 20 ms, with a 100 ms
minimum. Command syntax, response formats and the calibration journal are unchanged.

## Qualification results

Both USB connections stayed on their identified hub ports. RS485 used the user's
approximately 50 cm pair removed from CAT5e cable, terminated at both ends. Every
rate runs the full functional test in both master directions, then splits the
timed stress equally between the two directions. The table counts timed stress
queries only; functional counts are in the companion JSON.

| Baud | Active minutes | Forwarded queries | Simultaneous USB ADC/RS485 rounds | Median / max query latency (ms) | Result |
| ---: | ---: | ---: | ---: | ---: | --- |
| 115,200 | 15 | 120,765 | 3,239 | 5.287 / 22.956 | PASS |
| 230,400 | 5 | 64,365 | 1,728 | 3.385 / 12.932 | PASS |
| 250,000 | 10 | 134,656 | 3,613 | 3.237 / 51.231 | PASS |
| 460,800 | 5 | 91,483 | 2,456 | 2.426 / 7.975 | PASS |
| 1,000,000 | 5 | 120,316 | 3,230 | 1.852 / 6.471 | PASS |

The final normal 57,600-baud run passed five active minutes with
**22,718 forwarded queries**. Across the passing
functional and timed runs, **556,873 forwarded queries**
completed. Latencies are host-observed round trips for a mixed workload, not a
bus-only propagation or worst-case specification.

Stress includes all six channel query families, real ADC reads, maximum-length
forwarding payloads, exact configuration/calibration checks, ordered bursts,
simultaneous USB measurement/configuration reads on the peer, USB backpressure,
DTR recovery, missing-peer timeout/recovery, address 31, configuration setters,
and non-default settings as reset/wrong-action canaries. It uses one master at
a time. No unexpected reset or canary change was detected in the passing runs.

## Fault injection and native regressions

Both master directions passed temporary, USB-only diagnostic tests at 1 Mbaud:

- A 100 ms CPU interrupt blackout while a complete maximum-length query arrived.
  DMA received 197/200 bytes during the blocked intervals and the query completed.
- Deliberately disabling UART RX DMA during another blackout produced a real
  hardware overrun. USB stayed responsive, the damaged transaction timed out,
  and normal communication returned after discarding the incomplete line.
- Four padded setter frames sent while the peer CPU was blocked exceeded the
  DMA buffer. The driver discarded uncertain data, recovered, and retained the
  original ADC configuration. The setters requested the existing gain and did
  not request calibration or flash writes.

The first overflow test assertion incorrectly expected an absolute byte count
after multiple coalesced wraps. That assertion was corrected to use the observed
discard and known transmitted frames; the earlier attempt is preserved separately.
After a real overrun/overflow the first resynchronization query may time out;
this is intentional fail-closed behavior, not a retry or duplicate execution.

Native tests exercise DMA order/wrap, pending completion accounting, missed-wrap
overflow, a DMA copy overtaken by new input, transfer errors, all UART error bits
including ORE without RXNE, 32-bit counter wrap, transmit timeout bounds and DE
guard timing. Existing SCPI/application/storage/USB tests and 31 Python tests pass.
Diagnostic commands are absent from normal firmware; final hardware checks
verified the service query is undefined and left empty error queues.

## Final state and scope

Both boards have address 0, gain 1, 15 SPS and continuous mode on all four channels.
Their calibration readbacks match the original snapshot, including unit 0001's
existing calibration. Final real input readings in board-input volts were:

| Serial | Input 1 (V) | Input 2 (V) | Input 3 (V) | Input 4 (V) |
| --- | ---: | ---: | ---: | ---: |
| 0003 | 0.0000 | 0.0000 | 0.0000 | 0.0004 |
| 0001 | -0.0013 | 0.0028 | -0.0015 | 0.0003 |

No known voltage reference was connected for qualification, so these readings
do not establish absolute accuracy. Software still addresses 32 boards/128
channels; this two-node test does not qualify that electrical load, longer
cables, stubs, common-mode differences or injected electrical noise. A passing
bench rate is not a universal cable-length or installation rating.

Normal image: **43,212 bytes**, SHA-256 `c9c42bf7b2155a3d37abc01f160a200c70a11ca757ffbba8ab7c0744be9276e1`.
RAM reported by the linker is 8,696 bytes including reserved heap/stack. No new
SCPI baud command was added. Choosing a different production rate requires
building every bus board for the same rate.

The [machine-readable record](rs485-high-baud-fix-2026-11-22.json) contains each
rate's counts, timings, hashes, fault records and final snapshots. Raw evidence,
isolated A/B diagnostic source, binaries, upload records, regression logs and
sampled stress traces are in `../Validation/2026-11-22_RS485_High_Baud_Fix/`
relative to the firmware repository. A trace digest covers generated records;
not every checked burst reply is individually recorded, and this is not a wire
capture. No calibration, saved-address or serial writes were requested. Durable
request-ID allocation continues to use its normal journal.
