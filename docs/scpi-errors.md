# Queried errors — V5.0.0

The firmware sends text only in response to a query. Valid setters are silent.
Failed setters, rejected commands and failed queries record an error without
sending an automatic error line. Serial provisioning no longer returns `OK`.
Names remain exact and case-sensitive; query/comma syntax and one command per
LF line are unchanged from V4.0.0.

## Commands and host behavior

`SYST:ERR?` removes and returns the oldest error as `code,"message"`. The empty
response is `0,"No error"`. `*CLS` clears all queued errors and sends nothing;
it does not change calibration, identity, address or ADC configuration.

For example, send each command on its own line:

```text
INSTR:RS485:SET 32
SYST:ERR?
```

Only the query produces output: `-222,"Data out of range"`. A subsequent
`SYST:ERR?` returns `0,"No error"`.

A failed measurement query produces no measurement response. The host must use
a finite read timeout and then query `SYST:ERR?` for the reason. Never treat a
missing reading as zero or as a successful measurement. Query failure does not
stop later complete command lines from being processed. Clients must not assume
one output line per transmitted line: setters produce none, successful queries
produce their response, and failed queries produce none.

The queue holds eight signed 16-bit codes in RAM. It preserves FIFO order. When
full, another error replaces the newest occupied slot with `-350,"Queue overflow"`,
retaining the oldest seven. Further errors while full retain that marker. Each
read frees a slot; zero is never queued. These behaviors follow the
[SCPI error-queue definition, section 21.8](https://www.keysight.com/us/en/assets/9921-01870/miscellaneous/SCPI-99.pdf).

The queue survives `*RST`, transport reset and USB close/open. It clears on
`*CLS` or MCU restart. It is shared by USB and addressed RS485 access on one
board: reading or clearing through either interface affects that board's queue.
It is not written to flash. Foreign or unidentifiable RS485 frames remain
silently discarded; they cannot be attributed to an addressed command.

## Error codes

| Code | Message | Trigger |
| --- | --- | --- |
| 0 | No error | Queue empty |
| -102 | Syntax error | Invalid argument separators/count, misplaced question mark or compound line |
| -113 | Undefined header | Unknown or incorrectly cased command name |
| -128 | Numeric data error | Unsupported integer syntax or failed finite float conversion |
| -200 | Execution error | USB-only provisioning/boot command received over RS485 |
| -222 | Data out of range | Channel, numeric limit or calibration guard rejected |
| -224 | Illegal parameter value | Unsupported gain, sample rate or mode selection |
| -320 | Storage fault | Persistent save or request-ID reservation failed |
| -350 | Queue overflow | Error history exceeded eight entries |
| -363 | Input buffer overrun | Invalid/oversized input frame, or forwarding frame limit exceeded |
| -400 | Query error | A response cannot fit the bounded reply buffer |
| 1001 | ADC read failed | Conversion read failed |
| 1002 | ADC configuration failed | ADC setting/readback or trigger failed |
| 1003 | Identity locked | Attempt to replace an existing serial |
| 1004 | Settings reload failed | `*RST` could not reload/apply stored settings |
| 1101 | RS485 timeout | Forwarded query has no matching complete reply within 1 s |
| 1102 | RS485 write failed | Request/reply was not written completely |
| 1103 | RS485 busy | Stale receive data cannot be drained within the existing bound |

Positive codes are specific to this product. Numeric validation rules are
unchanged; error classification is more specific than the previous generic
`ERROR:INVALID ARGUMENTS` reply. Setter failures do not execute an invalid
request, and grouped numeric replies remain atomic on acquisition failure.

## RS485 and deferred status work

Each board has its own queue. `SYST:ERR?` on the USB master accesses only the
master's queue. Addressed RS485 queries can access a peer's queue using the
existing `address requestID payload` envelope. A failed forwarded query records
the peer's original error on that peer and eventually a timeout on the master.
Forwarded setters still lack acknowledgements: a successful UART transmission
does not establish remote execution, and the master does not collect a remote
setter's error automatically. Update all peers and clients together.

This pass implements the error queue and `*CLS`. It does not implement event
registers, `*ESR?`, `*ESE`, `*STB?`, `*SRE`, `*OPC`, operation/questionable status
trees or service-request notifications. These remain deferred. The interface
continues to implement a limited SCPI-like subset.

## Architecture and tools

The optional `A2D_ScpiErrorQueue.c/.h` module in reusable library version 3.1.0
contains only fixed FIFO state and operations. Code/message mapping remains in
`A2D_Protocol`; the application, dispatcher and transport report typed errors.
`SYST:ERR?` prepares a reply using `peek` and removes the entry only when the
complete reply is accepted by the output adapter, preserving backpressure
behavior. Queue access occurs in the main loop and uses no allocation.

The uploader handles older firmware during the transition. On V5.0.0 and later,
its settings reload explicitly sends `*CLS`, performs `*RST`, then queries
`SYST:ERR?`; this prevents a silent reload failure from passing verification.
Existing error history is cleared before this update step. The provisioning
tool checks `SYST:ERR?`, reads `*IDN?` and verifies identity again after `*RST`;
it expects the legacy `OK` only from older versions. Hardware regression tools
now query errors and use identity queries to detect any unsolicited output.

## Verification on 2026-11-22

The common V5.0.0 image uses 43,884 bytes of flash, leaving 11,412 bytes in the
54 KiB application region. Compared with V4.0.0, this adds 480 bytes of flash and
24 bytes of linker RAM usage; total RAM is 8,688 bytes including the configured
heap/stack reservation. The settings journal format is unchanged.

SHA-256: `10bd9c9fddc0aaba833d4808af189d25a8f1b8f3ea1e5bc8dfe5c5de915d5806`.

Host checks passed: 16 portable parser/queue groups, 50 application groups,
17 board-service groups, 3 CDC groups, 4 deferred bootloader checks, 16 numeric
formatting cases, the copied-package standalone C example, and 31 Python tests.
Injected ADC, storage and UART failures were checked in host tests. Queue tests
cover overflow, wrap, independence, clear, NULL handling, silent errors, failed
reply enqueue, addressed RS485 access and preservation through USB backpressure.

Physical tests used only unit 0003, USB UID `4977386D3659`, COM10, original hub
path `1-10.4.1.1.1.4`. Unit 0001 was never opened or flashed. Passed:

- Initial V4.0.0→V5.0.0 and repeat V5.0.0 uploads through the hub, preserving
  calibration, address and serial.
- Twenty error-policy checks including silence without a following query,
  specific error codes, FIFO/overflow, repeat provisioning, `*CLS`, `*RST`,
  close/open persistence and missing-peer timeout.
- The 43-command malformed grammar regression, 20,000 malformed/blank frames,
  280 packet/coalescing checks and 51 USB boot/session checks, with RAM canaries
  intact and no detected unexpected action, reset or hang.
- End-to-end acquisition across gain/rate/mode changes and temporary calibration.
  Thirty readings per unconnected input averaged 0, 0, 0 and 0.000433 V; channel 4
  ranged 0.0004–0.0005 V. Stored settings and an empty queue were restored.

No voltage reference or second RS485 peer was connected, so these checks do
not establish absolute accuracy or physical peer-to-peer interoperability.
Detailed records and the tested image are in the project's
`Validation/2026-11-22_SCPI_Errors/` directory. The compact record is
[scpi-error-tests-2026-11-22.json](scpi-error-tests-2026-11-22.json).
