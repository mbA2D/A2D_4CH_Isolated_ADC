"""V2 persistence/query validation on identified unit0003 only; RS485 must be disconnected."""
import argparse
import json
import time
from datetime import datetime, timezone
from pathlib import Path
from device_snapshot import identify
from scpi_errors import expect_error


def run(args):
    connection, descriptor, identity = identify(args.port, "0003", "4977386D3659")
    if descriptor.location != "1-10.4.1.1.1.4" or identity.split(",")[-1] != "V5.0.0":
        connection.close()
        raise RuntimeError("Expected V5.0.0 on unit0003 through the original hub")
    checks = []
    def check(name, condition):
        if not condition:
            raise RuntimeError("Failed: " + name)
        checks.append(name)
        print("PASS " + name, flush=True)
    factory = ",".join(["0.00000,2.81818"] * 4)
    with connection:
        connection.write(b"*CLS\n")
        def query(text):
            connection.write((text + "\n").encode("ascii"))
            reply = connection.readline().decode("ascii").strip()
            if not reply:
                raise RuntimeError("No response: " + text)
            return reply
        def command(text):
            connection.write((text + "\n").encode("ascii"))
            check("completed " + text, query("*IDN?") == identity)
        check("nominal V2 calibration before persistence test", query("CAL? 0") == factory)
        check("base address before persistence test", query("INSTR:RS485?") == "0")
        try:
            command("CAL:VOLT 1,0,-0.125,2.75,0.875")
            check("RAM calibration applied", query("CAL? 1") == "-0.12500,2.75000")
            check("reset query rejected", expect_error(connection, identity, "CAL:RESET 1?", -102) == -102)
            check("calibration unchanged by rejected query", query("CAL? 1") == "-0.12500,2.75000")
            check("save query rejected", expect_error(connection, identity, "CAL:SAVE 0?", -102) == -102)
            check("address query on setter rejected", expect_error(connection, identity, "INSTR:RS485:SET 7?", -102) == -102)
            check("address unchanged by rejected query", query("INSTR:RS485?") == "0")
            command("CAL:SAVE 1")
            command("*RST")
            check("calibration persisted after reset", query("CAL? 1") == "-0.12500,2.75000")
            command("INSTR:RS485:SET 7")
            command("INSTR:RS485:SAVE")
            command("*RST")
            check("address persisted after reset", query("INSTR:RS485?") == "7")
        finally:
            # Restore the nominal V2 state explicitly; never reconstruct floats from lossy display values.
            command("CAL:RESET 0")
            command("CAL:SAVE 0")
            command("INSTR:RS485:SET 0")
            command("INSTR:RS485:SAVE")
            command("*RST")
        check("nominal calibration restored and persisted", query("CAL? 0") == factory)
        check("base address restored and persisted", query("INSTR:RS485?") == "0")
    report = {"time_utc": datetime.now(timezone.utc).isoformat(), "identity": identity,
              "usb_serial": descriptor.serial_number, "usb_location": descriptor.location,
              "checks": checks, "calibration": factory, "address": "0", "passed": True}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", type=Path, required=True)
    run(parser.parse_args())
