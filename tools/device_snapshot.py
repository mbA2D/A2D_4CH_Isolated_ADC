"""Read one explicitly selected ADC; stop immediately if its identity differs."""

import argparse
import json
import re
from datetime import datetime, timezone
from pathlib import Path

import serial
from serial.tools import list_ports


def identify(port, expected_serial, expected_usb_serial=None):
    descriptor = next((p for p in list_ports.comports() if p.device == port), None)
    if descriptor is None:
        raise RuntimeError(f"{port} is not present")
    if expected_usb_serial and descriptor.serial_number != expected_usb_serial:
        raise RuntimeError(f"USB identity mismatch on {port}; refusing to open it")
    connection = serial.Serial(port, 115200, timeout=2, write_timeout=2)
    try:
        connection.reset_input_buffer()
        connection.write(b"*IDN?\n")
        response = connection.readline().decode("ascii", errors="replace").strip()
        fields = response.split(",")
        if len(fields) != 4 or fields[:3] != [
            "A2D Electronics", "4CH Isolated ADC", expected_serial
        ]:
            raise RuntimeError(f"Unexpected identity on {port}: {response!r}; stopped")
        return connection, descriptor, response
    except BaseException:
        connection.close()
        raise


def read_snapshot(connection, descriptor, identity, *, reload_settings=False):
    """Read through an already verified connection; optional reload is explicit."""
    version = identity.rsplit(",", 1)[-1]
    if not re.fullmatch(r"V[0-9]+\.[0-9]+\.[0-9]+", version):
        raise RuntimeError("Unrecognized firmware version for query syntax")
    parsed_version = tuple(map(int, version[1:].split(".")))
    legacy = parsed_version < (4, 0, 0)
    queued_errors = parsed_version >= (5, 0, 0)
    if reload_settings:
        if queued_errors:
            connection.write(b"*CLS\n")
        connection.write(b"*RST\n*IDN?\n")
        reply = connection.readline().decode("ascii", errors="replace").strip()
        if reply != identity:
            raise RuntimeError(f"Settings reload failed: {reply!r}")
        if queued_errors:
            connection.write(b"SYST:ERR?\n")
            error = connection.readline().decode("ascii", errors="replace").strip()
            if error != '0,"No error"':
                raise RuntimeError(f"Settings reload failed: {error!r}")
    result = {
        "time_utc": datetime.now(timezone.utc).isoformat(),
        "port": descriptor.device,
        "usb_serial": descriptor.serial_number,
        "usb_location": descriptor.location,
        "identity": identity,
        "settings_view": "stored_after_reload" if reload_settings else "live",
        "queries": {},
    }
    # Keep stable report keys while updating a pre-V4 device to the new grammar.
    legacy_queries = {
        "CAL? 0": "CAL 0?",
        "MEAS:VOLT? 0": "MEAS:VOLT 0?",
        "MEAS:VOLT:ADC? 0": "MEAS:VOLT:ADC 0?",
    }
    for command in ("INSTR:RS485?", "CAL? 0", "MEAS:VOLT? 0", "MEAS:VOLT:ADC? 0"):
        wire_command = legacy_queries.get(command, command) if legacy else command
        connection.write((wire_command + "\n").encode("ascii"))
        response = connection.readline().decode("ascii", errors="replace").strip()
        if not response:
            error = ""
            if queued_errors:
                connection.write(b"SYST:ERR?\n")
                error = connection.readline().decode("ascii", errors="replace").strip()
            raise RuntimeError(f"No response to {command!r}; queued error: {error!r}")
        result["queries"][command] = response
    return result


def snapshot(port, expected_serial, expected_usb_serial=None):
    connection, descriptor, identity = identify(port, expected_serial, expected_usb_serial)
    with connection:
        return read_snapshot(connection, descriptor, identity)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--usb-serial")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = snapshot(args.port, args.serial, args.usb_serial)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
