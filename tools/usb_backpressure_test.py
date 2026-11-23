"""Exercise unread USB replies and recovery on identified development unit0003 only."""
import argparse
import json
import time
from datetime import datetime, timezone
from pathlib import Path
import serial
from device_snapshot import identify


def run(args):
    c, d, identity = identify(args.port, "0003", "4977386D3659")
    if d.location != "1-10.4.1.1.1.4":
        c.close()
        raise RuntimeError("Wrong hub path")
    started = time.monotonic()
    report = {"time_utc": datetime.now(timezone.utc).isoformat(), "identity": identity,
              "usb_serial": d.serial_number, "usb_location": d.location, "requests": 5000}
    with c:
        c.write_timeout = 1
        try:
            c.write(b"*IDN?\n" * report["requests"])
            report["write_completed"] = True
        except serial.SerialTimeoutException:
            report["write_completed"] = False  # An unread host may legitimately backpressure writes.
        time.sleep(0.3)
        c.dtr = False
        c.reset_output_buffer()
        c.reset_input_buffer()
        time.sleep(0.2)
        c.dtr = True
        c.timeout = 0.1
        # Terminate any cancelled partial request, then look for a new, unique reply.
        c.write(b"\nINSTR:RS485?\n")
        deadline = time.monotonic() + 3
        recovered = False
        while time.monotonic() < deadline:
            answer = c.readline().decode("ascii", errors="replace").strip()
            if answer == "0":
                recovered = True
                break
        if not recovered:
            raise RuntimeError("Fresh address query did not recover after unread replies")
        c.write(b"*IDN?\n")
        if c.readline().decode("ascii").strip() != identity:
            raise RuntimeError("Fresh identity lost framing after recovery")
        c.write(b"CAL? 0\n")
        report["calibration"] = c.readline().decode("ascii").strip()
        c.write(b"MEAS:VOLT? 0\n")
        report["volts"] = [float(v) for v in c.readline().decode("ascii").strip().split(",")]
        if len(report["volts"]) != 4:
            raise RuntimeError("Measurement recovery failed")
    report.update(passed=True, seconds=round(time.monotonic() - started, 3))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", required=True)
    p.add_argument("--output", type=Path, required=True)
    run(p.parse_args())
