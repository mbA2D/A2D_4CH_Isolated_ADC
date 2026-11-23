# A2D 4CH Isolated ADC — User and Programming Manual

**Firmware: V5.0.1 · Document updated: 2026-11-23**

This manual describes all 23 commands in the V5.0.1 firmware. Read `*IDN?` to
check the version installed on your device before using these examples. This
manual does not describe the older published V2.1.3 release.

The interface uses a limited SCPI-like command set with exact, case-sensitive
names. This document is the current programming reference. Older spec sheets
contain obsolete query syntax and calibration argument order.

## Contents

- [Getting started](#getting-started)
- [Command syntax and replies](#command-syntax-and-replies)
- [Channels and RS485 addressing](#channels-and-rs485-addressing)
- [Complete command list](#complete-command-list)
- [Identification, reset and trigger](#identification-reset-and-trigger)
- [Measurements and ADC settings](#measurements-and-adc-settings)
- [Calibration](#calibration)
- [Address, serial number and firmware updates](#address-serial-number-and-firmware-updates)
- [Errors](#errors)
- [Settings and persistence](#settings-and-persistence)
- [Python example](#python-example)
- [Troubleshooting and compatibility](#troubleshooting-and-compatibility)

## Getting started

Connect the device by USB and open its virtual COM port. Use 115200 baud,
8 data bits, no parity, one stop bit and no hardware flow control. USB CDC
stores the selected line settings without using them as a physical UART baud
rate. The separate RS485 UART runs at **230400 baud, 8N1**. All boards on the
bus must use the same baud rate. V5.0.0 used 57600 baud; update all bus peers
when moving to this release.

Assert **DTR** while communicating. Send ASCII commands terminated by LF
(`\n`); CRLF (`\r\n`) is also accepted. Start with a two-second host read
timeout. Close other programs that may have the port open.

Send these queries individually and read one reply after each:

```scpi
*IDN?
INSTR:RS485?
INSTR:GAIN? 0
MEAS:VOLT? 0
SYST:ERR?
```

Illustrative replies, in the same order:

```text
A2D Electronics,4CH Isolated ADC,0123,V5.0.1
0
1,1,1,1
1.2345,2.3456,3.4567,4.5678
0,"No error"
```

The serial number and readings depend on the connected unit. Channel 0 readings
contain the four local inputs, in physical channel order. Unconnected inputs
are not voltage references and should not be used to establish accuracy.

## Command syntax and replies

- Command names must match the spelling and case shown here. Abbreviations,
  lowercase names and an optional leading colon are not supported.
- Put `?` at the end of the query **header**, before its parameters:
  `MEAS:VOLT? 1`. `MEAS:VOLT 1?` and `MEAS:VOLT 1` are invalid.
- Separate the header from parameters with whitespace. Separate multiple
  parameters with commas: `INSTR:GAIN 4,1`.
- Spaces and tabs around commas are allowed: `INSTR:GAIN 4 , 1`. Missing commas,
  repeated commas, empty fields and leading/trailing commas are invalid.
- Send **one command per line**. Any semicolon rejects the entire line, so
  `INSTR:GAIN 4,1;*IDN?` executes neither command. Separate LF-terminated lines
  may be transmitted together and are processed in order.
- Angle brackets in syntax descriptions mark required parameters; do not send
  the brackets. There are no optional parameters in the command list.
- Integer parameters use decimal digits, with an optional leading `+` except
  for the serial number. Negative values, fractions and hexadecimal integer
  notation are rejected. Calibration points accept finite real numbers,
  including decimal exponents such as `1.25e-3`. Use `.` as the decimal point
  and do not append unit suffixes. The real-number converter also accepts
  C-style hexadecimal floats; decimal notation is recommended for clients.
- A successful query returns one CRLF-terminated line. Lists use commas without
  spaces. Voltage replies have four decimal places; calibration coefficients
  have five; configuration replies are decimal integers. Displayed digits do
  not imply measurement accuracy.
- **Setters and other non-query commands produce no reply**, including no `OK`.
  Failed commands and failed queries queue an error and also produce no reply.
  Read only after a query. On a query timeout, retrieve the cause with
  `SYST:ERR?`; do not substitute zero for a missing measurement.
- Blank lines are ignored. A line may contain at most 199 bytes before LF,
  including an optional CR. Oversized lines and lines containing NUL are
  discarded through LF. Forwarded commands have a smaller payload limit
  because the RS485 envelope shares the 200-byte frame buffer; keep commands short.

The firmware does not implement compound commands, quoted input strings,
binary blocks, SCPI channel lists such as `(@1:4)`, or full SCPI/IEEE 488.2 status
reporting. Quoted descriptions in error **replies** are supported.

## Channels and RS485 addressing

Each board has four physical inputs and an address `A` from 0 to 31. Its global
channels are `4*A+1` through `4*A+4`:

| Board address | Global channels |
| --- | --- |
| 0 | 1–4 |
| 1 | 5–8 |
| 2 | 9–12 |
| 31 | 125–128 |

The software address space supports 32 boards including the master, or 128 input
channels. This is an addressing limit, not a qualification of every 32-board
physical installation. The hardware tests so far use two boards. Cable length,
topology, termination, electrical loading and baud rate must be qualified for
the intended installation.

Use one main bus cable with short connections to each board. Enable termination
only at the two physical ends of that cable; disable it on intermediate boards.
Use one master at a time. Idle peers do not add a protocol reply delay to a
query for another board, but polling all inputs takes longer as boards are added.
Each missing-peer query can add the approximately one-second reply timeout.

Through USB, channels 1–4 always select that USB-connected board's physical
inputs. Its own global channel numbers also select those inputs. Channel 0
means the local group of four, not every board on the chain.

Only an address 0 board forwards USB channel commands to other boards. For
example, `MEAS:VOLT? 5` sent to that master queries address 1, physical input 1.
A USB-connected board with a nonzero address does not forward commands for
other boards. Commands without a channel parameter always act on the board
receiving them; they do not propagate to the chain.

Direct RS485 access uses global channels owned by the addressed board. Local
USB channel aliases 1–4 do not apply to an address 1 peer; use 5–8 instead.

| Command family | Channel 0 through USB | Channel 0 through addressed RS485 |
| --- | --- | --- |
| Voltage, raw-voltage and configuration queries | Four local values | No reply; no group read |
| `CAL?` | Four offset/gain pairs | No reply; no group read |
| `INSTR:GAIN`, `INSTR:DR`, `INSTR:MODE` | Set all four local inputs | Set all four local inputs |
| `CAL:RESET`, `CAL:SAVE` | Act on all four local inputs | Ignored |
| `CAL:VOLT` | Rejected; select one input | Rejected; select one input |

### Direct RS485 frames

RS485 requests and query replies carry an address and request ID:

```text
<address> <request_id> <command>
<address> <request_id> <reply>
```

Use one ASCII space between the envelope fields. The address is 0–31 and the
request ID is 1–4294967295; both use unsigned decimal digits. Each frame ends
with LF; replies end with CRLF. Example for address 1's first input:

```text
Request: 1 123 MEAS:VOLT? 5
Reply:   1 123 1.2345
```

`Request:` and `Reply:` are labels, not transmitted text. Query responses echo
both fields; foreign addresses and malformed envelopes are silently discarded.
There is no broadcast address or setter acknowledgement. A direct controller
should avoid reusing request IDs while stale replies might still exist. The
USB master's forwarding implementation manages its own durable request IDs.

A forwarded query waits up to about one second for a complete reply with the
matching address and ID. On timeout, the master queues error 1101 and sends no
measurement reply. A forwarded setter has no remote completion acknowledgement:
successful transmission does not prove execution. Query the resulting setting
when verification is required. No RS485 CRC is implemented.

## Complete command list

All arguments shown are required. A dash in the reply column means no reply.
Channel arguments follow the rules above; `ch` is 0–128 except where noted.

| Command | Parameters / meaning | Successful reply |
| --- | --- | --- |
| `*IDN?` | Read manufacturer, model, serial and firmware version | Four comma-separated fields |
| `*RST` | Reload stored calibration/address; reset ADC settings and LED | — |
| `*CLS` | Clear this board's error queue | — |
| `*TRG` | Trigger all four local ADCs and select single-shot mode | — |
| `SYST:ERR?` | Remove the oldest error | `code,"message"` |
| `INSTR:LED <state>` | `state`: 0=off,1=on | — |
| `MEAS:VOLT? <ch>` | Read calibrated board-input voltage | One or four voltages |
| `MEAS:VOLT:ADC? <ch>` | Read raw ADC-input voltage | One or four voltages |
| `INSTR:GAIN <gain>,<ch>` | `gain`: 1, 2, 4 or 8 | — |
| `INSTR:GAIN? <ch>` | Read confirmed ADC gain | One or four integers |
| `INSTR:DR <rate>,<ch>` | `rate`: 15, 60 or 240 samples/second | — |
| `INSTR:DR? <ch>` | Read confirmed conversion rate | One or four integers |
| `INSTR:MODE <mode>,<ch>` | `mode`: 0=single-shot,1=continuous | — |
| `INSTR:MODE? <ch>` | Read confirmed conversion mode | One or four integers |
| `CAL:VOLT <ch>,<actual1>,<measured1>,<actual2>,<measured2>` | Apply two-point calibration in RAM; `ch`: 1–128 | — |
| `CAL? <ch>` | Read calibration coefficients | `offset,gain`, or four such pairs |
| `CAL:RESET <ch>` | Restore nominal calibration in RAM | — |
| `CAL:SAVE <ch>` | Save selected calibration to flash | — |
| `INSTR:RS485:SET <address>` | Set this board's live address: 0–31 | — |
| `INSTR:RS485?` | Read this board's live address | One integer |
| `INSTR:RS485:SAVE` | Save this board's live address to flash | — |
| `SYST:SER:SET <serial>` | Provision exactly four digits; USB only | — |
| `SYST:BOOT` | Enter the existing USB bootloader; USB only | — |

## Identification, reset and trigger

### `*IDN?`

Returns `A2D Electronics,4CH Isolated ADC,<serial>,V5.0.1`. A provisioned serial
has four digits, including leading zeros; an unprovisioned unit reports
`UNASSIGNED`. This command does not change settings or clear errors.

### `*RST`

This is a settings reload, not an MCU reboot. It turns the LED off, restores
gain 1 / 15 SPS / continuous conversion on every input, and reloads saved
calibration and the saved RS485 address. Unsaved calibration/address changes
are discarded. Serial identity and queued errors are preserved. A reload or
ADC-reset failure queues error 1004. Read `SYST:ERR?` to check for failure.

### `*CLS`

Clears the entire local error queue. It does not reset measurements, calibration,
address, serial or ADC configuration. Existing errors are lost when cleared;
read them first if they are needed for diagnosis.

### `*TRG`

Starts a single-shot conversion on all four local ADCs. It also changes every
ADC's mode to 0, including inputs previously in continuous mode. It does not
return readings or wait for all conversions to finish before accepting the
next command. A measurement query waits for conversion readiness within the
firmware's read timeout. A trigger failure queues error 1002.

Example, with no replies expected from the first two lines:

```scpi
INSTR:GAIN 1,0
*TRG
MEAS:VOLT? 0
```

Use `INSTR:MODE 1,0` to resume continuous conversion. There is no channel
parameter for `*TRG` and no chain-wide trigger command.

### `INSTR:LED <state>`

Sets the local indicator LED: 0=off or 1=on. The setting is temporary and `*RST`
turns it off. There is no LED query command.

## Measurements and ADC settings

### `MEAS:VOLT? <ch>` and `MEAS:VOLT:ADC? <ch>`

`MEAS:VOLT?` returns the calibrated voltage at the board input. `MEAS:VOLT:ADC?`
returns the voltage at the ADC input, with PGA gain accounted for, before
divider/calibration correction. Both report volts to four decimal places.

```text
Command: MEAS:VOLT? 1
Reply:   1.2345

Command: MEAS:VOLT:ADC? 0
Reply:   0.4381,0.8323,1.2266,1.6208
```

The labels are not transmitted. Group readings are taken sequentially from
the four ADCs; they are not a guaranteed simultaneous sample. A measurement
query does not itself trigger a new single-shot conversion. Use `*TRG` when
a new single-shot acquisition is required.

The firmware waits up to about 120ms per selected ADC for a ready conversion.
An ADC/I2C failure or configuration mismatch queues 1001, with no partial group
reply. Numerical output outside the formatter's supported range queues -222.
Calibration point limits are not clamps on measurement results; negative
readings can be returned. Electrical input ratings are separate from the
calibration validation bounds below.

### `INSTR:GAIN <gain>,<ch>` and `INSTR:GAIN? <ch>`

Select or read ADC PGA gain 1, 2, 4 or 8. This is distinct from the calibration
scale coefficient returned by `CAL?`. Higher PGA gain reduces the ADC input
span. The default after power-up or `*RST` is 1. The setting is not saved.

```scpi
INSTR:GAIN 4,1
INSTR:GAIN? 1
```

The query returns `4` after a successful setting. `INSTR:GAIN? 0` returns four
values, for example `4,1,1,1` over USB.

### `INSTR:DR <rate>,<ch>` and `INSTR:DR? <ch>`

Select or read 15, 60 or 240 samples/second. These are ADC conversion rates,
not guaranteed USB transaction rates. The default is 15. The setting is not saved.

```scpi
INSTR:DR 60,0
INSTR:DR? 0
```

The USB query returns `60,60,60,60` after all four settings succeed.

### `INSTR:MODE <mode>,<ch>` and `INSTR:MODE? <ch>`

Select or read 0=single-shot or 1=continuous conversion. The default is 1. The
setting is not saved. Writing ADC configuration can start a conversion; use
`*TRG` to explicitly request another single-shot acquisition.

```scpi
INSTR:MODE 0,0
*TRG
MEAS:VOLT? 0
INSTR:MODE? 0
INSTR:MODE 1,0
```

The mode query returns `0,0,0,0`; the last setter resumes continuous conversion.

Configuration queries check actual ADC readback against the configuration
known to the firmware. They do not trigger conversions or wait for conversion
readiness. A failed readback or a failed configuration write queues 1002.
Group writes are attempted on every selected input: an I2C failure can leave
some inputs changed and others unchanged. Query the group after recovering
from the failure; group setters are not a rollback transaction.

## Calibration

### `CAL:VOLT <ch>,<actual1>,<measured1>,<actual2>,<measured2>`

Applies one input's two-point calibration immediately in RAM. The parameter
order is **channel, actual1, measured1, actual2, measured2**.

- `actual1`, `actual2`: known reference voltages at the board input, in volts.
- `measured1`, `measured2`: corresponding uncalibrated ADC-input readings from
  `MEAS:VOLT:ADC?`, in volts.
- `ch`: one input,1–128 under the addressing rules;0 is invalid.

The coefficients implement:

```text
gain   = (actual2 - actual1) / (measured2 - measured1)
offset = measured2 - actual2 / gain
board_input_volts = (raw_adc_volts - offset) * gain
```

The offset is in **raw ADC volts**, not board-input volts. Both points may be
supplied in either order, provided the calculated coefficients pass validation.

| Validation item | Inclusive limit |
| --- | --- |
| Each actual reference point | 0–5 V at the board input |
| Each measured point | -2.048 to +2.048 V at the ADC input |
| Measured-point separation | At least 0.001 V in magnitude |
| Actual-point separation | Nonzero |
| Calculated scale coefficient | Nominal 2.818181818 ±10%; approximately 2.536364–3.100000 |
| Calculated offset | -0.25 to +0.25 raw ADC volts |

All points and coefficients must be finite. Limits are evaluated using
single-precision arithmetic. A rejected calibration leaves the previous
coefficients unchanged. Nonfinite/conversion-invalid points queue -128;
out-of-range points or coefficients queue -222.

Illustrative calculation for input 1:

```scpi
CAL:VOLT 1,0,0,2.75,1
CAL? 1
SYST:ERR?
```

Expected query replies are `0.00000,2.75000` and `0,"No error"`, assuming the
queue was empty beforehand. These numbers are a syntax example, not reference
measurements for your board. For real calibration, acquire the two measured
points from the actual device while applying known reference voltages.

The setting is temporary until `CAL:SAVE` is sent.

### `CAL? <ch>`

Returns **offset first, gain second**, each to five decimal places. Example
nominal response for one input: `0.00000,2.81818`.

Over USB, `CAL? 0` returns eight comma-separated values:

```text
offset1,gain1,offset2,gain2,offset3,gain3,offset4,gain4
```

This is readback, not a direct coefficient-setting interface. The rounded text
does not preserve the full precision of coefficients stored internally.

### `CAL:RESET <ch>`

Restores nominal scale 2.818181818 and offset 0 in RAM for the selected input,
or all local inputs with USB channel 0. It neither saves the new values nor
erases serial identity. To discard this temporary reset, use `*RST`; to make
the nominal calibration persistent, follow with `CAL:SAVE`.

### `CAL:SAVE <ch>`

Saves the selected input's current calibration to flash, or all four local
inputs with USB channel 0. Other unsaved inputs are not saved by a single-input
save. Saving calibration does not save a changed RS485 address. Storage failure
queues -320; this command sends no acknowledgement.

After applying a calibration, a typical USB verification sequence is:

```scpi
SYST:ERR?
CAL? 1
CAL:SAVE 1
SYST:ERR?
*RST
CAL? 1
SYST:ERR?
```

Read only after query lines. Confirm every error read returns `0,"No error"`
and the coefficients survive the settings reload. If older errors exist, read
them first so they are not confused with the result of this operation.

## Address, serial number and firmware updates

### `INSTR:RS485:SET <address>`

Changes the receiving board's live address to 0–31 immediately. It is temporary
until saved. Through USB, choose address 0 for a board that must forward to a
chain. On direct RS485, subsequent requests must use the new address and its
new global channels. There is no acknowledgement to this setter.

### `INSTR:RS485?`

Returns the receiving board's current live address as one decimal integer.
It does not report every address on a chain.

### `INSTR:RS485:SAVE`

Saves the live address to flash. It does not save unsaved calibration changes.
Storage failure queues -320. Example over USB:

```scpi
INSTR:RS485:SET 1
INSTR:RS485?
INSTR:RS485:SAVE
SYST:ERR?
*RST
INSTR:RS485?
```

Both address queries should return `1`; confirm an empty error queue as well.
Assign distinct addresses to boards sharing an RS485 bus.

### `SYST:SER:SET <serial>` — USB only

Manufacturing command to provision exactly four decimal digits, `0000`–`9999`,
including leading zeros. It writes persistent identity immediately; no save
command is required. An unprovisioned board reports `UNASSIGNED` through `*IDN?`.
Repeating the same serial is permitted. Attempting a different serial on an
already provisioned board queues 1003 and preserves the existing identity.
Use the assigned manufacturing serial; there is no serial-unlock SCPI command.

The setter is silent. Check `SYST:ERR?`, then read the serial through `*IDN?`
and verify it again after `*RST`. The repository's
[`provision_identity.py`](../tools/provision_identity.py) performs these checks.
Calibration operations and normal firmware updates preserve identity. A mass
flash erase can remove it; it is not hardware OTP protection. RS485 use queues -200.

### `SYST:BOOT` — USB only

Requests entry into the installed Maple USB bootloader. It sends no reply;
the application COM port disconnects. Use the supplied USB updater to select
the correct device, transfer the image and verify identity and saved settings
after the application returns. Opening the port, changing baud rate or sending
the old `1EAF` token does not request bootloader entry in this firmware.

RS485 use queues -200 without entering the bootloader. `*RST` also does not
enter the bootloader. Application restart discards unsaved runtime changes.

Follow the [USB update instructions](updating-firmware.md). Use the uploader
provided with the target firmware version; it handles the older query and
provisioning conventions during upgrades. No SWD access is needed for a normal
USB field update with the bootloader already installed.

## Errors

### `SYST:ERR?` and `*CLS`

Every `SYST:ERR?` removes the oldest queued entry and returns `code,"message"`.
When empty it returns `0,"No error"`. Read until zero to drain the queue.
`*CLS` discards all entries without replying.

The queue holds eight errors. Overflow preserves the oldest seven and replaces
the newest occupied slot with `-350,"Queue overflow"`. Further errors while
full retain that marker. Reading frees space for new errors.

There is one queue per board, shared between its USB and addressed RS485 access.
Reading or clearing it through either interface affects that same queue. The
USB master's queue is not an aggregate of the chain. An addressed peer's queue
can be queried directly, for example `1 124 SYST:ERR?` over RS485.

The queue survives USB close/open and `*RST`. It clears on `*CLS` or an MCU
restart. It is not stored in flash. Errors do not generate unsolicited messages.

| Code | Message | Meaning |
| --- | --- | --- |
| 0 | No error | Queue empty |
| -102 | Syntax error | Invalid parameter syntax/count or compound line |
| -113 | Undefined header | Unknown command, including incorrect case |
| -128 | Numeric data error | Unsupported integer syntax or failed finite real conversion |
| -200 | Execution error | USB-only command received over RS485 |
| -222 | Data out of range | Numeric/channel/calibration/output limit rejected |
| -224 | Illegal parameter value | Unsupported gain, rate or mode selection |
| -320 | Storage fault | Save or durable request-ID reservation failed |
| -350 | Queue overflow | More errors than the queue can retain |
| -363 | Input buffer overrun | Invalid/oversized input or forwarding frame limit exceeded |
| -400 | Query error | A complete response cannot fit the reply buffer |
| 1001 | ADC read failed | Measurement acquisition failed |
| 1002 | ADC configuration failed | Configuration read/write or trigger failed |
| 1003 | Identity locked | Attempt to replace an assigned serial |
| 1004 | Settings reload failed | `*RST` could not reload/apply saved settings |
| 1101 | RS485 timeout | Forwarded query received no matching complete reply |
| 1102 | RS485 write failed | A complete request or reply could not be transmitted |
| 1103 | RS485 busy | Stale receive traffic could not be drained within the bound |

Positive codes are product-specific. A peer's failed query records its original
error on that peer; the master can subsequently record a timeout. Remote setter
errors are not automatically collected by the master because setters have no
RS485 acknowledgement protocol.

V5.0.1 does not implement `*ESR?`, `*ESE`, `*STB?`, `*SRE`, `*OPC`, `*OPC?`,
operation/questionable status trees or service-request notifications. A command
not listed in the complete command list is not available.

## Settings and persistence

| Setting / state | Takes effect | Retention / save operation |
| --- | --- | --- |
| ADC gain, rate, mode | On the configuration command | RAM only; power-up and `*RST` restore 1 / 15 SPS / continuous |
| Indicator LED | On the command | RAM only; power-up and `*RST` turn it off |
| Calibration | On `CAL:VOLT` or `CAL:RESET` | RAM until `CAL:SAVE`; saved values load at power-up / `*RST` |
| RS485 address | On `INSTR:RS485:SET` | RAM until `INSTR:RS485:SAVE`; saved value loads at power-up / `*RST` |
| Serial identity | On successful provisioning | Immediately persistent and locked against a different serial |
| Error queue | On a detected command/operation error | RAM; retained across USB sessions and `*RST`, cleared by `*CLS` or MCU restart |

A board without saved settings starts with address 0 and nominal calibration.
When a valid saved record contains calibration outside the current coefficient
limits, affected inputs load nominal calibration and the repaired record is
saved. Check calibration after an older-version migration; see the
[update guide](updating-firmware.md).
USB disconnection/reconnection changes the communication session without
resetting live instrument settings. DTR transitions discard incomplete input,
stale pending replies and outstanding forwarded transactions. Keep DTR asserted
while communicating and deassert it before closing; changing host handles alone
is not a reliable session signal.

## Python example

Install `pyserial` in your Python environment. Replace `COM_TARGET` with the
correct device's port (`/dev/ttyACM...` on Linux). The example queries identity,
configuration, voltages and errors without changing instrument settings.

```python
import serial

with serial.Serial("COM_TARGET", 115200, timeout=2, write_timeout=2) as device:
    device.dtr = True
    try:
        def query(command):
            device.write((command + "\n").encode("ascii"))
            reply = device.readline()
            if not reply:
                raise TimeoutError(
                    f"No response to {command!r}; inspect SYST:ERR? after the timeout"
                )
            if not reply.endswith(b"\n"):
                raise RuntimeError("Partial reply; restore the session before continuing")
            return reply.decode("ascii").rstrip("\r\n")

        identity = query("*IDN?")
        print(identity)
        fields = identity.split(",")
        if fields[:2] != ["A2D Electronics", "4CH Isolated ADC"]:
            raise RuntimeError("Unexpected device")
        if len(fields) != 4 or fields[3] != "V5.0.1":
            raise RuntimeError("Use the manual matching the installed firmware")

        print("Gains:", query("INSTR:GAIN? 0"))
        print("Volts:", [float(v) for v in query("MEAS:VOLT? 0").split(",")])
        for _ in range(9):  # At most eight queued entries, then the empty reply.
            error = query("SYST:ERR?")
            print("Error:", error)
            if int(error.split(",", 1)[0]) == 0:
                break
    finally:
        device.dtr = False
```

For a setter, use `device.write(b"INSTR:GAIN 2,1\n")` without reading an
acknowledgement; follow with `SYST:ERR?` and `INSTR:GAIN? 1` to verify it. On a
failed query, do not immediately retry a mutating command. After a missing-peer
timeout the firmware remains usable; query its error queue for the reason.
If framing is uncertain or a partial reply was received, end/reopen the DTR
session before issuing another query.

## Troubleshooting and compatibility

| Symptom | Check |
| --- | --- |
| No USB reply | Correct port/device, DTR asserted, LF sent, no competing serial monitor, finite host timeout |
| No reply after a setter | Expected; use an explicit query to verify the result |
| No reply after a query | Read `SYST:ERR?`; confirm command spelling, parameters and channel rules |
| Unexpected error when checking a command | Drain older errors first; the queue returns oldest first |
| Channel 0 query silent over RS485 | Select a global channel owned by that peer |
| Configuration differs after reconnect/reset | USB reconnect preserves live settings; `*RST` restores ADC defaults and saved calibration/address |
| Calibration/address changes lost | Send the corresponding save command and check errors before restart |
| Mode unexpectedly 0 | `*TRG` selects single-shot mode on all four ADCs |
| Missing-peer error 1101 | Check peer power, address, RS485 wiring and matching firmware; the master cannot infer why a peer stayed silent |
| Storage error -320 while forwarding | Persistent request-ID allocation may be disabled or a flash operation may have failed. `*RST` does not restart the counter; a known legacy migration can require controlled service recovery. |
| Error 1001 or 1002 | ADC acquisition/configuration failed; inspect the queue and retry only after resolving the underlying condition |
| USB port disappears after `SYST:BOOT` | Expected bootloader transition; use the supplied updater |

V4.0.0 moved measurement/calibration query markers onto headers and required
commas between arguments. V5.0.0 removed immediate error lines and provisioning
`OK` replies and added `SYST:ERR?`. Update host applications and all chain peers
together. Do not use older `MEAS:VOLT 1?`, `CAL 1?`, or channel-last calibration
examples with this firmware.

For maintenance details, see [syntax migration](scpi-syntax.md),
[error handling and qualification](scpi-errors.md), and
[USB firmware updates](updating-firmware.md).
