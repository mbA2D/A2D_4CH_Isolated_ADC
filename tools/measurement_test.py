"""End-to-end readings and compatibility checks on one explicitly identified ADC.

Inputs must be unconnected for this procedure: it temporarily changes gain/rate,
conversion mode, and RAM calibration, then restores stored settings with *RST.
It does not write EEPROM or claim absolute accuracy without a reference source.
"""
import argparse
import json
import math
import statistics
import time
from datetime import datetime, timezone
from pathlib import Path

from device_snapshot import identify


def run(args):
    connection, descriptor, identity = identify(args.port, args.serial, args.usb_serial)
    report = {
        "time_utc": datetime.now(timezone.utc).isoformat(),
        "identity": identity, "usb_serial": descriptor.serial_number,
        "usb_location": descriptor.location, "port": args.port,
        "input_condition": "all four inputs unconnected, confirmed by user",
        "checks": [], "samples": [], "configuration_sweep": [],
    }
    if descriptor.location != args.usb_location:
        connection.close()
        raise RuntimeError("USB location differs from the explicitly selected hub path")

    with connection:
        def query(command):
            connection.write((command + "\n").encode("ascii"))
            answer = connection.readline().decode("ascii").strip()
            if not answer:
                raise RuntimeError(f"No response to {command}")
            return answer

        def command(text):
            connection.write((text + "\n").encode("ascii"))
            if query("*IDN?") != identity:
                raise RuntimeError(f"Unexpected response after {text}")

        def check(name, condition):
            if not condition:
                raise RuntimeError(f"Failed: {name}")
            report["checks"].append(name)
            print(f"PASS {name}", flush=True)

        def vector(text, count):
            values = [float(item) for item in text.split(",")]
            if len(values) != count or not all(map(math.isfinite, values)):
                raise RuntimeError(f"Invalid numeric response {text!r}")
            return values

        baseline_cal = query("CAL 0?")
        baseline_address = query("INSTR:RS485?")
        report["calibration_before"] = baseline_cal
        report["address_before"] = baseline_address
        try:
            # Reset restores the documented gain=1, rate=15 SPS, continuous defaults.
            command("*RST")
            time.sleep(0.2)
            for i in range(args.samples):
                started = time.monotonic()
                raw = query("MEAS:VOLT:ADC 0?")
                calibrated = query("MEAS:VOLT 0?")
                report["samples"].append({
                    "time_utc": datetime.now(timezone.utc).isoformat(),
                    "raw_text": raw, "voltage_text": calibrated,
                    "raw_volts": vector(raw, 4), "volts": vector(calibrated, 4),
                    "query_seconds": round(time.monotonic() - started, 6),
                })
                time.sleep(0.1)
            check("default four-channel samples are finite", len(report["samples"]) == args.samples)

            for gain in (1, 2, 4, 8):
                command(f"INSTR:GAIN {gain},0")
                for rate in (15, 60, 240):
                    command(f"INSTR:DR {rate},0")
                    time.sleep(0.2)
                    raw = vector(query("MEAS:VOLT:ADC 0?"), 4)
                    calibrated = vector(query("MEAS:VOLT 0?"), 4)
                    singles = []
                    for ch in range(1, 5):
                        singles.append({"channel": ch,
                                        "raw": vector(query(f"MEAS:VOLT:ADC {ch}?"), 1)[0],
                                        "volts": vector(query(f"MEAS:VOLT {ch}?"), 1)[0]})
                    report["configuration_sweep"].append({"gain": gain, "rate_sps": rate,
                                                           "raw_volts": raw, "volts": calibrated,
                                                           "individual_channels": singles})
            check("all gain/rate combinations return group and individual readings", len(report["configuration_sweep"]) == 12)

            command("*RST")
            command("INSTR:MODE 0,0")
            command("*TRG")
            time.sleep(0.2)
            report["single_shot_volts"] = vector(query("MEAS:VOLT 0?"), 4)
            check("single-shot trigger returns four readings", True)
            command("INSTR:MODE 1,0")
            time.sleep(0.2)

            # Exercise host point order, formatting, and calibration application in RAM.
            command("CAL:VOLT 1,0,-0.125,2.75,0.875")
            check("legacy two-point calibration order and format", query("CAL 1?") == "-0.12500,2.75000")
            raw = vector(query("MEAS:VOLT:ADC 1?"), 1)[0]
            measured = vector(query("MEAS:VOLT 1?"), 1)[0]
            report["temporary_calibration"] = {"raw_volts": raw, "volts": measured,
                                                 "expected_from_displayed_raw": (raw + 0.125) * 2.75}
            # Queries are separate conversions, each rounded to 4 decimals.
            check("calibration affects actual ADC reading", abs(measured - (raw + 0.125) * 2.75) < 0.002)
            check("nonfinite calibration rejected", query("CAL:VOLT 1,0,nan,5,1") == "ERROR:INVALID ARGUMENTS")
            check("invalid calibration preserves current coefficients", query("CAL 1?") == "-0.12500,2.75000")

            # The next command must wait behind the missing-peer response, not overtake it.
            start = time.monotonic()
            connection.write(b"MEAS:VOLT 5?\n*IDN?\n")
            first = connection.readline().decode("ascii").strip()
            second = connection.readline().decode("ascii").strip()
            report["forward_timeout_seconds"] = round(time.monotonic() - start, 6)
            check("asynchronous forwarding keeps response order", first == "ERROR:RS485 TIMEOUT" and second == identity)
        finally:
            command("*RST")
            time.sleep(0.2)
            restored_cal = query("CAL 0?")
            restored_address = query("INSTR:RS485?")
            report["calibration_after"] = restored_cal
            report["address_after"] = restored_address
            check("stored calibration restored exactly", restored_cal == baseline_cal)
            check("stored RS485 address preserved", restored_address == baseline_address)

    report["channels"] = []
    for ch in range(4):
        values = [sample["volts"][ch] for sample in report["samples"]]
        raw_values = [sample["raw_volts"][ch] for sample in report["samples"]]
        report["channels"].append({"channel": ch + 1, "count": len(values),
                                   "mean_volts": statistics.mean(values), "min_volts": min(values),
                                   "max_volts": max(values), "stddev_volts": statistics.pstdev(values),
                                   "mean_raw_volts": statistics.mean(raw_values)})
    report["passed"] = True
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["channels"], indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--usb-serial", required=True)
    parser.add_argument("--usb-location", required=True)
    parser.add_argument("--inputs-unconnected", required=True, action="store_true")
    parser.add_argument("--rs485-disconnected", required=True, action="store_true")
    parser.add_argument("--samples", type=int, default=60)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.samples < 2:
        parser.error("--samples must be at least 2")
    run(args)
