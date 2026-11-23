# Configuration queries — V3.2.0

Three read-only commands are added to the existing C firmware:

| Command | Returned integer(s) |
| --- | --- |
| `INSTR:GAIN? <channel>` | PGA gain: 1, 2, 4 or 8 |
| `INSTR:DR? <channel>` | Conversion rate: 15, 60 or 240 samples/second |
| `INSTR:MODE? <channel>` | 0=single-shot, 1=continuous |

The question mark belongs to the header. The channel argument is required.
For example, `INSTR:GAIN? 1` reads channel 1. Existing setters retain their
value/channel form, for example `INSTR:GAIN 4,1`; `INSTR:GAIN 4,1?` remains
invalid and does not change the setting. No alternate query syntax is added.

## Channels and replies

Existing local/chain addressing rules apply. On the USB base (address 0), channels
1..4 are local and 5..128 are forwarded to the corresponding RS485 peer.
USB channel 0 queries the four local ADCs and returns CSV in physical channel
order. The base's channel 0 response does not aggregate the whole chain.
Direct USB access retains the existing local/global channel mapping.

RS485 queries use the current address/request-ID envelope and the addressed
board's global channels. Channel0 is silent on RS485, matching the existing
group-read rule. For example, an address 1 peer can receive
`1 123 INSTR:GAIN? 5` and reply `1 123 4` when its first channel has gain 4.
Forwarded queries use the same matching-ID, ordering and timeout logic as
measurement queries; no setter acknowledgements or new frame format are added.

After reset, defaults are gain 1, 15 SPS, continuous:

```text
INSTR:GAIN? 1
1
INSTR:DR? 0
15,15,15,15
INSTR:MODE? 0
1,1,1,1
```

`*TRG` switches the existing mode shadow and hardware to single-shot, so
`INSTR:MODE? 0` then returns `0,0,0,0`. `*RST` restores the defaults above.
Queries return the current runtime configuration; these settings are not made
persistent by querying them.

## Readback and failure behavior

Each selected channel reads the ADC's three-byte I2C response and confirms its
configuration bits against the last acknowledged configuration. Conversion
readiness is ignored for these queries: a conversion need not finish just to
read its settings. The query does not write ADC registers, start a conversion,
wait for a sample, save calibration/address, or change serial identity.

An unconfigured channel, failed/short I2C read, unsupported resolution, or
configuration mismatch returns `ERROR:ADC CONFIG`. A group query validates all
four selected results before emitting any values; it returns one error rather
than partial CSV if any channel fails. Invalid arguments retain the existing
`ERROR:INVALID ARGUMENTS` response and cannot execute a setter or forward.

## Qualification

The target remains C11/HAL/CMake with the existing Maple loader and unchanged
60-byte settings/serial journal. Version is V3.2.0; no re-provisioning or
recalibration is required for this extension. The common application is 43364
bytes, 344 bytes above V3.1.0, leaving 11932 of the 55296-byte allowance. Link RAM
allocation remains 8664 bytes including the existing heap/stack reservations.

Host regressions pass: 44 application groups, 17 service groups, 14 portable SCPI
groups, 3 CDC groups, 4 deferred-reset checks, 16 numeric-formatting cases, the
copied standalone C package example and 21 Python tests. Added tests cover new
syntax/ranges, all channel combinations, read-only behavior, pending ADC
conversions, failed/short reads and mismatched configurations, complete group
errors, addressed peer replies, forwarding and timeout behavior.

On 0003 (USB UID 4977386D3659, COM10, hub 1-10.4.1.1.1.4), 493 hardware checks passed
across all gain/rate/mode combinations, mixed per-channel values, group/scalar
queries, malformed requests, fragmented input, missing-peer timeouts, trigger
and reset. Another 280 USB packet/coalescing checks passed. Calibration/address
and serial 0003 were checked preserved. No physical second RS485 peer was
connected; peer framing/routing is covered by host tests. Unit 0001 was untouched.

Raw evidence is in `../Validation/2026-11-22_Configuration_Queries/`; the summary
is [configuration-query-tests-2026-11-22.json](configuration-query-tests-2026-11-22.json).
V3.2.0 remains an unreleased development build. Other deferred improvements,
including error queues, status/capability queries and RS485 acknowledgements,
are outside this change.
