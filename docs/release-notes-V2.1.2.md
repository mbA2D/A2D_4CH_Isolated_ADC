# V2.1.2 release candidate

**Release on hold:** subsequent hardware testing found a fragmented-input
bootloader reset and commands surviving serial session changes. See
`docs/adversarial-retest-2026-11-21.md`. The passing qualification below predates
those counterexamples; the candidate should be repaired before publication.

User-specified calibration limits are applied and the final qualification pass
is complete on unit 0003 through its original USB hub.

This candidate includes the V2.1.1 reset-token/numeric-output fixes and adds
calibration input and coefficient range checks. Invalid calibration is rejected
before changing RAM or forwarding a remote command, preserving prior values.

Limits and the complete command audit are in `docs/command-ranges.md`: reference
points 0–5 V, raw points within ±2.048 V, raw separation at least 1 mV, positive
scale within the requested ±10% of nominal 2.818181818 and raw offset within the
requested ±0.25 V. Actual measurement accuracy still needs
known reference inputs.

Stored coefficient loading and every save use the same coefficient limits.
Older structurally valid records with an out-of-range finite calibration are
repaired to nominal only on affected channels, retaining valid channels, the
address and the durable request-ID high-water mark. Repair uses the existing
atomic journal and reports a failure if it cannot commit. The storage layout is
unchanged. Other command arguments already had appropriate range checks.

The reusable SCPI library remains 1.0.0 and the proposals in
[issue #1](https://github.com/mbA2D/A2D_4CH_Isolated_ADC/issues/1) remain deferred.

## Qualification

65 host groups/checks and 164 physical command-range checks pass. The real upgrade
from an intentionally invalid saved calibration on 0003 preserved the valid
channel, address and forwarding operation. USB and ST-Link builds pass; ST-Link
is build-only.

The final image passed 250,000 host parser cases, 78,394 invalid USB dispatches,
100,000 malformed/foreign RS485 frames on the host, 20,000 malformed USB frames
on the board and 5,000 malformed numeric calibration attempts. The 600-second
hardware soak completed 11,415 configuration cycles and 22,830 measurement
vectors, discarded 457 oversized 64 KiB frames and recovered from 12 unread-reply
floods without losing the volatile calibration/address canaries.

Two repeat uploads through the hub used the identical qualified binary and
preserved saved calibration/address across MCU resets. The upgrade that repaired
the older-policy record also passed through the hub. Final checks include 23
persistence checks, 34 USB/command smoke checks and 10 measurement checks, with 60
samples per unconnected input. Nominal calibration/address 0 were restored and
verified after testing. Evidence is summarized in
`docs/command-ranges-2026-11-21.json` and the candidate manifest.

The binary is 54,032 bytes of 55,296 allowed; static RAM remains 6,380 of 20,480 bytes.
The development-unit image is for serial 0003. Build with
the intended unit identity and use the guarded USB updater for other units.
The qualified unit remains on its original USB hub; unit 0001 is excluded.

No reference-voltage accuracy, physical RS485 peer or physical power-cut fixture
was available. V1-to-V2 migration was not repeated. This is an uncommitted release
candidate, not a published/tagged release.
