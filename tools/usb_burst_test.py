"""Repeat USB packet boundaries on the explicitly identified development unit0003."""
import argparse
import json
import time
from datetime import datetime, timezone
from pathlib import Path
from device_snapshot import identify


def run(args):
    connection, descriptor, identity = identify(args.port, "0003", "4977386D3659")
    if descriptor.location != "1-10.4.1.1.1.4":
        connection.close()
        raise RuntimeError("Expected the original hub path")
    checks = 0
    sizes = (1, 31, 32, 63, 64, 65, 127, 128, 191, 192, 198, 199, 200, 201, 4095, 4096, 4097, 8192)
    started = time.monotonic()
    with connection:
        for cycle in range(args.cycles):
            for size in sizes:
                connection.write(b"X" * size + b"\n")
                reply = connection.readline().decode("ascii").strip()
                expected = "ERROR:LINE TOO LONG" if size >= 200 else "ERROR:INVALID COMMAND" if size >= 32 else "ERROR:INVALID ARGUMENTS"
                if reply != expected:
                    raise RuntimeError(f"cycle{cycle}, size{size}: {reply!r} != {expected!r}")
                checks += 1
            print(f"PASS packet cycle {cycle + 1}", flush=True)
        connection.write(b"*IDN?\n" * 100)
        for _ in range(100):
            if connection.readline().decode("ascii").strip() != identity:
                raise RuntimeError("Coalesced identity commands lost framing")
            checks += 1
        print("PASS 100 coalesced commands", flush=True)
        connection.write(b"CAL 0?\n")
        calibration = connection.readline().decode("ascii").strip()
        connection.write(b"INSTR:RS485?\n")
        address = connection.readline().decode("ascii").strip()
    report = {"time_utc": datetime.now(timezone.utc).isoformat(), "identity": identity,
              "usb_serial": descriptor.serial_number, "usb_location": descriptor.location,
              "cycles": args.cycles, "sizes": sizes, "passed_checks": checks,
              "seconds": round(time.monotonic() - started, 3), "calibration": calibration,
              "address": address, "passed": True}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{checks} USB checks passed", flush=True)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", required=True)
    p.add_argument("--cycles", type=int, default=10)
    p.add_argument("--output", type=Path, required=True)
    run(p.parse_args())
