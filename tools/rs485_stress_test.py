"""Timed, single-master RS485 stress on the identified hub-connected units.

Runs each master role for half the requested active duration. Restores both
addresses and ADC settings. Calibration and saved addresses are never changed.
"""
import argparse
from collections import deque
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import time
import traceback

import serial
from rs485_link_test import Test, HEADERS, close_units
from scpi_errors import parse_error


class Stress(Test):
    def __init__(self, output, version):
        super().__init__(output, version)
        self.tail = deque(maxlen=80)
        self.digest = hashlib.sha256()
        self.records = 0

    def record(self, **entry):
        self.records += 1
        line = json.dumps(entry, separators=(",", ":")) + "\n"
        self.digest.update(line.encode("utf-8"))
        self.tail.append(entry)
        if self.records <= 150 or self.records % 500 == 0 or entry.get("missing_reply") or entry.get("event") or "backpressure_write_completed" in entry:
            self.trace.write(line)
            self.trace.flush()

    def prepare(self, master, peer):
        self.command(peer, "INSTR:RS485:SET 1")
        self.command(master, "INSTR:RS485:SET 0")
        self.canaries = {}
        for number, gains, rate in ((master, [1, 2, 4, 8], 60), (peer, [8, 4, 2, 1], 240)):
            for local, gain in enumerate(gains, 1):
                self.command(number, f"INSTR:GAIN {gain},{local}")
            self.command(number, f"INSTR:DR {rate},0")
            self.command(number, "INSTR:MODE 1,0")
            self.canaries[number] = {"GAIN": list(map(str, gains)), "DR": [str(rate)] * 4, "MODE": ["1"] * 4}
        self.check_canaries(master, peer)

    def check_canaries(self, master, peer):
        for number, address in ((master, "0"), (peer, "1")):
            for name, values in self.canaries[number].items():
                if self.query(number, f"INSTR:{name}? 0") != ",".join(values):
                    raise RuntimeError(f"Unit{number}: ADC canary changed; possible reset/wrong action")
            if self.query(number, "INSTR:RS485?") != address:
                raise RuntimeError("Address canary changed")
            if self.query(number, "CAL? 0") != self.units[number]["before"]["queries"]["CAL? 0"]:
                raise RuntimeError("Calibration changed")
            if self.query(number, "*IDN?") != self.units[number]["before"]["identity"]:
                raise RuntimeError("Identity changed")
            self.no_errors(number)
        self.counts["canary_rounds"] += 1

    def expected(self, peer, header, local):
        if header == "CAL?":
            values = self.units[peer]["before"]["queries"]["CAL? 0"].split(",")
            return ",".join(values[2 * (local - 1):2 * local])
        for name in ("GAIN", "DR", "MODE"):
            if header == f"INSTR:{name}?":
                return self.canaries[peer][name][local - 1]
        return None

    def checked_remote(self, master, peer, header, local, *, long=False, address=1):
        command = f"{header} {address * 4 + local}"
        if long:
            limit = 185 if address < 10 else 184
            command += (" \t" * 100)[:limit - len(command)]
        result = self.forwarded(master, command)
        expected = self.expected(peer, header, local)
        if expected is None:
            if not math.isfinite(float(result)):
                raise RuntimeError("Nonfinite remote measurement")
        elif result != expected:
            raise RuntimeError(f"Corrupt/stale/misrouted reply: {result!r} != {expected!r}")
        if long:
            self.counts["maximum_forwarded_payloads"] += 1
        return result

    def burst(self, master, peer):
        commands = [f"INSTR:GAIN? {i + 5}\n*IDN?\n" for i in range(4)]
        self.units[master]["connection"].write("".join(commands).encode("ascii"))
        for i in range(4):
            if self.receive(master) != self.canaries[peer]["GAIN"][i]:
                raise RuntimeError("Burst remote reply lost ordering")
            if self.receive(master) != self.units[master]["before"]["identity"]:
                raise RuntimeError("Burst local reply lost ordering")
        self.counts["forwarded_queries"] += 4
        self.counts["ordered_burst_replies"] += 8

    def concurrency(self, master, peer):
        self.units[master]["connection"].write(b"INSTR:GAIN? 5\n")
        if self.query(peer, "*IDN?") != self.units[peer]["before"]["identity"]:
            raise RuntimeError("Peer USB failed during RS485 query")
        if self.receive(master) != self.canaries[peer]["GAIN"][0]:
            raise RuntimeError("RS485 query failed during peer USB service")
        self.counts["forwarded_queries"] += 1
        self.counts["USB_RS485_concurrency_rounds"] += 1

    def pressure(self, master, peer):
        c = self.units[peer]["connection"]
        c.write_timeout = 0.5
        completed = True
        try:
            c.write(b"*IDN?\n" * 5000)
        except serial.SerialTimeoutException:
            completed = False  # Expected when the host leaves IN unread.
        finally:
            c.write_timeout = 2
        for i in range(12):
            self.checked_remote(master, peer, "INSTR:GAIN?", i % 4 + 1)
        c.dtr = False
        c.reset_output_buffer()
        c.reset_input_buffer()
        time.sleep(0.1)
        c.reset_input_buffer()
        c.dtr = True
        if self.query(peer, "INSTR:RS485?") != "1":
            raise RuntimeError("Stale USB output survived session recovery")
        self.check_canaries(master, peer)
        self.counts["peer_USB_backpressure_rounds"] += 1
        self.counts["peer_USB_writes_completed" if completed else "peer_USB_writes_backpressured"] += 1
        self.record(peer=peer, backpressure_write_completed=completed)

    def timeout_recovery(self, master, peer):
        c = self.units[master]["connection"]
        c.write(b"MEAS:VOLT? 9\n*IDN?\nSYST:ERR?\n")
        if self.receive(master) != self.units[master]["before"]["identity"]:
            raise RuntimeError("Timeout generated unsolicited data")
        if parse_error(self.receive(master))[0] != 1101:
            raise RuntimeError("Wrong missing-peer diagnostic")
        self.checked_remote(master, peer, "INSTR:GAIN?", 1)
        self.counts["missing_peer_recoveries"] += 1

    def session_recovery(self, master, peer):
        c = self.units[master]["connection"]
        c.write(b"MEAS:VOLT? 5\n")
        c.dtr = False
        c.reset_output_buffer()
        c.reset_input_buffer()
        time.sleep(0.3)  # Allow the already-transmitted RS485 transaction to finish.
        c.reset_input_buffer()
        c.dtr = True
        self.checked_remote(master, peer, "INSTR:GAIN?", 2)
        self.check_canaries(master, peer)
        self.counts["master_session_recoveries"] += 1

    def setters(self, master, peer, local):
        original = self.canaries[peer]["GAIN"][local - 1]
        alternate = "2" if original != "2" else "4"
        for gain in (alternate, original):
            self.command(master, f"INSTR:GAIN {gain},{local + 4}")
            expected = self.canaries[peer]["GAIN"].copy()
            expected[local - 1] = gain
            if self.query(peer, "INSTR:GAIN? 0") != ",".join(expected):
                raise RuntimeError("Remote setter changed the wrong input")
            self.checked_remote(master, peer, "INSTR:GAIN?", local) if gain == original else None
        self.counts["checked_forwarded_setters"] += 2

    def timed_direction(self, master, peer, seconds):
        self.prepare(master, peer)
        start = time.monotonic()
        first_query_count = self.counts["forwarded_queries"]
        deadline = start + seconds
        progress = start + 30
        missing = start + min(30, seconds / 3)
        sessions = start + min(45, seconds / 2)
        pressure = start + min(60, seconds * 0.65)
        boundary = start + min(75, seconds * 0.8)
        index = 0
        while time.monotonic() < deadline:
            local = index % 4 + 1
            header = HEADERS[(index // 4) % len(HEADERS)]
            self.checked_remote(master, peer, header, local, long=index % 8 == 0)
            if index % 32 == 0:
                self.burst(master, peer)
                self.concurrency(master, peer)
            if index % 128 == 0:
                self.check_canaries(master, peer)
                self.setters(master, peer, (index // 128) % 4 + 1)
            now = time.monotonic()
            if now >= missing:
                self.timeout_recovery(master, peer)
                missing = now + 60
            if now >= sessions:
                self.session_recovery(master, peer)
                sessions = now + 90
            if now >= pressure:
                self.pressure(master, peer)
                pressure = now + 180
            if now >= boundary:
                self.command(peer, "INSTR:RS485:SET 31")
                for i in range(1, 5):
                    self.checked_remote(master, peer, "INSTR:GAIN?", i, long=True, address=31)
                self.command(peer, "INSTR:RS485:SET 1")
                self.counts["address31_rounds"] += 1
                boundary = now + 120
            if now >= progress:
                print(json.dumps({"master": master, "peer": peer, "elapsed_s": round(now - start, 1),
                                  "remaining_s": max(0, round(deadline - now, 1)), "queries": self.counts["forwarded_queries"],
                                  "long_payloads": self.counts["maximum_forwarded_payloads"], "status": "passed so far"}), flush=True)
                progress = now + 30
            index += 1
        self.check_canaries(master, peer)
        self.phases.append({"master": master, "peer": peer, "active_seconds": round(time.monotonic() - start, 3), "loops": index,
                            "forwarded_queries": self.counts["forwarded_queries"] - first_query_count})
        self.restore_settings()


def run(args):
    args.output.mkdir(parents=True, exist_ok=True)
    test = Stress(args.output, args.version)
    report = {"time_utc": datetime.now(timezone.utc).isoformat(), "version": args.version,
              "baud": args.baud, "baud_verified": bool(getattr(args, "verified_image", None)),
              "baud_basis": "verified_build_and_upload_manifest" if getattr(args, "verified_image", None) else "unverified_host_label",
              "requested_active_seconds": args.seconds, "passed": False, "restored": False}
    if getattr(args, "verified_image", None):
        report["verified_image"] = args.verified_image
    start = time.monotonic()
    try:
        test.open()
        report["before"] = {n: u["before"] for n, u in test.units.items()}
        test.timed_direction("0003", "0001", args.seconds / 2)
        test.timed_direction("0001", "0003", args.seconds / 2)
        report["passed"] = True
    except BaseException as error:
        report.update(error=str(error), traceback=traceback.format_exc())
        raise
    finally:
        try:
            test.restore_settings()
            report["restored"] = True
            report["after"] = {n: {cmd: test.query(n, cmd) for cmd in
                ("*IDN?", "INSTR:RS485?", "CAL? 0", "INSTR:GAIN? 0", "INSTR:DR? 0", "INSTR:MODE? 0", "SYST:ERR?")}
                for n in test.units}
        except Exception as error:
            report.update(passed=False, restoration_error=str(error))
        cleanup_errors = close_units(test.units, test.trace)
        if cleanup_errors:
            report.update(passed=False, cleanup_errors=cleanup_errors)
        report.update(seconds=round(time.monotonic() - start, 3), counts=dict(test.counts), directions=test.phases,
                      trace_sha256=test.digest.hexdigest(), trace_records=test.records, final_transactions=list(test.tail))
        if test.latencies:
            ordered = sorted(test.latencies)
            report["latency_ms"] = {"median": round(ordered[len(ordered) // 2], 3), "max": round(ordered[-1], 3)}
        (args.output / "summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({k: v for k, v in report.items() if k not in ("before", "after", "traceback", "final_transactions", "verified_image")}), flush=True)
    if not report["passed"] or not report["restored"]:
        raise RuntimeError("Stress or restoration failed; inspect summary.json")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--seconds", type=float, required=True)
    p.add_argument("--baud", type=int, default=230400)
    p.add_argument("--version", default="V5.0.1")
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    if args.seconds < 30:
        p.error("Use at least30 seconds to exercise both roles")
    run(args)
