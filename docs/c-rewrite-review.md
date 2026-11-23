# C rewrite: module and system review

Six sub-agents reviewed the frozen V3.0.0 baseline and the V3.1.0 C modules.
The parent inspected their evidence, reviewed the findings, implemented fixes,
and performed the physical tests. Reviewers used source and isolated host
models; none opened COM/USB devices or uploaded firmware. Full reports are in
`../Validation/2026-11-22_C_Rewrite/reviews/{pre-c,post-c}/`.

## Coverage and final assessment

| Reviewer | Modules reviewed separately | Post-port result |
| --- | --- | --- |
| Zeno | Calibration, Measurement, board facade, platform contracts | No C-port defects; invalid-channel fix verified; 40,020 calibration and 344,064 signed-code comparisons |
| Huygens | SettingsStore and Flash | No C-port regressions; 15 service groups include power cuts, migration, mirror corruption, serial and first-boot limitation |
| Parfit | Reusable SCPI parser/reader, device Protocol, numeric I/O | No unresolved findings; 1,199,222 protocol comparisons and 5,172,993 formatting cases; midpoint fix verified |
| Goodall | Transport, Dispatcher, Application, deferred USB reset | 40 application groups; ordered pending output, fairness, bounded drain and failed-transmit regressions; injected regressions detected |
| Kierkegaard | HAL main/USB/UART/I2C, descriptors/low-level glue/syscalls, composition, CMake/toolchain/linker/startup | No concrete functional defects in the target C integration |
| Popper | Full command-to-hardware-to-reply path, initialization, stored state, sessions, update/provision tools | No new actionable integration defects; 13 real-module C interaction groups and independent ARM build passed |

Zeno additionally reviewed the C SCPI library independently of its port author,
including caller capacities, callback contracts, ownership and isolated-package
compilation. No findings. Parfit's C library port preserved the original 10 test
groups and added four C API/compatibility groups. Differential comparisons of
30,000 parser inputs, 30,000 numeric pairs, 200,000 framing bytes and 2,000 reader
tapes found no grammar/framing differences.

These counts are overlapping evidence, not a sum of unique independent tests.
The ARM build independently reproduced the parent's exact 43020-byte binary
(SHA256 8126f64ad2c479d1f2bce1a67e65f0a812c8234c061fbe3705dc091502085d00).

## Findings and parent disposition

| Finding | Parent assessment and disposition |
| --- | --- |
| Invalid calibrated API channel returned 0 V | Confirmed; return NaN, matching raw-read failures. Wire parser already excluded these indices; direct API is now consistent. |
| Midpoint decimal output could round low | Confirmed; one scaled integer rounding step with explicit half-away-from-zero ties; target positive/negative cases pass. |
| USB enqueue deadline silently lost responses | Confirmed; retain one bounded complete response, pause further USB dispatch, preserve order and propagate backpressure. Session termination still discards old traffic intentionally. |
| Complete USB requests starved RS485 | Confirmed; alternate preferred source after handled frames and continue RS485 while a USB response is pending. |
| Forwarded RS485 transmission failure was ignored | Confirmed; check complete writes, restore receive mode, return an immediate USB error, and never retry potentially executed mutations automatically. Replies use one bounded frame and return status. |
| Pre-forward RS485 drain had no work bound | Confirmed; cap to 512 reads and return a busy error before issuing/transmitting a new transaction. |
| USB updater could use a stale physical path | Confirmed; compare fresh/current descriptor against the initial path before settings reload or reset; mocked moved-device case invokes neither reset nor writer. |
| Updater compared RAM edits with post-reset stored settings | Confirmed; separately record live state, explicitly reload persisted state on the verified connection, and compare that baseline after upload. No implicit SAVE. |
| Forwarded setter errors cannot be correlated without acknowledgements | Confirmed protocol limitation, deliberately deferred in issue #1. Locally known UART failure is now reported; successful forwarding still does not prove remote execution. Adding success/error completions would change the RS485 protocol and is separate work. |
| Interrupted first-ever initialization leaves an unknown ID namespace | Confirmed availability limitation, deliberately fail-closed. Automatic ID restart could reuse prior IDs. A known virgin unit needs controlled manufacturing/service recovery; provisioning alone does not certify allocation. Manufacturing must test forwarding/allocation before field release. Ordinary reset/provisioning does not repair the namespace. |

Eight fixes are implemented; the final two rows remain explicit limitations.
No new SCPI aliases/getters, error queue, RS485 CRC/acknowledgement protocol,
discovery or calibration export interfaces were introduced.

## Limits

Host models do not establish physical UART turnaround, line-noise behavior,
real brownout/partial erase outcomes, absolute ADC accuracy, IRQ timing or
maximum stack usage. Physical qualification is exclusively on 0003 through its
original hub with inputs unconnected and RS485 disconnected. Unit 0001 is
excluded. Actual device counts/results are in the qualification JSON and C
rewrite report; they are not attributed to the sub-agents' host reviews.
