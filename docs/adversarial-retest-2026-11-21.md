# V2.1.2 adversarial retest: release on hold

Resolution: both findings below are fixed and physically re-tested in V2.1.3.
See `release-notes-V2.1.3.md`. This report and the V2.1.2 hold remain as the
historical record of the failing version.

The exact V2.1.2 candidate was uploaded through the original hub to unit 0003,
COM10, USB UID `4977386D3659`, path `1-10.4.1.1.1.4`. Saved settings were preserved.
The binary SHA256 is
`1e532120b4e855cf2f1af0ab3e5e6bf52c57e9eb47dc22278a73ea64de601176`.
Unit 0001 was excluded from all device operations. Production firmware was not
changed during this retest.

Two remaining failure paths were reproduced on the physical board. Resolve
these before publishing a release. The earlier qualification remains a record
of its passing cases; it does not cover the failures below.

## R1: fragmented malformed input can enter the bootloader

After DTR activity, send the malformed line `1EAF_NOT_A_RESET` followed by LF
in two USB writes: `1EAF`, a 20 ms gap, then `_NOT_A_RESET\n`. The first write
immediately initiates reset; the second fails because the USB interface has
already stopped. Unit 0003 disappears from COM10 and enumerates in Maple DFU at
the same physical hub path. The same behavior was reproduced with 200 bytes of
an unterminated oversized line already buffered before the token.

Expected application behavior: reject the malformed/oversized frame and retain
the volatile calibration/address markers. Observed behavior: enter DFU and lose
the running application's volatile state. The same longer token sent together
in one packet is rejected, so behavior depends on packetization.

The exact-four-byte test in `A2D_USB_Bootloader.cpp:16` checks a USB receive
packet, rather than a complete application command. The core invokes it after
counting control-line changes; it does not know the application line state.
This leaves a false-reset path after the earlier long-packet fix.

The fix should give bootloader entry explicit, validated framing independent of
USB packet boundaries, with the guarded updater adjusted accordingly. A quiet
delay alone cannot distinguish a token from a slowly fragmented longer line.
Preserve the working hub disconnect/reset sequence.

Both probes were recovered by uploading the identical candidate. The DFU writer
was used only after verifying that the sole Maple DFU device was at 0003's
previously verified physical path. Returning runtime UID, serial, version,
saved calibration and address were checked.

Evidence: `boot-fragment.json`, `boot-context.json`, their matching `.py`
reproducers and DFU/recovery logs in the evidence directory below.

## R2: unfinished commands survive serial session changes

1. Set volatile address 7 and a distinct calibration marker.
2. Send `INSTR:RS485:SET 3` without LF.
3. Close COM10, wait 200 ms, and reopen it.
4. Send `1\n`, which is invalid as a standalone command.

The address becomes 31. Bytes from the old and new programs have combined into
a valid setter. Calibration remains at its volatile marker, confirming this was
an unintended command completion rather than a board reset. DTR low/high without
closing the port reproduces the same change.

A related symptom occurs when the old session leaves an oversized unterminated
frame: the new session's first `*IDN?` gets `ERROR:LINE TOO LONG`; the next query
works. The line buffer's length/discard state survives the session boundary.

`Transport::poll` in `A2D_Transport.cpp:46` consumes the persistent USB line reader
without session lifecycle handling. The application never connects a USB
session transition to clearing that reader. Clear pending USB input and framing
state at the session boundary; also test whether pending forwarded transactions
need cancellation so a new client cannot inherit the previous client's reply.
That latter case is a follow-up requirement, not a measured finding in this run.

Evidence: `sessions.json` and `edge_cases.py --phase sessions`.

## Passing coverage in this retest

| Test | Result |
| --- | --- |
| Fresh upload of the exact V2.1.2 image through the hub | Passed; saved settings preserved |
| Existing targeted regressions | 5 passed |
| New seeded malformed/blank USB frames | 100,000 passed; 3,127 state-marker checks |
| Injected byte values in an invalid setter | 255 passed; LF intentionally excluded as a command boundary |
| Splits of valid and invalid commands | 81 passed |
| Packet/oversized frame sizes | 14 sizes passed, including 1 MiB |
| Local/invalid commands queued behind absent RS485 peers | 30 batches passed in order, about 1 second each |
| Alternating saved calibration, reset and malformed-input batches | 64 actual changing commits passed; 128 hardware measurement vectors |
| Port reopenings with varied line settings | 100 passed when no partial frame remained |
| Final command/USB smoke checks | 34 passed |

The two reset probes required recovery uploads. There was no unrecoverable hang
in the other probes. The board was left running V2.1.2 through the hub, with
nominal saved calibration and address 0. Serial handles were closed.

Inputs were unconnected and RS485 terminals disconnected. This run cannot
establish reference-voltage accuracy or physical-peer RS485 behavior. Physical
power interruption and I2C fault injection were not performed.

Evidence directory, relative to the firmware project:
`../Validation/2026-11-21_Adversarial_Retest/`.
The machine-readable result is `summary.json` there. Earlier candidate archives
are retained as evidence; see the adjacent `V2.1.2-RELEASE-HOLD.json` marker.
