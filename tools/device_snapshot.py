"""Read one explicitly selected ADC; stop immediately if its identity differs."""

import argparse
import json
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


def snapshot(port, expected_serial, expected_usb_serial=None):
    connection, descriptor, identity = identify(port, expected_serial, expected_usb_serial)
    with connection:
        result = {
            "time_utc": datetime.now(timezone.utc).isoformat(),
            "port": port,
            "usb_serial": descriptor.serial_number,
            "usb_location": descriptor.location,
            "identity": identity,
            "queries": {},
        }
        for command in ("INSTR:RS485?", "CAL 0?", "MEAS:VOLT 0?", "MEAS:VOLT:ADC 0?"):
            connection.write((command + "\n").encode("ascii"))
            response = connection.readline().decode("ascii", errors="replace").strip()
            if not response:
                raise RuntimeError(f"No response to {command!r}")
            result["queries"][command] = response
        return result


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
