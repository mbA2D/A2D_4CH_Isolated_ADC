"""Update one identified ADC through its existing Maple v2 USB bootloader."""

import argparse
import json
import re
import struct
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path

import serial
from serial.tools import list_ports

from device_snapshot import identify, snapshot

APPLICATION_START = 0x08002000
APPLICATION_END = 0x0800F800


def check_image(binary, expected_serial, expected_version=None):
    image = binary.read_bytes()
    if not 8 <= len(image) <= APPLICATION_END - APPLICATION_START:
        raise RuntimeError("Binary does not fit the reserved application region")
    stack, reset = struct.unpack_from("<II", image)
    if not 0x20000000 < stack <= 0x20005000 or stack % 4:
        raise RuntimeError("Invalid STM32F103 initial stack pointer")
    if not reset & 1 or not APPLICATION_START <= (reset & ~1) < APPLICATION_END:
        raise RuntimeError("Binary is not linked for the 0x08002000 application address")
    if expected_serial.encode("ascii") + b"\0" not in image:
        raise RuntimeError("Expected device serial is absent from the firmware image")
    if expected_version and expected_version.encode("ascii") + b"\0" not in image:
        raise RuntimeError("Expected firmware version is absent from the image")


def dfu_interfaces(output, physical_path):
    matches = []
    for line in output.splitlines():
        if not re.search(r"Found DFU: \[1eaf:0003\]", line, re.IGNORECASE):
            continue
        path = re.search(r'path="([^"]+)"', line)
        alternate = re.search(r"\balt=(\d+)", line)
        name = re.search(r'name="([^"]*)"', line)
        usb_serial = re.search(r'serial="([^"]*)"', line)
        if path and alternate and path.group(1) == physical_path and alternate.group(1) == "2":
            if not name or not re.search(r"0x0?8002000", name.group(1), re.IGNORECASE):
                raise RuntimeError("DFU alternate 2 does not advertise the expected application address")
            matches.append({"path": path.group(1), "serial": usb_serial.group(1) if usb_serial else ""})
    return matches


def reset_to_bootloader(connection, version):
    if not re.fullmatch(r"V[0-9]+\.[0-9]+\.[0-9]+", version):
        raise RuntimeError("Unrecognized running version; refusing to guess its reset protocol")
    # The one-time transition from deployed <=V2.1.2 images still needs their
    # legacy handshake. New applications accept only a complete USB command.
    if tuple(map(int, version[1:].split("."))) >= (2, 1, 3):
        print("Requesting bootloader with USB SCPI SYST:BOOT", flush=True)
        connection.dtr = True
        if connection.write(b"SYST:BOOT\n") != len(b"SYST:BOOT\n"):
            raise RuntimeError("Short write of bootloader command")
        return
    print("Requesting bootloader with the legacy handshake for the running older image", flush=True)
    # STM32duino/Arduino_Tools' upload_reset sequence, sent only to the verified port.
    connection.rts = False
    connection.dtr = False
    connection.dtr = True
    time.sleep(0.05)
    connection.dtr = False
    connection.rts = True
    connection.dtr = True
    time.sleep(0.05)
    connection.dtr = False
    time.sleep(0.05)
    connection.write(b"1EAF")
    try:
        connection.flush()
    except serial.SerialException:
        pass  # A successful reset can disconnect the COM port immediately.


def require_single_maple(output, physical_path):
    # The libusb-win32 Maple writer cannot select a physical path. Qualify its
    # only matching device first; it also refuses multiple matching devices.
    paths = set()
    for line in output.splitlines():
        if re.search(r"Found DFU: \[1eaf:0003\]", line, re.IGNORECASE):
            match = re.search(r'path="([^"]+)"', line)
            if not match:
                raise RuntimeError("A Maple DFU device has no identifiable physical path")
            paths.add(match.group(1))
        elif re.search(r"Cannot open DFU device 1eaf:0003", line, re.IGNORECASE):
            raise RuntimeError("Another Maple DFU device cannot be identified safely")
    if paths != {physical_path}:
        raise RuntimeError("Legacy Maple upload requires exactly one DFU device at the verified USB path")


def update(args):
    check_image(args.binary, args.serial, args.version)
    if args.port == "UNSET":
        raise RuntimeError("Set upload_port in platformio.local.ini or pass --upload-port")
    before = snapshot(args.port, args.serial, args.usb_serial)
    if not before["usb_serial"] or not before["usb_location"]:
        raise RuntimeError("A stable USB identity and physical port path are required")
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    records = args.binary.parent / "uploads" / timestamp
    records.mkdir(parents=True, exist_ok=True)
    (records / "before.json").write_text(json.dumps(before, indent=2) + "\n", encoding="utf-8")
    started = time.monotonic()
    events = []

    def record(stage, **details):
        events.append({
            "stage": stage,
            "time_utc": datetime.now(timezone.utc).isoformat(),
            "elapsed_seconds": round(time.monotonic() - started, 3),
            **details,
        })
        (records / "timeline.json").write_text(json.dumps(events, indent=2) + "\n", encoding="utf-8")

    connection, descriptor, identity = identify(args.port, args.serial, before["usb_serial"])
    print(f"Verified {identity} on {args.port}, USB path {descriptor.location}", flush=True)
    record("application_identified", identity=identity, usb_path=descriptor.location)
    try:
        record("reset_requested")
        reset_to_bootloader(connection, identity.split(",")[3])
        # Closing drops DTR and purges unfinished USB traffic in new firmware.
        # Keep this handle open until the command has reset the application.
        deadline = time.monotonic() + 12
        interface = None
        last_listing = ""
        listing_history = []
        while time.monotonic() < deadline:
            listing = subprocess.run([
                str(args.dfu_util), "-d", "1eaf:0003", "-l"
            ], capture_output=True, text=True, timeout=4)
            last_listing = listing.stdout + listing.stderr
            listing_history.append(last_listing)
            matches = dfu_interfaces(last_listing, before["usb_location"])
            if len(matches) > 1:
                raise RuntimeError("Ambiguous DFU match; refusing to upload")
            if matches:
                interface = matches[0]
                record("bootloader_detected", usb_path=interface["path"], probes=len(listing_history))
                break
            time.sleep(0.1)
    finally:
        connection.close()
    (records / "dfu-list.txt").write_text("\n--- probe ---\n".join(listing_history), encoding="utf-8")
    if interface is None:
        record("bootloader_timeout")
        raise RuntimeError(f"Target bootloader not accessible; see {records / 'dfu-list.txt'}")

    # Modern libusb versions fail the completion packet with this board's
    # existing libusb-win32 driver. Use the original Maple writer, after a fresh
    # physical-path and single-device check. Never choose an arbitrary DFU unit.
    listing = subprocess.run([str(args.dfu_util), "-d", "1eaf:0003", "-l"],
                             capture_output=True, text=True, check=True, timeout=4)
    require_single_maple(listing.stdout + listing.stderr, interface["path"])
    if b"More than one" not in args.maple_dfu_util.read_bytes():
        raise RuntimeError("Unrecognized Maple writer; multi-device rejection is required")
    command = [str(args.maple_dfu_util), "-d", "1eaf:0003", "-a", "2"]
    command.extend(["-D", str(args.binary), "-R"])
    print(f"Uploading {args.binary.stat().st_size} bytes to target USB path {interface['path']}", flush=True)
    record("download_started", binary_bytes=args.binary.stat().st_size)
    flashed = subprocess.run(command, capture_output=True, text=True, timeout=60)
    flash_output = flashed.stdout + flashed.stderr
    (records / "dfu-upload.txt").write_text(flash_output, encoding="utf-8")
    print(flash_output, flush=True)
    record("download_finished", returncode=flashed.returncode)

    # Some Maple/libusb combinations return an error on the final USB reset even
    # after a successful transfer. Verify the running device before accepting it.
    after = None
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        ports = [p for p in list_ports.comports() if p.serial_number == before["usb_serial"]]
        if len(ports) == 1:
            try:
                after = snapshot(ports[0].device, args.serial, before["usb_serial"])
                break
            except (RuntimeError, serial.SerialException):
                pass
        time.sleep(0.25)
    if after is None:
        record("application_timeout")
        raise RuntimeError(f"Application did not return after upload; inspect {records}")
    (records / "after.json").write_text(json.dumps(after, indent=2) + "\n", encoding="utf-8")
    if after["identity"].split(",")[3] != args.version:
        raise RuntimeError(f"Running version does not match {args.version}: {after['identity']}")
    for command in ("CAL 0?", "INSTR:RS485?"):
        if after["queries"][command] != before["queries"][command] and not args.allow_settings_reset:
            raise RuntimeError(f"Persistent settings changed: {command}; backups are in {records}")
    if "finished!" not in flash_output and "Download done." not in flash_output:
        raise RuntimeError(f"dfu-util failed (exit {flashed.returncode}); inspect {records}")
    record("application_verified", identity=after["identity"], port=after["port"], settings_preserved=all(after["queries"][c] == before["queries"][c] for c in ("CAL 0?", "INSTR:RS485?")))
    print(f"Verified {after['identity']} on {after['port']}; settings verification complete", flush=True)
    print(f"Upload records: {records}", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--usb-serial")
    parser.add_argument("--allow-settings-reset", action="store_true", help="Allow intentional new-format defaults; before/after values are backed up")
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--dfu-util", type=Path, required=True)
    parser.add_argument("--maple-dfu-util", type=Path, required=True)
    update(parser.parse_args())
