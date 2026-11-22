"""Exercise one identified ADC over USB; RS485 traffic is explicitly opt-in."""

import argparse
import json
import time
import serial
from pathlib import Path

from device_snapshot import identify


def run(args):
    connection, descriptor, identity = identify(args.port, args.serial, args.usb_serial)
    results = []
    with connection:
        healthy = True
        def query(command):
            nonlocal healthy
            try:
                connection.write((command + "\n").encode("ascii"))
                answer = connection.readline().decode("ascii").strip()
            except serial.SerialException:
                healthy = False
                raise
            if not answer:
                healthy = False
                raise RuntimeError(f"No response to {command}")
            return answer

        def check(name, condition):
            if not condition:
                raise RuntimeError(f"Failed: {name}")
            results.append(name)
            print(f"PASS {name}", flush=True)

        calibration = query("CAL 0?")
        address = query("INSTR:RS485?")
        if identity.split(",")[3] != "V2.1.3":
            raise RuntimeError(f"Expected V2.1.3 before regression testing: {identity}")
        try:
            check("four voltage measurements", len([float(v) for v in query("MEAS:VOLT 0?").split(",")]) == 4)
            for command in ("MEAS:VOLT", "MEAS:VOLT -1?", "MEAS:VOLT 256?", "INSTR:RS485:SET 256"):
                check(f"reject {command}", query(command) == "ERROR:INVALID ARGUMENTS")
            for size in (63, 64, 65, 127, 128, 191, 192, 199, 200):
                expected = "ERROR:LINE TOO LONG" if size == 200 else "ERROR:INVALID COMMAND"
                print(f"Testing {size}-byte input", flush=True)
                check(f"{size}-byte input", query("X" * size) == expected)
            check("identity after malformed input", query("*IDN?") == identity)
            if not args.skip_large_burst:
                check("4096-byte discarded frame", query("X" * 4096) == "ERROR:LINE TOO LONG")
                check("identity after discarded burst", query("*IDN?") == identity)

            # A fragment lasting longer than Arduino's former one-second read
            # timeout must remain pending until its newline arrives.
            connection.write(b"INSTR:LED ")
            time.sleep(1.2)
            check("fragment stays pending", connection.in_waiting == 0)
            connection.write(b"1\n")
            check("fragment completion", query("*IDN?") == identity)
            connection.write(b"INSTR:LED 0\n")
            for command in ("INSTR:GAIN 2,0", "INSTR:DR 60,0", "INSTR:MODE 0,0", "INSTR:MODE 1,0"):
                connection.write((command + "\n").encode("ascii"))
                check(f"accept {command}", query("*IDN?") == identity)

            if args.rs485_disconnected:
                start = time.monotonic()
                answer = query("MEAS:VOLT 5?")
                elapsed = time.monotonic() - start
                check("missing RS485 peer times out", answer == "ERROR:RS485 TIMEOUT" and 0.8 <= elapsed < 1.8)
                check("identity after RS485 timeout", query("*IDN?") == identity)
                for channel, maximum in ((5, 185), (41, 184)):
                    command = f"MEAS:VOLT {channel}?"
                    padded = command + " " * (maximum - len(command))
                    check(f"forwarding boundary accepted for CH{channel}", query(padded) == "ERROR:RS485 TIMEOUT")
                    check(f"forwarding boundary rejected for CH{channel}", query(padded + " ") == "ERROR:LINE TOO LONG")
                for channel in (1, 5):
                    check(f"calibration overflow rejected for CH{channel}",
                          query(f"CAL:VOLT {channel},0,0,1e30,1e-30") == "ERROR:INVALID ARGUMENTS")
        finally:
            if healthy:
                connection.write(b"*RST\n")
                connection.flush()
                time.sleep(0.15)
        check("calibration preserved", query("CAL 0?") == calibration)
        check("address preserved", query("INSTR:RS485?") == address)
        check("identity after reset", query("*IDN?") == identity)
    report = {
        "identity": identity, "port": args.port, "usb_serial": descriptor.serial_number,
        "usb_location": descriptor.location, "passed": results,
        "rs485_disconnected_test": args.rs485_disconnected,
        "large_burst_test": not args.skip_large_burst,
        "calibration": calibration, "address": address,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{len(results)} hardware checks passed", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--usb-serial", required=True)
    parser.add_argument("--rs485-disconnected", action="store_true")
    parser.add_argument("--skip-large-burst", action="store_true", help="Run normal/persistence qualification before the separate destructive burst test")
    parser.add_argument("--output", type=Path, required=True)
    run(parser.parse_args())
