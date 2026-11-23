"""Run isolated baud builds on the two identified development units, then restore.

Waits for the independently running 15-minute standard-firmware baseline. The
original100ms timeout variants at9600/19200 are diagnostic negative cases.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
import traceback

from serial.tools import list_ports
from device_snapshot import identify, read_snapshot
from rs485_link_test import Test, TARGETS


def run(args):
    root = Path(__file__).resolve().parents[1]
    python = Path(sys.executable)
    report = {"time_utc": datetime.now(timezone.utc).isoformat(), "complete": False, "rates": []}
    events = args.output / "events.jsonl"
    args.output.mkdir(parents=True, exist_ok=True)
    attempt = ""
    if args.resume:
        previous_path = args.output / "sweep-summary.json"
        previous = json.loads(previous_path.read_text(encoding="utf-8"))
        if previous["complete"]:
            raise RuntimeError("Qualification already completed")
        attempt = "resume-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        archive = args.output / (attempt + "-stopped-summary.json")
        shutil.copy2(previous_path, archive)
        report["rates"] = previous["rates"]
        report["previous_attempts"] = previous.get("previous_attempts", []) + [str(archive)]
        previous_path.unlink()
    completed_versions = {item["version"] for item in report["rates"]}

    def event(stage, **data):
        row = {"time_utc": datetime.now(timezone.utc).isoformat(), "stage": stage, **data}
        with events.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(row) + "\n")
        print(json.dumps(row), flush=True)

    def update(number, version, binary, label):
        uid, location = TARGETS[number]
        matches = [p for p in list_ports.comports() if p.serial_number == uid]
        if len(matches) != 1 or matches[0].location != location:
            raise RuntimeError(f"Unit{number}: unexpected USB identity/path")
        folder = args.output / "uploads" / (f"{attempt}-{label}" if attempt else label) / number
        folder.mkdir(parents=True, exist_ok=True)
        local_binary = folder / "firmware.bin"
        shutil.copy2(binary, local_binary)
        event("upload_started", unit=number, version=version)
        command = [str(python), str(root / "tools/usb_upload.py"), "--port", matches[0].device,
                   "--serial", number, "--usb-serial", uid, "--version", version, "--binary", str(local_binary)]
        with (folder / "upload.log").open("w", encoding="utf-8") as stream:
            subprocess.run(command, cwd=root, stdout=stream, stderr=subprocess.STDOUT, check=True, timeout=100)
        event("upload_verified", unit=number, version=version)

    def upload_pair(version, binary, label):
        # Never flash both simultaneously: the Maple writer must see one DFU unit.
        for number in ("0003", "0001"):
            update(number, version, binary, label)

    def negative_probe(variant, folder):
        folder.mkdir(parents=True, exist_ok=True)
        test = Test(folder, variant["version"])
        result = {"baud": variant["baud"], "version": variant["version"], "expected_limitation": "100ms UART timeout on a 200-byte frame",
                  "limitation_reproduced": False, "restored": False}
        try:
            test.open()
            test.command("0003", "INSTR:RS485:SET 1")
            test.command("0001", "INSTR:RS485:SET 0")
            command = "INSTR:GAIN? 5".ljust(185)
            try:
                test.forwarded("0001", command)
            except RuntimeError as error:
                result["observed"] = str(error)
                if '1102,"RS485 write failed"' not in str(error):
                    raise
                result["limitation_reproduced"] = True
            result["wire_frame_bytes"] = 200
            result["wire_time_ms"] = 200 * 10 * 1000 / variant["baud"]
        finally:
            try:
                test.restore_settings()
                result["restored"] = True
            finally:
                for unit in test.units.values():
                    unit["connection"].dtr = False
                    unit["connection"].close()
                test.trace.close()
                (folder / "summary.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        return result

    def standard_restore():
        event("restoring_standard_firmware")
        upload_pair("V5.0.0", args.output / "standard-V5.0.0.bin", "final-standard")
        final = {}
        for number, (uid, location) in TARGETS.items():
            matches = [p for p in list_ports.comports() if p.serial_number == uid]
            if len(matches) != 1 or matches[0].location != location:
                raise RuntimeError("Target moved during restoration")
            c, descriptor, identity = identify(matches[0].device, number, uid)
            with c:
                if not identity.endswith(",V5.0.0"):
                    raise RuntimeError("Normal firmware did not return")
                final[number] = read_snapshot(c, descriptor, identity)
                c.write(b"INSTR:GAIN? 0\nINSTR:DR? 0\nINSTR:MODE? 0\nSYST:ERR?\n")
                values = [c.readline().decode("ascii").strip() for _ in range(4)]
                if values != ["1,1,1,1", "15,15,15,15", "1,1,1,1", '0,"No error"']:
                    raise RuntimeError(f"Unit{number}: unexpected restored settings {values!r}")
                final[number]["ADC_configuration_and_errors"] = values
                c.dtr = False
        return final

    try:
        event("waiting_for_baseline")
        deadline = time.monotonic() + 1200
        while not args.baseline.is_file():
            if time.monotonic() > deadline:
                raise RuntimeError("Baseline did not finish")
            time.sleep(1)
        baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
        if not baseline["passed"] or not baseline["restored"]:
            raise RuntimeError("Baseline failed; no baud builds will be installed")
        report["baseline"] = baseline
        variants = json.loads(args.variants.read_text(encoding="utf-8"))
        if len(variants) != 9:
            raise RuntimeError("Nine verified variant builds are required")
        for variant in variants:
            binary = Path(variant["binary"])
            if hashlib.sha256(binary.read_bytes()).hexdigest() != variant["sha256"]:
                raise RuntimeError("Variant checksum mismatch")
            label = variant["version"]
            if label in completed_versions:
                continue
            folder = args.output / "rates" / label
            folder.mkdir(parents=True, exist_ok=True)
            upload_pair(label, binary, label)
            if variant["baud"] in (9600, 19200) and not variant["adaptive_timeout"]:
                result = negative_probe(variant, folder / "negative-long-frame")
                result.update(adaptive_timeout=False, diagnostic_only=True)
                report["rates"].append(result)
                event("diagnostic_case_completed", **result)
                continue
            result = dict(variant)
            try:
                event("rate_test_started", baud=variant["baud"], version=label)
                with (folder / "functional.log").open("w", encoding="utf-8") as stream:
                    subprocess.run([str(python), str(root / "tools/rs485_link_test.py"), "--cycles", "24",
                                    "--version", label, "--output", str(folder / "functional")],
                                   cwd=root, check=True, stdout=stream, stderr=subprocess.STDOUT, timeout=180)
                with (folder / "stress.log").open("w", encoding="utf-8") as stream:
                    subprocess.run([str(python), str(root / "tools/rs485_stress_test.py"), "--seconds", str(args.rate_seconds),
                                    "--version", label, "--baud", str(variant["baud"]), "--output", str(folder / "stress")],
                                   cwd=root, check=True, stdout=stream, stderr=subprocess.STDOUT, timeout=args.rate_seconds + 180)
                result["passed"] = True
            except subprocess.CalledProcessError as error:
                result.update(passed=False, failure=str(error))
                # A measured baud limit is a result; fresh builds clear partial RS485 input.
                # Identity/calibration restoration is still required before finishing.
            report["rates"].append(result)
            (args.output / "sweep-progress.json").write_text(json.dumps(report["rates"], indent=2) + "\n", encoding="utf-8")
            event("rate_test_finished", baud=variant["baud"], version=label, passed=result["passed"])

        report["final_after_upload"] = standard_restore()
        event("standard_soak_started", seconds=args.soak_seconds, baud=57600)
        with (args.output / "final-soak.log").open("w", encoding="utf-8") as stream:
            subprocess.run([str(python), str(root / "tools/rs485_stress_test.py"), "--seconds", str(args.soak_seconds),
                            "--baud", "57600", "--version", "V5.0.0", "--output", str(args.output / "final-soak-57600-30min")],
                           cwd=root, check=True, stdout=stream, stderr=subprocess.STDOUT, timeout=args.soak_seconds + 180)
        report["final_soak"] = json.loads((args.output / "final-soak-57600-30min/summary.json").read_text(encoding="utf-8"))
        report["complete"] = True
        event("qualification_complete")
    except BaseException as error:
        report.update(error=str(error), traceback=traceback.format_exc())
        raise
    finally:
        if not report["complete"]:
            try:
                report["emergency_standard_restoration"] = standard_restore()
            except BaseException as error:
                report["restoration_error"] = str(error)
        (args.output / "sweep-summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--baseline", type=Path, required=True)
    p.add_argument("--variants", type=Path, required=True)
    p.add_argument("--rate-seconds", type=int, default=60)
    p.add_argument("--soak-seconds", type=int, default=1800)
    p.add_argument("--resume", action="store_true", help="Preserve previous results and run only unattempted variants")
    run(p.parse_args())
