# Updating firmware over USB

V2.1.3 retains the existing Maple/STM32duino bootloader. Use the updated project
uploader; old Arduino/Maple reset helpers do not enter the bootloader from this
version. Manufacturing still installs the bootloader/application through SWD.

## Select the correct unit

The release's `.bin` and `.elf` are **unit 0003 builds**, not universal images.
The source supports other units, but their image must be built with the intended
four-digit identity. Do not bypass the uploader's identity and USB-path checks.

Open the tagged source in VS Code with PlatformIO. Copy
`platformio.local.ini.example` to `platformio.local.ini`, then set:

- `custom_device_serial`: the unit's four-digit serial.
- `upload_port`: its current COM port.
- `custom_usb_serial`: its STM32 USB chip UID, as reported by Windows or
  `python -m serial.tools.list_ports -v`.

The example contains the development unit's values; replace them for another
unit. The local file is ignored by Git. Close serial monitors before uploading.

```powershell
pio run -e usb_unit
pio run -e usb_unit -t upload
```

The uploader verifies the running identity, records calibration/address and
selects the entry method from the running version. V2.1.3 and later use the
complete USB-only `SYST:BOOT` command; older verified applications use their
legacy handshake for the transition. It keeps the COM handle open until DFU is
observed, checks the physical USB path and sole Maple device, writes the
application, then verifies the returning identity and saved settings.

The image starts at `0x08002000` and ends before `0x0800F800`; the installed
bootloader and final two settings pages are retained. The attached release binary
is the exact image qualified on 0003 through its hub. The normal PlatformIO upload
command builds the selected unit's image before writing it.

## First upgrade from V1

V2 settings and RS485 framing differ from V1. Record the existing address and
calibration, update all chain peers together, and plan to recalibrate with known
reference inputs. For this one upload only, add:

```ini
custom_allow_settings_reset = yes
```

under `[env:usb_unit]` in the local configuration. Remove it immediately afterward.
Ordinary repeat updates then require saved calibration/address to match. Very old
applications predating the hub-safe disconnect sequence may need one direct-PC
transition upload. An appropriate Maple DFU driver is required on the host;
cross-PC driver installation was not qualified in this test run.

## Serial clients

Keep DTR high while communicating and clear it before closing. DTR transitions
end unfinished USB frames and transactions without resetting instrument settings.
For explicit input resynchronization with DTR high, a NUL byte followed by LF
rejects the unfinished frame; consume `ERROR:LINE TOO LONG` before the next query.
The board cannot infer an operating system's handle changes if the host provides
no DTR/session signal.
