"""Qualify silent failures and the queried error FIFO on unit0003 via its hub."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time
import traceback
from device_snapshot import identify
from hardware_stress import Rig
from scpi_errors import parse_error


def run(args):
    report = {"time_utc": datetime.now(timezone.utc).isoformat(), "passed": False, "checks": []}
    started = time.monotonic()
    rig = None
    try:
        rig = Rig(args, report)
        rig.install_canaries()
        for command, code in (
            ("BOGUS", -113), ("MEAS:VOLT 1?", -113), ("INSTR:GAIN 4 1", -102),
            ("INSTR:LED hello", -128), ("INSTR:LED 2", -222), ("MEAS:VOLT? 129", -222),
            ("INSTR:GAIN 3,1", -224), ("CAL:VOLT 1,0,0,4,1", -222),
            ("CAL:VOLT 1,0,nan,2.75,1", -128), ("SYST:BOOT;*IDN?", -102),
            ("SYST:SER:SET 0004", 1003), ("SYST:ERR? 1", -102),
        ):
            rig.query(command, code)
            rig.check_canaries()
            rig.query("SYST:ERR?", '0,"No error"')
            report["checks"].append("silent rejection, correct queried code: " + command)

        # Delay before sending any query: neither failed setters nor failed queries talk.
        for command, expected in (("INSTR:LED 2", -222), ("MEAS:VOLT? 129", -222)):
            rig.send((command + "\n").encode("ascii"))
            time.sleep(0.15)
            if rig.c.in_waiting:
                raise RuntimeError("Unsolicited bytes after " + command)
            if parse_error(rig.query("SYST:ERR?"))[0] != expected:
                raise RuntimeError("Silent rejection was not recorded")
        report["checks"].append("failed setter and failed query remain silent without a following query")

        rig.command("SYST:SER:SET 0003")
        rig.query("SYST:ERR?", '0,"No error"')
        report["checks"].append("repeat provisioning has no OK reply and preserves identity")

        commands = ("BOGUS", "INSTR:LED hello", "INSTR:LED 2", "INSTR:GAIN 3,1")
        codes = (-113, -128, -222, -224)
        rig.send(("\n".join(commands[i % 4] for i in range(50)) + "\n*IDN?\n").encode("ascii"))
        if rig.receive() != rig.identity:
            raise RuntimeError("Error flood generated unsolicited bytes")
        actual = [parse_error(rig.query("SYST:ERR?"))[0] for _ in range(9)]
        if actual != [codes[i % 4] for i in range(7)] + [-350, 0]:
            raise RuntimeError("Wrong FIFO/overflow policy: " + repr(actual))
        report["checks"].append("eight-entry FIFO preserves oldest seven and reports overflow once")

        rig.command("INSTR:GAIN 4,0")
        rig.send(b"BOGUS\n*CLS\n*IDN?\n")
        if rig.receive() != rig.identity:
            raise RuntimeError("Clear emitted output")
        rig.query("SYST:ERR?", '0,"No error"')
        rig.check_canaries()
        rig.query("INSTR:GAIN? 0", "4,4,4,4")
        report["checks"].append("CLS clears queue without changing gain, calibration, address or serial")

        rig.command("BOGUS")
        rig.c.close()
        rig.c, descriptor, identity = identify(args.port, "0003", "4977386D3659")
        if descriptor.location != "1-10.4.1.1.1.4" or identity != rig.identity:
            raise RuntimeError("Target changed across port reopen")
        rig.c.timeout = 0.1
        rig.query("SYST:ERR?", '-113,"Undefined header"')
        rig.check_canaries()
        report["checks"].append("error queue and RAM canaries survive USB close/open")

        rig.command("INSTR:LED 2")
        rig.command("*RST")
        rig.query("SYST:ERR?", '-222,"Data out of range"')
        rig.query("CAL? 0", rig.baseline_cal)
        rig.query("INSTR:RS485?", rig.baseline_address)
        report["checks"].append("RST reloads settings without clearing errors")

        rig.install_canaries()
        rig.command("INSTR:RS485:SET 0")
        rig.address = "0"
        then = time.monotonic()
        rig.query("MEAS:VOLT? 5", 1101)
        report["forward_timeout_seconds"] = round(time.monotonic() - then, 3)
        rig.check_canaries()
        report["checks"].append("missing RS485 peer produces only a queried timeout error")
        report["raw_volts"] = rig.vector("MEAS:VOLT:ADC? 0")
        rig.restore()
        report["restored_volts"] = rig.vector("MEAS:VOLT? 0")
        rig.query("SYST:ERR?", '0,"No error"')
        report["checks"].append("four actual inputs read and saved settings restored with empty queue")
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
        print(f"{len(report['checks'])} queued-error checks; passed={report['passed']}", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--version", default="V5.0.0")
    parser.add_argument("--output", type=Path, required=True)
    run(parser.parse_args())
