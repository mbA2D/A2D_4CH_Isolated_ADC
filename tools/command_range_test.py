"""Command acceptance/rejection checks on unit0003 through its original hub only."""
import argparse
import json
import math
from pathlib import Path
import struct
import time

from hardware_stress import Rig


def f32(value):
    return struct.unpack("f", struct.pack("f", value))[0]


def run(args):
    report = {"passed": False, "checks": []}
    rig = None
    started = time.monotonic()
    try:
        rig = Rig(args, report)
        factory = ",".join(["0.00000,2.81818"] * 4)
        if rig.baseline_cal != factory or rig.baseline_address != "0":
            raise RuntimeError("This test restores known nominal development-unit settings")
        rig.install_canaries()
        rig.command("INSTR:RS485:SET 0")
        rig.address = "0"
        invalid_points = (
            "-0.0001,0,5,2", "0,0,5.0001,2", "0,0.23,5,2.0481",
            "0,-2.0481,5,2", "0,0,2,1", "0,0,4,1", "0,1,2.5,0",
            "0,0,2.5,1", "0,0,3.2,1",
            "0,0.2501,2.75,1.2501", "0,-0.2501,2.75,0.7499",
            "0,0,0.0028,0.0009999", "0,0,1e20,1", "0,0,0.0028,1e-20",
            "0,0,5,0", "0,0,0,1", "nan,0,5,2", "0,inf,5,2",
        )
        for channel in (0, 1, 4, 5, 128, 129):
            for points in invalid_points:
                line = f"CAL:VOLT {channel},{points}"
                rig.query(line, (-102, -113, -128, -222, -224))
                rig.check_canaries()
                report["checks"].append("rejected without changing calibration/address: " + line)

        invalid = (
            "INSTR:LED -1", "INSTR:LED 2", "INSTR:LED 256",
            "INSTR:GAIN 0,0", "INSTR:GAIN 3,0", "INSTR:GAIN 9,0", "INSTR:GAIN 256,0",
            "INSTR:DR 0,0", "INSTR:DR 14,0", "INSTR:DR 16,0", "INSTR:DR 59,0",
            "INSTR:DR 61,0", "INSTR:DR 239,0", "INSTR:DR 241,0",
            "INSTR:MODE -1,0", "INSTR:MODE 2,0", "INSTR:MODE 256,0",
            "INSTR:RS485:SET -1", "INSTR:RS485:SET 32", "INSTR:RS485:SET 256",
            "MEAS:VOLT? -1", "MEAS:VOLT? 129", "MEAS:VOLT:ADC? 256",
            "CAL? -1", "CAL:RESET 129", "CAL:SAVE 256", "INSTR:GAIN 2,129",
        )
        for line in invalid:
            rig.query(line, (-102, -113, -128, -222, -224))
            rig.check_canaries()
            report["checks"].append("rejected: " + line)

        for line in ("INSTR:LED 0", "INSTR:LED 1", "INSTR:GAIN 1,0", "INSTR:GAIN 2,0",
                     "INSTR:GAIN 4,0", "INSTR:GAIN 8,0", "INSTR:DR 15,0",
                     "INSTR:DR 60,0", "INSTR:DR 240,0", "INSTR:MODE 0,0", "*TRG",
                     "INSTR:MODE 1,0"):
            rig.command(line)
            rig.vector("MEAS:VOLT:ADC? 0")
            report["checks"].append("accepted: " + line)
        for address in (31, 0):
            rig.command(f"INSTR:RS485:SET {address}")
            rig.address = str(address)
            rig.query("INSTR:RS485?", str(address))
            if address == 31:
                for channel in (0, 1, 4, 125, 128):
                    answer = [float(v) for v in rig.query(f"MEAS:VOLT? {channel}").split(",")]
                    if not all(math.isfinite(v) for v in answer):
                        raise RuntimeError("Bad channel-boundary measurement")
                    report["checks"].append(f"accepted local/group/global channel {channel}")
            report["checks"].append(f"accepted address {address}")

        nominal = f32(2.818181818)
        lower, upper = f32(nominal * f32(0.9)), f32(nominal * f32(1.1))
        valid = (
            ("0,0,5,1.8", 0, 5/1.8), ("5,1.8,0,0", 0, 5/1.8),
            (f"0,0,{lower:.9g},1", 0, lower), (f"0,0,{upper:.9g},1", 0, upper),
            ("0,0.25,2.75,1.25", 0.25, 2.75), ("0,-0.25,2.75,0.75", -0.25, 2.75),
            ("0,0,0.0028,0.001", 0, 2.8), ("0,0.23,5,2.048", 0.23, 5/(2.048-0.23)),
        )
        for points, offset, gain in valid:
            rig.command("CAL:VOLT 1," + points)
            values = [float(v) for v in rig.query("CAL? 1").split(",")]
            if len(values) != 2 or abs(values[0] - offset) > 0.00001 or abs(values[1] - gain) > 0.00001:
                raise RuntimeError(f"Wrong accepted calibration: {points}: {values}")
            report["checks"].append("accepted calibration boundary: " + points)

        # Save a usable value, then prove rejected updates cannot change the next save/reload.
        rig.install_canaries()
        rig.command("CAL:SAVE 1")
        rig.query("CAL:VOLT 1,0,0,1e20,1", (-102, -113, -128, -222, -224))
        rig.command("CAL:SAVE 1")
        rig.command("*RST")
        rig.query("CAL? 1", rig.marker)
        report["checks"].append("rejected calibration cannot replace persisted valid calibration")
        # Reconstruct known defaults rather than importing rounded display coefficients.
        rig.command("CAL:RESET 0")
        rig.command("CAL:SAVE 0")
        rig.command("INSTR:RS485:SET 0")
        rig.command("*RST")
        rig.query("CAL? 0", factory)
        rig.query("INSTR:RS485?", "0")
        report["checks"].append("nominal stored calibration/address restored")
        report["passed"] = True
    finally:
        report["seconds"] = round(time.monotonic() - started, 3)
        if rig is not None:
            report["counts"] = dict(rig.counts)
            report["last_transactions"] = list(rig.trace)
            rig.c.close()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
        print(f"{len(report['checks'])} command-range checks; passed={report['passed']}", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--version", default="V5.0.0")
    parser.add_argument("--output", type=Path, required=True)
    run(parser.parse_args())
