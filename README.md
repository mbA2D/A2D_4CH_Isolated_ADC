# A2D 4CH Isolated ADC firmware

[V2.1.3 release](https://github.com/mbA2D/A2D_4CH_Isolated_ADC/releases/tag/V2.1.3)
retains Maple USB field updates. See the
[upgrade instructions](docs/updating-firmware.md) before updating a V1 chain or
building for another unit; the attached release binary is for serial 0003 only.

Development worktree: `D:\Documents\A2D\Projects\003 - Isolated 4CH ADC\Firmware`,
branch `platformio-migration`. GitHub Desktop should select **Firmware** under the
existing A2D_4CH_Isolated_ADC repository. The shared Git metadata remains in the
original Arduino-library checkout; keep that checkout in place.

## Current V2.1.3 scope

V2.1.3 adds the USB-only `SYST:BOOT` command and clears unfinished USB traffic
at DTR/session changes. It removes the raw `1EAF` receive trigger. The guarded
uploader selects the protocol from the verified running version and keeps its
COM handle open until bootloader detection completes. Calibration, settings and
the installed Maple bootloader are retained. Independent review and qualification
on 0003 through its original hub are complete: 76 host checks, 123 boot/session
checks, 100,000 malformed frames, repeated uploads and a ten-minute stress soak.
See [`docs/release-notes-V2.1.3.md`](docs/release-notes-V2.1.3.md) and
[`docs/bootloader-options.md`](docs/bootloader-options.md).

## Previous V2.1.2 scope

**Release on hold:** a later hardware retest reproduced a false bootloader reset
with fragmented input and an unintended command across serial sessions. See
[`docs/adversarial-retest-2026-11-21.md`](docs/adversarial-retest-2026-11-21.md).

V2.1.2 rejects calibration points and calculated coefficients outside documented
guard bands before changing state or forwarding. Stored coefficients are also
checked; invalid finite legacy values are repaired on affected channels while
preserving valid calibration, address and request-ID history. Other command
ranges were already validated. See [`docs/command-ranges.md`](docs/command-ranges.md)
and [`docs/release-notes-V2.1.2.md`](docs/release-notes-V2.1.2.md).

The final V2.1.2 candidate passed 65 host checks, 164 hardware command-range cases,
malformed-traffic testing, a ten-minute stress soak and repeated firmware uploads
on 0003 through its original hub. See
[`docs/command-ranges-2026-11-21.json`](docs/command-ranges-2026-11-21.json) for
counts, measured inputs and qualification limits. Unit 0001 was excluded.

V2.1.1 addressed two defects reproduced during adversarial testing on unit0003:
the USB bootloader hook required an exact four-byte `1EAF` packet after
the existing control-line sequence (superseded by `SYST:BOOT` in V2.1.3), and complete numeric replies are checked
against Arduino Print's representable range before formatting. Out-of-range
finite values return `ERROR:NUMERIC RANGE` instead of `ovf` or a partial CSV.
Normal commands, stored calibration and RS485 settings format are unchanged.
The deferred interface improvements remain in GitHub issue #1.

V2.1.0 extracts the command parser and bounded line reader into the independent
C++11 library [`lib/A2D_Scpi`](lib/A2D_Scpi/README.md). Copy that folder into another
PlatformIO project's `lib/` directory to reuse it. It has no Arduino, STM32 or ADC
dependencies. The included relay-controller example builds independently on a PC;
the same source can be added to an STM32CubeIDE C++ project later.

The ADC's command table, argument ranges, channel mapping, calibration checks,
handlers and USB/RS485 adapters remain application-specific. Existing command
forms, replies and settings format are preserved; no recalibration/storage
migration is needed for the V2.0.1-to-V2.1.0 update.

V2 fixes the eight issues documented in the project validation folder
`../Validation/2026-11-21_System_Review/review-findings.json`. It does not add the
separately proposed SCPI aliases, getters, error queue, acquisition commands,
RS485 CRC/write acknowledgement, discovery, or calibration export interfaces.
Older firmware interoperability is not supported by the new RS485 framing or
settings format. The first V2 installation uses nominal calibration defaults;
recalibrate with known reference voltages before making accuracy claims.

| Issue | Fix |
| --- | --- |
| Failed ADC reads looked like measurements | Check every I2C read length/configuration and write ACK; return a measurement/configuration error |
| Cached sample used with new gain/reset | Read ready hardware data; no software conversion cache; bounded readiness wait |
| Late RS485 reply matched another query | Echo address and request ID; reject mismatches; reserve IDs durably across reboot |
| Settings saves wore/partially erased calibration | One commit per save, CRC/sanity checks, alternate flash pages, commit marker last |
| USB flush could hang the main loop | Buffer a whole reply, bounded queue admission, no USB flush |
| Trigger left the wrong mode shadow | Update every channel to single-shot after general-call trigger |
| Setter queries changed state | Reject unsupported question-mark forms before dispatch |
| ADC scale used 32767 | Signed 12/14/16-bit normalization with denominator 32768 |

## Build and upload

Open this folder in VS Code with PlatformIO. Copy `platformio.local.ini.example`
to `platformio.local.ini` and set the intended serial, port, and USB chip serial.
The local file is ignored by Git. Default production identity remains 0002;
never use that default when building another physical unit.

```powershell
pio run -e usb_unit
pio run -e usb_unit -t upload
```

Only unit **0003**, COM10, USB chip serial `4977386D3659`, hub path
`1-10.4.1.1.1.4`, is authorized for this development work. Unit **0001** is excluded.
The uploader checks USB identity and `*IDN?` before resetting and accepts DFU only
at the same physical path with no other Maple DFU device present. It backs up
calibration/address, uploads, and verifies the returning identity and settings.

The initial V1-to-V2 installation deliberately initializes the new settings
format. For that one upload, set `custom_allow_settings_reset = yes` locally.
The uploader records the changed values rather than reporting preservation.
Remove the setting immediately afterward; ordinary repeat updates check for
unchanged calibration/address. Close serial monitors before uploading.

The existing Maple bootloader remains at 0x08000000. V2.1.3 field updates use the
complete USB-only `SYST:BOOT` command. The uploader retains the DTR/`1EAF`
handshake only to transition verified older applications. `loop()` stops USB,
holds D+ low for 500 ms, then resets. This hub-safe sequence is unchanged from V1.0.5.
Very old applications may need one direct-PC update to install that sequence.
No bootloader rewrite or SWD access is needed for normal field updates.

`platformio.ini` pins ST STM32 19.7.1, Arduino STM32 core 2.12.0, GCC 12.3.1, CMSIS,
and upload tools. ADC I2C operations are now directly implemented in Measurement;
the old external MCP3425/Arduino EEPROM implementations are no longer used.
Single-buffer USB receive and the default two-packet transmit queue are retained.

## Modules

Small concrete classes, fixed-size buffers and statically allocated state keep
the design suitable for the F103C8. There is no RTOS or service container.

| Module | Responsibility |
| --- | --- |
| `firmware/main.cpp` | Construct objects, start hardware, service reset and application |
| `A2D_Application` | Schedule one command at a time, preserve reply ordering |
| `lib/A2D_Scpi` | Reusable parsing, numeric conversion and bounded bulk line framing |
| `A2D_Protocol` | ADC command table, argument/channel/calibration rules, owned requests |
| `A2D_Dispatcher` | Local/forwarded execution, complete reply formatting, error propagation |
| `A2D_Transport` | USB output admission, RS485 direction, correlated query/timeout state |
| `A2D_Measurement` | Checked MCP3425 register I/O, readiness and voltage conversion |
| `A2D_MCP3425.h` | Register encodings only |
| `A2D_Calibration` | Two-point calculation and `(raw - offset) * gain` |
| `A2D_SettingsStore` | Validated journal records and request-ID allocation |
| `A2D_Flash` | Bounded two-page erase/program/readback adapter |
| `A2D_4CH_Isolated_ADC` | Board API and GPIO, delegates to the services |
| `A2D_USB_Bootloader` | Deferred STM32 hub-safe bootloader reset |
| `A2D_USB_Session` | CDC session events and F103 endpoint/queue cleanup |

ADC configuration writes restart conversion. Measurements wait for matching
ready data, with a 120 ms readiness budget; underlying I2C calls also have their
own bounded HAL timeout. Failures return `ERROR:ADC READ` or `ERROR:ADC CONFIG`,
never cached numbers or a partial group response. Settings failures return
`ERROR:STORAGE WRITE`; failed reset returns `ERROR:RESET FAILED`.

USB replies are collected before transmission. The complete line must fit the
127 usable bytes of the core's normal TX queue, otherwise an explicit response-
length error is sent. Admission and transmit-drain pacing share a 100 ms deadline and check connection
state directly, without the core boolean operator's sleeping behavior.
A disconnected/non-reading host can lose a whole reply, but cannot hold the main
loop in `flush()` or cause this writer to enqueue half a line. UART flush is still
required before dropping RS485 DE. Sustained complete USB commands retain priority
over incoming RS485 commands; fair scheduling is outside this fix set.

## STM32F103 USB receive backpressure patch

V2.0.1 includes a small project-local correction to the pinned CDC class. When
its receive queue is full, the original ClearBuffer path prepares a zero-length
OUT receive as VALID. The F103 allocation macro makes that a 32-byte buffer;
a following 64-byte packet can STALL the host pipe. The completed packet already
leaves RX in NAK, so the patch keeps that state until the bulk reader frees space
and resumes a full-size receive. The receive-buffer sizing and endpoint behavior
are described in [ST RM0008](https://www.st.com/resource/en/reference_manual/rm0008-stm32f103xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).

`tools/pio_config.py` compiles an environment-local copy of the CDC source using
`tools/usb_rx_patch.py`. The installed/shared framework is never changed. Its
source SHA256 is checked; a different core source fails the build until reviewed.
The fix is conditional on STM32F1 single-buffer mode. Tests compile the actual
pause function and HAL buffer-allocation macro, demonstrating the original
undersized VALID state and the corrected NAK/resume behavior. Diagnostic heartbeat
and fault-capture code used during investigation is absent from the release.

## Commands and RS485 framing

Existing case-sensitive SCPI-like command names remain. Examples:
`MEAS:VOLT 1?`, `MEAS:VOLT:ADC 0?`, `CAL 0?`, `INSTR:GAIN 2,0`,
`CAL:VOLT CH,ACTUAL1,MEASURED1,ACTUAL2,MEASURED2`, and `CAL:SAVE 0`.
Calibration measured points are raw ADC volts. Unsupported setter queries such
as `CAL:RESET 1?` are rejected without mutation. USB replies retain CRLF and the
existing decimal precision. No general SCPI query aliases have been added.

RS485 now requires a decimal address and nonzero 32-bit request ID:

```text
request:  1 65537 MEAS:VOLT 5?\r\n
response: 1 65537 1.2345\r\n
```

The base matches both address and ID. Malformed, foreign, stale and overlong
responses are discarded while the current query remains pending until its
1000 ms timeout. Commands remain serialized during a forwarded query. Incoming
requests to this board echo the received address/ID even for error replies.
Successful write commands remain silent; CRCs and write acknowledgements are
explicitly deferred. Old untagged frames are ignored.

Each base reserves a block of 65,536 request IDs in both settings copies before
issuing any ID from it. Mirroring the high-water mark prevents CRC fallback from
rolling issued IDs backward. This costs two commits per reserved block; ordinary
calibration/address saves still cost one. Reset skips unused IDs in the reserved block. IDs never
wrap into reuse; exhausted/corrupt allocation state fails closed. Calibration
reset does not reset this allocation. Both-record corruption requires service
before remote forwarding can be re-enabled; local USB measurements/uploads stay
available. Independently controlled simultaneous RS485 masters are unsupported.

USB input capacity remains 199 bytes before LF, including optional CR. Forwarded
command payloads reserve the maximum ID and CRLF: 185 bytes for addresses 0..9,
184 for addresses 10..31. Channel 0 means this board; channels 1..128 map four per
address. Existing RS485 channel-0 measurement/calibration/save/reset behavior
remains silent; channel-0 gain/rate/mode applies to all four local ADCs.

## Flash and manufacturing

| Region | Addresses | Size |
| --- | --- | --- |
| Existing bootloader | 0x08000000..0x08001fff | 8 KiB |
| Application | 0x08002000..0x0800f7ff | 54 KiB maximum |
| Settings slot 0 | 0x0800f800..0x0800fbff | 1 KiB |
| Settings slot 1 | 0x0800fc00..0x0800ffff | 1 KiB |

The linker and post-build/image checks reserve both settings pages. The board
upload limit is 63488 because the linker subtracts the 8192-byte application
offset; the final binary limit is 55296 bytes. Neither updater nor Flash writes
the bootloader. The flash adapter only erases/programs one of the two fixed pages.

A record contains magic/version, generation, four gain/offset pairs, address,
request-ID high-water mark, CRC32 and a commit word. Each save writes only the
inactive page, verifies it, and leaves the previous valid page intact. The commit
word is programmed last. Newest valid generation wins across generation wrap.
Save-all performs one page rewrite; an unchanged save performs none. Corrupt
records do not supply NaNs or invalid addresses to the application.

Program the qualified existing bootloader once through the manufacturing SWD
fixture, then the unit-specific application at 0x08002000. `stlink` builds the
same offset application; manufacturing fixture programming and actual power-cut
qualification remain separate hardware work.

## Tests and evidence

```powershell
python tools/run_tests.py
python -m unittest discover -s tests -p "test_*.py"
```

Host tests cover the actual command/transport/service/flash adapter code with
simulated ports, I2C and HAL. The Windows flash test maps a private address range
matching the MCU addresses; it never accesses physical hardware. Cases include
I2C short reads/NACK/readiness, all sample rates/gains and signed codes, stale
replies across timeout/reboot, USB backpressure and disconnect, query validation,
flash-operation interruption at every commit step, CRC corruption, batching,
request-ID persistence/exhaustion and generation wrap. USB-reset and uploader
identity/region checks remain included.

`hardware_smoke.py` tests only an explicitly identified unit. `measurement_test.py`
collects actual raw/calibrated samples and sweeps configuration, temporarily using
RAM calibration. `fixes_smoke.py` is restricted to 0003 on the hub and tests query
rejection plus real settings save/reset/reload, then restores nominal calibration
and address0. RS485 timeout tests require its terminals disconnected.

Current work records are under `../Validation/2026-11-21_Eight_Fixes/`, including
pre-change calibration/source backups, build/upload logs, host test evidence,
reviewer findings and hardware results. Physical RS485 peer operation and power-
cut timing are not inferred from host simulation. Unconnected-input readings
cannot establish analog accuracy; a reference source is needed to recalibrate.

Earlier V1.0.5/V1.1.0 images, measurements and reviews remain in their project
validation folders. `docs/module-reviews-2026-11-21.json` describes the earlier
V1.1.0 module review; the current fix review supersedes its relevant findings.

## V2.0.1 qualification on 2026-11-21

- Unit0003 was installed and re-uploaded through the original hub; the returning
  identity and saved calibration/address were verified. Unit0001 was untouched.
- 50 host groups pass: 23 application, 5 USB-reset, 10 service/flash, and 12 Python
  uploader/core-patch groups. USB and SWD builds pass; SWD was build-only.
- 34 hardware smoke checks pass, also after the repeat upload; the 4096-byte
  discarded frame and 199/200-byte boundaries no longer stall reception.
- 280 repeated USB packet/coalescing checks pass, including 10 cycles through
  8192-byte inputs and 100 coalesced identity commands. Recovery after 5000 unread
  identity requests passes with fresh address, identity, calibration and readings.
- 23 real persistence/query checks pass. 60 samples per channel, all 12 gain/rate
  combinations and single-shot triggering pass 10 measurement/compatibility checks.
- Inputs were unconnected. Default means were 0, 0, 0 and about0.44mV; readings have
  0.1mV output resolution. Nominal calibration/address0 were restored and saved.
  Reference recalibration remains necessary for accuracy.
- The release binary is 53840 bytes of the 55296-byte application allowance;
  reported static RAM is 6380 bytes of20480. Two flash settings pages are reserved.

The development unit's allocator was deliberately provisioned once with RS485
disconnected after its initial unrecognized legacy contents disabled forwarding.
The provisioning code/build flag was removed before V2.0.1 qualification. No ID
was issued by that temporary service image. New manufacturing units start with
erased settings pages; never reset a deployed ID namespace on a live bus.

Independent review dispositions and final hardware status are in
`docs/eight-fixes-review-2026-11-21.json`. The project validation folder contains
images/ELF/source snapshots, checksums, before/after settings, upload traces,
measurements, simulated fault results, and the receive-stall investigation.

## V2.1.0 reusable parser qualification on 2026-11-21

The reusable package is `lib/A2D_Scpi`: three source/header files, PlatformIO
metadata, retained license, standalone tests, a relay-controller example, and a
guide covering PlatformIO, CubeIDE, input adapters and argument lifetimes. The
library uses an explicit command table; projects keep their own handlers and
validation. No new ADC command forms or RS485 features were introduced.

- 61 host groups/checks pass: the existing 50, ten independent SCPI groups and
  one copied-package example check. The package compiles independently for ARM
  Cortex-M3 with no Arduino/STM32 include paths, exceptions or RTTI.
- 51,246 comparisons against the saved V2.0.1 parser pass, including valid and
  malformed command mutations, both sources, channel addressing and line limits.
- V2.1.0 was uploaded to unit0003 through the original hub. Identity, saved
  calibration and RS485 address were checked before/after; unit0001 was untouched.
- 34 hardware checks, 280 USB packet/coalescing checks and ten measurement checks
  pass. Measurements include 60 raw/calibrated samples per channel, all 12 gain/
  rate combinations and single-shot triggering. Unconnected-input means were
  0, 0, 0 and about0.44mV; these do not establish reference accuracy.
- The application binary is 53688 bytes, 152 bytes smaller than V2.0.1, leaving
  1608 bytes of the 55296-byte allowance. Static RAM remains6380 bytes.

Records, source/image snapshots and the copyable `A2D_Scpi-1.0.0.zip` package are
under `../Validation/2026-11-21_SCPI_Reuse/`. Saved calibration/storage format and
the durable request-ID namespace are preserved. Changes remain uncommitted in
the development worktree.

## Independent reusable SCPI review on 2026-11-21

An independent sub-agent reviewed the reusable library and its ADC integration
against the documented grammar and API contracts. No actionable functional
defects were found. Additional host probes covered 24,000 randomized framing
cases, 12,480 channel/routing/request-preservation checks and 128 calibration
argument-order/ownership checks. Their source and output are retained with the
review evidence; the parent agent inspected the probes and their results.

The PowerShell example invocations in the library guide were corrected to use
`./instrument.exe` and `./test_scpi.exe`, then executed from a copied package.
Firmware/library implementation and V2.1.0 binary are unchanged. The existing
61 host groups/checks pass. Unit0003 through the original hub passed 34 command
checks, 280 USB framing/coalescing checks and ten measurement checks, including
60 samples per channel and all gain/rate combinations. Saved calibration and
address are preserved; unit0001 was untouched.

The report is `docs/scpi-library-review-2026-11-21.json`; complete review, test and
updated package records are under `../Validation/2026-11-21_SCPI_Review/`. This
review does not add SCPI/RS485 features or change the settings format.

## V2.1.1 adversarial release qualification on2026-11-21

Two defects were reproduced on0003 and fixed: malformed long `1EAF` packets
caused an unintended CPU reset after DTR activity, and extreme finite calibration
could produce a nonnumeric `ovf` reply. The exact-token reset check and atomic
numeric range check are covered by host and real-device regressions.

The corrected image passed25000 adversarial frame/numeric cases,600 initial
configuration cycles,100 port reopen cycles and a600.11-second mixed-load soak.
The soak completed11414 state cycles,22828 measurement vectors,457 discarded
64KiB frames and12 unread-reply flood/recovery rounds with RAM canaries intact.
62 host groups/checks and the larger application/parser/RS485 host probes pass.
Three hub uploads, saved non-default settings across update, final persistence,
command and real-input measurement checks pass. Unit0001 was untouched.

The binary is53768 bytes (1528 bytes free); static RAM remains6380 bytes. The
candidate, tests, source checksums and known qualification limits are described
in `docs/release-notes-V2.1.1.md` and `docs/release-stress-2026-11-21.json`.
Full records and the unit0003 candidate bundle are in
`../Validation/2026-11-21_Release_Stress/`. Deferred improvements remain in
[issue #1](https://github.com/mbA2D/A2D_4CH_Isolated_ADC/issues/1).
