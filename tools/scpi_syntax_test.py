"""Check the V4 grammar on identified unit0003 through its original USB hub.

Uses volatile canaries to detect unintended actions/resets and restores stored
settings on success. Inputs are unconnected; RS485 terminals are disconnected.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time
import traceback

from hardware_stress import Rig


def run(args):
    report = {"time_utc": datetime.now(timezone.utc).isoformat(), "passed": False,
              "checks": [], "rejected": []}
    started = time.monotonic()
    rig = None
    try:
        rig = Rig(args, report)
        rig.install_canaries()
        rig.command("INSTR:GAIN\t4 \t, \t0 \r")
        rig.command("INSTR:DR 60 , 0")
        rig.command("INSTR:MODE 1, 0")
        rig.query("INSTR:GAIN? 0", "4,4,4,4")
        rig.query("INSTR:DR? 0", "60,60,60,60")
        rig.query("INSTR:MODE? 0", "1,1,1,1")
        rig.command("CAL:VOLT 1 , +0 , .125 , 2.75e0 , 1.125")
        rig.check_canaries()
        report["checks"].append("commas allow surrounding whitespace and valid numeric tokens")

        invalid = (
            "MEAS:VOLT 0?", "MEAS:VOLT 0", "MEAS:VOLT:ADC 0?", "MEAS:VOLT:ADC 0",
            "CAL 0?", "CAL 0", "MEAS:VOLT?0", "MEAS:VOLT? 0?", "CAL ?0",
            "INSTR:GAIN 4 0", "INSTR:GAIN 4\t0", "INSTR:GAIN,4,0",
            "INSTR:GAIN ,4,0", "INSTR:GAIN 4,,0", "INSTR:GAIN 4, ,0",
            "INSTR:GAIN 4,0,", "INSTR:GAIN 4,0, \t\r", "INSTR:GAIN 4,0?",
            "INSTR:DR 15 0", "INSTR:MODE ,0,0", "INSTR:RS485:SET ,0,",
            "CAL:RESET ,0,", "CAL:SAVE ,0,", "CAL:VOLT 1,0,,0,2.75,1",
            "CAL:VOLT 1 0 0 2.75 1", "SYST:SER:SET ,0003,", "SYST:BOOT ,",
            "SYST:BOOT;", "*RST;", "CAL:RESET 0;*IDN?",
            "INSTR:GAIN 8,0;INSTR:GAIN? 0", "INSTR:GAIN 8,0;",
            "INSTR:RS485:SET 0;*IDN?", "*IDN?;*RST", "INSTR:GAIN? 0;",
            "meas:volt? 0", "Meas:Volt? 0", "instr:gain? 0", ":MEAS:VOLT? 0",
            "MEAS:VOLT? ,0", "MEAS:VOLT? 0,", "CAL? 0,,", "*IDN? ,",
        )
        for command in invalid:
            rig.query(command, (-102, -113, -128, -222, -224))
            rig.check_canaries()
            rig.query("INSTR:GAIN? 0", "4,4,4,4")
            rig.query("INSTR:DR? 0", "60,60,60,60")
            rig.query("INSTR:MODE? 0", "1,1,1,1")
            report["rejected"].append(command)
        report["checks"].append("malformed queries/separators/compounds/case leave RAM canaries and ADC settings intact")

        rig.query(" \t*IDN? \t\r", rig.identity)
        rig.query(" \tCAL?\t0 \r", rig.marker_all)
        report["readings"] = {
            "raw": rig.vector("MEAS:VOLT:ADC? 0"),
            "calibrated_with_canary": rig.vector("MEAS:VOLT? 0"),
        }
        report["checks"].append("canonical measurement/calibration/configuration queries return complete numeric replies")

        rig.send(b"MEAS:VOLT")
        time.sleep(0.1)
        if rig.c.in_waiting:
            raise RuntimeError("Partial query header executed before LF")
        rig.send(b"?\t0\n")
        if len(rig.receive().split(",")) != 4:
            raise RuntimeError("Fragmented canonical query failed")
        rig.send(b"*IDN?\nINSTR:GAIN? 0\nCAL? 0\n")
        for expected in (rig.identity, "4,4,4,4", rig.marker_all):
            if rig.receive() != expected:
                raise RuntimeError("Separate coalesced command lines lost reply ordering")
        report["checks"].append("fragmented headers and multiple separate LF lines retain framing and reply order")

        # Only address0 is a USB-to-RS485 master. Keep the calibration canary.
        rig.command("INSTR:RS485:SET 0")
        rig.address = "0"
        rig.check_canaries()
        then = time.monotonic()
        rig.query("MEAS:VOLT? 5", 1101)
        report["forwarded_query_seconds"] = round(time.monotonic() - then, 3)
        rig.query("MEAS:VOLT 5?", (-102, -113, -128, -222, -224))
        rig.check_canaries()
        report["checks"].append("new query syntax routes to RS485; old syntax is rejected locally")

        rig.restore()
        report["readings"]["restored"] = rig.vector("MEAS:VOLT? 0")
        rig.query("INSTR:GAIN? 0", "1,1,1,1")
        rig.query("INSTR:DR? 0", "15,15,15,15")
        rig.query("INSTR:MODE? 0", "1,1,1,1")
        report["checks"].append("stored calibration/address/identity preserved and ADC defaults restored")
        report["passed"] = True
    except BaseException as error:
        report.update(error=str(error), traceback=traceback.format_exc())
        raise
    finally:
        report["seconds"] = round(time.monotonic() - started, 3)
        if rig is not None:
            report["counts"] = dict(rig.counts)
            report["last_transactions"] = list(rig.trace)
            rig.c.close()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"{len(report['checks'])} grammar checks, {len(report['rejected'])} rejected commands; passed={report['passed']}", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--version", default="V5.0.0")
    parser.add_argument("--output", type=Path, required=True)
    run(parser.parse_args())
