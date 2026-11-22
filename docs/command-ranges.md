# Command and calibration ranges (V2.1.2)

Gain tolerance and offset limits below were specified by the user: nominal gain
±10% and offset ±0.25 V. Offset uses the existing raw ADC voltage convention.

Calibration requests are validated before RAM changes or forwarding. The same
calculation/validation is used by the protocol parser and the board calibration
API. Rejection returns `ERROR:INVALID ARGUMENTS` and retains the previous values.

## Calibration limits

| Quantity | Inclusive limits |
| --- | --- |
| Actual/reference points | 0 to 5 V at the board input |
| Measured points | -2.048 to +2.048 raw ADC V, the widest ADC span |
| Separation of measured points | At least 0.001 raw ADC V |
| Calculated scale coefficient | Nominal 2.818181818 multiplied by 0.9 to 1.1; approximately 2.5363636 to 3.1 |
| Calculated offset | -0.25 to +0.25 raw ADC V |

All four points and both coefficients must be finite. The scale must be positive;
reversing both endpoint orders is supported, while a negative slope is rejected.
Coefficients are published only after every check succeeds.

The coefficient/offset/span limits are broad firmware guard bands, not component
tolerances or accuracy claims. They are centralized in `CalibrationLimits` in
`A2D_Calibration.h`. Measured points are raw ADC volts after PGA normalization,
before board scaling, rather than calibrated board volts. This is separate from
the selectable hardware PGA gain of 1, 2, 4 or 8.

The board's specified input is 0–5 V, confirmed by the supplied schematic and
[product specification](https://a2delectronics.ca/shop/battery-testing/4-channel-isolated-adc/).
The ADC differential conversion span is ±2.048 V/PGA, documented in the
[MCP3425 datasheet](https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/DataSheets/22072b.pdf).
The allowed raw calibration-point span covers all PGA settings; it does not
change the board's electrical ratings or permit negative terminal inputs.

## Stored values

Loading calibration coefficients also applies the coefficient limits. An older,
structurally valid record containing finite but out-of-range coefficients is
repaired one channel at a time in RAM: invalid channels use nominal defaults,
valid channels retain their stored coefficients. One atomic journal commit then
persists the repaired calibration. The address and request-ID high-water mark
are retained; the journal sequence advances normally. A failed repair reports storage/reset failure and
leaves the prior committed record available for retry.

Every settings write checks all calibration coefficients before erasing flash.
The record layout and CRC are unchanged. Calibration repair does not initialize
or erase a deployed request-ID namespace.

## Other commands reviewed

Existing argument-count, integer-overflow and query/setter checks already cover
these ranges; no new command bounds were needed outside calibration.

| Command/field | Accepted values |
| --- | --- |
| `INSTR:LED` | 0 or 1 |
| `INSTR:GAIN` | 1, 2, 4 or 8 |
| `INSTR:DR` | 15, 60 or 240 SPS |
| `INSTR:MODE` | 0 or 1 |
| `INSTR:RS485:SET` | Address 0 through 31 |
| Measurement/calibration queries, calibration reset/save, configuration channel | 0 through 128, with existing group/local/chain rules |
| `CAL:VOLT` channel | 1 through 128; channel 0 is rejected |
| RS485 frame address | 0 through 31; foreign addresses ignored |
| RS485 request ID | 1 through 4294967295; zero/overflow rejected |
| `*IDN?`, `*RST`, `*CLS`, `*TRG`, address query/save | No arguments |
| Input frame | At most 199 bytes before LF, including optional CR |
| Forwarded command payload | At most 185 bytes for addresses 0–9; 184 for addresses 10–31 |

Negative, fractional or overflowing integer arguments and extra arguments are
rejected. Unsupported setter-query forms are rejected before dispatch. The
existing channel 0/source semantics and case-sensitive command names remain.

## Verification

Host tests include exact/adjacent float boundaries, invalid-request ownership,
no mutation/forwarding on rejection, valid command endpoints, save/load behavior,
and interrupted repair at every flash operation without request-ID rollback.
On 0003, older-policy records with an extreme coefficient and then a finite gain
of 3.2 were upgraded: the affected channel became nominal, another channel's valid
calibration and address 7 were preserved, and forwarding still allocated an ID and
reached the missing-peer timeout. All 164 physical command-range cases with the
final limits pass, including save/reload following a rejected update. Another
5,000 malformed numeric calibration attempts were rejected. Nominal saved
calibration/address 0 were restored afterward.

The final build also passed 65 host groups/checks, 20,000 malformed USB frames,
a ten-minute mixed-traffic soak, two repeat hub uploads, persistence checks and
actual ADC measurements. See `command-ranges-2026-11-21.json` for the recorded
results and evidence paths.

Measurements use unconnected inputs; these tests do not establish accuracy.
The reusable SCPI library and the deferred command/interface improvements are
unchanged.
