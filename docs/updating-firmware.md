# Updating firmware over USB

V5.0.1 uses one common application image with a persistent serial. It retains
the existing Maple bootloader at `0x08000000`; the application occupies
`0x08002000` through at most `0x0800F7FF`. The final two 1 KiB pages hold the
settings journal. Normal updates never replace the bootloader or those pages.

V5.0.1 defaults RS485 to **230400 baud, 8N1**. Update every board on the bus;
older releases used 57600 baud. USB CDC communication is unaffected by this
RS485 rate change. Calibration, address and serial storage remain compatible
with the V5 journal.

## Host preparation

Build using the root README. Install the appropriate Maple DFU driver on the
host. This Windows setup uses a modern `dfu-util` for physical-path discovery
and the original libusb-win32 Maple writer for transfer. Keep each executable
beside its matching USB DLL. Paths are configurable; the optional local cache
is `%USERPROFILE%/.cache/a2d-tools/dfu/{modern,maple}/dfu-util.exe`.
The modern lister tested here is dfu-util 0.11; the Maple writer comes from the
STM32duino tools 1.0.2 package. The updater verifies the writer's multiple-device
rejection support. Cross-PC driver installation is not qualified by this run.

Copy `upload.local.json.example` to `upload.local.json`. Set `port`, `serial`,
`usb_serial`, `dfu_util` and `maple_dfu_util`. Values must be strings. Do not put
`provision_serial` or `allow_settings_reset` into this file; those are explicit
one-time command-line choices. Close serial monitors, then:

```powershell
.\.venv\Scripts\python.exe tools/usb_upload.py
```

CLI arguments override local settings. `--binary` and `--version` can select an
explicit image; defaults are the CMake release output and source version.
The uploader records before/after identity and settings plus DFU logs under
an `uploads` directory next to the image. It checks the USB UID before opening
the COM port, then checks the instrument identity. After `SYST:BOOT`, it holds
the handle open until Maple DFU appears at the same physical USB path and
requires exactly one accessible Maple DFU target before transfer.
It rechecks that the freshly identified application's USB path still matches
its first snapshot before issuing any reset. The uploader records live RAM
settings separately in `before-live.json`, then sends `*RST` over that same
verified connection and reads persisted settings into `before.json`.
Unsaved calibration/address and runtime ADC settings are discarded, as they
would be by a firmware restart; nothing is implicitly saved. Post-upload
comparison uses the persisted baseline, not the old volatile state. On V5.0.0
and later, the uploader sends `*CLS` before the settings reload and queries
`SYST:ERR?` afterwards so a silent reload failure cannot pass verification.
This explicitly clears any existing error history before the update.

Use this version's uploader for the V4.0.0 syntax transition. It selects the
snapshot query spelling from the verified running version, so both the old
application and the new one can be checked during a single update. Snapshot
report keys now use `CAL? 0`, `MEAS:VOLT? 0` and `MEAS:VOLT:ADC? 0`. This host
transition support does not add legacy command aliases to the new firmware.
The provisioning tool also selects its verification protocol by running version:
V5.0.0 and later use `SYST:ERR?` plus identity readback; older versions use
their original `OK` acknowledgement.


## First transition from compiled serials

V2.1.x has no stored serial. The new firmware migrates a valid old journal,
preserving calibration, address and request-ID history, and initially reports
`UNASSIGNED`. For this transition only, run:

```powershell
.\.venv\Scripts\python.exe tools/usb_upload.py --provision-serial
```

The updater provisions the explicitly selected serial only after matching the
same USB UID and physical path. It reads it back, reloads settings and verifies
identity again. Ordinary updates omit this flag. Manufacturing can instead use
`tools/provision_identity.py` after the first SWD installation.

Do not downgrade to V2.1.x after provisioning: it cannot read the new 60-byte
journal. The saved V2.2.0 common-image checkpoint understands the new format.
V1 upgrades additionally require explicit `--allow-settings-reset` and
recalibration because their storage format differs. Very old firmware may
need one direct-PC transition to install the hub-safe disconnect sequence.

The current calibration policy is also enforced when settings load. An older
coefficient outside the documented gain/offset limits is replaced with nominal
calibration for that input, while trusted serial/address/request-ID metadata is
retained. The updater reports the changed calibration readback. Check `CAL? 0`
and recalibrate affected inputs before relying on measurements.

## Serial clients and bootloader entry

Keep DTR high while communicating and clear it before closing. DTR transitions
clear incomplete frames, stale replies and forwarded transactions without
changing instrument settings. The board cannot infer host handle changes when
the host sends no session signal. A NUL byte followed by LF rejects an unfinished
frame. In V5.0.0 this queues error -363 without sending a reply; read it with
`SYST:ERR?` once framing is restored.

`SYST:BOOT` followed by LF (or CRLF) is USB-only. The application stops USB,
holds D+ low for 500 ms and resets into Maple. Legacy `1EAF` reset traffic is
used by the uploader only for verified applications at or below V2.1.2.
Physical unplug/replug recovery and the bootloader remain separate from the
application's `*RST`, which reloads settings and resets ADC configuration.

## Manufacturing and unknown settings namespaces

Provisioning the serial is one step, not the entire functional test. Confirm
ADC reads and request-ID allocation/forwarding before releasing a manufactured
unit. An addressed query to a known peer is preferable. With RS485 disconnected
and address0, send `MEAS:VOLT? 5`, allow the forwarded-query timeout to finish,
then read `SYST:ERR?`. In V5.0.0, error 1101 (`RS485 timeout`) rather than -320
(`Storage fault`) establishes that an ID could be allocated and the frame could
be transmitted; it does not qualify peer operation. The failed measurement
query itself sends no reply.

If power fails during the first-ever settings initialization, both pages can
be nonblank with no valid committed record. Firmware deliberately disables
request-ID allocation for that ambiguous namespace to prevent ID reuse.
Serial provisioning, calibration reset and `*RST` do not repair it. A known
virgin unit needs controlled manufacturing/service reinitialization; do not
blindly reset a previously used namespace or claim this as automatic recovery.

An unsupported legacy settings layout can also leave this disabled state after
migration. This occurred on unit0001 and was confirmed by reading its raw
journal. The [controlled recovery record](request-id-recovery-2026-11-22.md)
documents a target-locked repair that preserved calibration and identity and
then passed RS485 tests in both master directions. It is not an automatic reset
policy for unknown or exhausted namespaces.
