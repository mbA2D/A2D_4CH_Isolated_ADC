"""Test the linked development units0001/0003; restore live addresses/settings.

Both units must run V5.0.1 and be on their identified hub ports. No calibration
or address save commands are sent. Only one board acts as RS485 master at a time.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import statistics
import time
import traceback

from serial.tools import list_ports
from device_snapshot import identify, read_snapshot
from scpi_errors import parse_error

TARGETS = {
    "0003": ("4977386D3659", "1-10.4.1.1.1.4"),
    "0001": ("4976386E3659", "1-10.4.1.1.1.1"),
}
HEADERS = ("MEAS:VOLT?", "MEAS:VOLT:ADC?", "CAL?", "INSTR:GAIN?", "INSTR:DR?", "INSTR:MODE?")


def close_units(units, trace):
    """Close every handle even when a disconnected port rejects DTR/close."""
    errors = []
    for number, unit in units.items():
        for action in (lambda c: setattr(c, "dtr", False), lambda c: c.close()):
            try:
                action(unit["connection"])
            except Exception as error:
                errors.append(f"{number}: {error}")
    try:
        trace.close()
    except Exception as error:
        errors.append(f"Trace close: {error}")
    return errors


def configuration_fields(reply):
    fields = reply.split(",")
    if len(fields) != 4:
        raise RuntimeError(f"Expected four ADC configuration fields, received {reply!r}")
    return fields


class Test:
    def __init__(self, output, version="V5.0.1"):
        self.units = {}
        self.version = version
        self.counts = Counter()
        self.latencies = []
        self.phases = []
        self.trace = (output / "transactions.jsonl").open("w", encoding="utf-8")

    def record(self, **entry):
        self.trace.write(json.dumps(entry) + "\n")
        self.trace.flush()

    def query(self, number, command):
        unit = self.units[number]
        started = time.monotonic()
        unit["connection"].write((command + "\n").encode("ascii"))
        try:
            answer = self.receive(number)
        except TimeoutError as failure:
            # A failed V5 query is silent. Read its diagnostic before cleanup,
            # so an expected recorded error cannot prevent restoring settings.
            unit["connection"].write(b"SYST:ERR?\n")
            error = self.receive(number)
            self.record(unit=number, command=command, missing_reply=True, queried_error=error)
            raise RuntimeError(f"Unit{number}: {command!r} failed; queued {error}") from failure
        self.record(unit=number, command=command, response=answer,
                    milliseconds=round((time.monotonic() - started) * 1000, 3))
        return answer

    def receive(self, number):
        data = self.units[number]["connection"].readline()
        if not data:
            raise TimeoutError(f"Unit{number}: reply timeout")
        if not data.endswith(b"\n"):
            raise RuntimeError(f"Unit{number}: incomplete or missing reply: {data!r}")
        self.counts["USB_query_replies"] += 1
        return data.decode("ascii").strip()

    def no_errors(self, number):
        answer = self.query(number, "SYST:ERR?")
        if answer != '0,"No error"':
            raise RuntimeError(f"Unit{number}: unexpected error {answer!r}")

    def command(self, number, command):
        unit = self.units[number]
        unit["connection"].write((command + "\n").encode("ascii"))
        self.record(unit=number, command=command)
        if self.query(number, "*IDN?") != unit["before"]["identity"]:
            raise RuntimeError("Unexpected output from setter or changed identity")
        self.no_errors(number)

    def open(self):
        for number, (uid, location) in TARGETS.items():
            matches = [p for p in list_ports.comports() if p.serial_number == uid]
            if len(matches) != 1 or matches[0].location != location:
                raise RuntimeError(f"Unit{number}: wrong USB identity/hub path")
            c, descriptor, identity = identify(matches[0].device, number, uid)
            self.units[number] = {"connection": c, "before": None}
            if identity.rsplit(",", 1)[1] != self.version:
                raise RuntimeError(f"Both units must run {self.version}")
            before = read_snapshot(c, descriptor, identity)
            self.units[number]["before"] = before
            before["configuration"] = {
                name: configuration_fields(self.query(number, f"INSTR:{name}? 0"))
                for name in ("GAIN", "DR", "MODE")
            }
            self.no_errors(number)

    def forwarded(self, master, command):
        started = time.monotonic()
        result = self.query(master, command)
        self.latencies.append((time.monotonic() - started) * 1000)
        self.counts["forwarded_queries"] += 1
        return result

    def compare(self, master, peer, address, header, local):
        remote = self.forwarded(master, f"{header} {address * 4 + local}")
        direct = self.query(peer, f"{header} {local}")
        if header.startswith("MEAS:"):
            values = [float(remote), float(direct)]
            if not all(math.isfinite(v) for v in values) or abs(values[0] - values[1]) > 0.005:
                raise RuntimeError(f"Remote/direct readings differ: {remote}, {direct}")
        elif remote != direct:
            raise RuntimeError(f"Wrong addressed data: {remote!r} != {direct!r}")
        self.counts["remote_USB_comparisons"] += 1
        return remote

    def direction(self, master, peer, cycles):
        self.command(peer, "INSTR:RS485:SET 1")
        self.command(master, "INSTR:RS485:SET 0")
        phase = {"master": master, "peer": peer, "address": 1, "readings": {}}
        for local in range(1, 5):
            for header in HEADERS:
                answer = self.compare(master, peer, 1, header, local)
                if header == "MEAS:VOLT?":
                    phase["readings"][str(local)] = float(answer)
        print(f"PASS {master}->{peer}: all four channels and six query types", flush=True)

        for name, values in (("GAIN", (1, 2, 4, 8)), ("DR", (15, 60, 240)), ("MODE", (0, 1))):
            for local in range(1, 5):
                for value in values:
                    self.command(master, f"INSTR:{name} {value},{local + 4}")
                    if self.compare(master, peer, 1, f"INSTR:{name}?", local) != str(value):
                        raise RuntimeError("Forwarded setter did not reach the intended ADC")
                    self.no_errors(peer)
                    self.counts["forwarded_setter_checks"] += 1

        # Distinct remote gains make reply ordering observable; local channel0 stays local.
        for local in range(1, 5):
            self.command(master, f"INSTR:GAIN {1 << (local - 1)},{local + 4}")
        if self.query(master, "INSTR:GAIN? 0") != ",".join(self.units[master]["before"]["configuration"]["GAIN"]):
            raise RuntimeError("Remote settings affected the master")
        burst = []
        expected = []
        for index in range(24):
            burst.append(f"INSTR:GAIN? {index % 4 + 5}\n*IDN?\n")
            expected.extend((str(1 << (index % 4)), self.units[master]["before"]["identity"]))
        self.units[master]["connection"].write("".join(burst).encode("ascii"))
        for answer in expected:
            if self.receive(master) != answer:
                raise RuntimeError("Coalesced remote/local queries lost reply ordering")
        self.counts["ordered_burst_replies"] += len(expected)
        self.counts["forwarded_queries"] += 24

        for local in range(1, 5):
            for name, value in (("GAIN", 1), ("DR", 15), ("MODE", 1)):
                self.command(master, f"INSTR:{name} {value},{local + 4}")
        for index in range(cycles):
            local = index % 4 + 1
            header = HEADERS[(index // 4) % len(HEADERS)]
            self.compare(master, peer, 1, header, local)
            if index % 100 == 0:
                self.no_errors(master)
                self.no_errors(peer)
                print(f"PASS {master}->{peer}: {index}/{cycles} repeated comparisons", flush=True)

        # Exercise USB service on the peer while its addressed RS485 reply is pending.
        for index in range(100):
            local = index % 4 + 1
            command = f"INSTR:GAIN? {local + 4}"
            self.units[master]["connection"].write((command + "\n").encode("ascii"))
            if self.query(peer, "*IDN?") != self.units[peer]["before"]["identity"]:
                raise RuntimeError("Peer USB lost framing during RS485 service")
            if self.receive(master) != "1":
                raise RuntimeError("RS485 reply failed during peer USB service")
            self.counts["simultaneous_USB_RS485_rounds"] += 1
            self.counts["forwarded_queries"] += 1

        # A real peer at the highest address exercises two-digit envelopes/channels125..128.
        self.command(peer, "INSTR:RS485:SET 31")
        for local in range(1, 5):
            for header in HEADERS:
                self.compare(master, peer, 31, header, local)
        self.command(peer, "INSTR:RS485:SET 1")
        self.counts["highest_address_channel_checks"] += 24

        # Address2 is absent. Only the queried error may follow the identity fence.
        started = time.monotonic()
        self.units[master]["connection"].write(b"MEAS:VOLT? 9\n*IDN?\nSYST:ERR?\n")
        if self.receive(master) != self.units[master]["before"]["identity"]:
            raise RuntimeError("Missing-peer failure emitted an unsolicited response")
        if parse_error(self.receive(master))[0] != 1101:
            raise RuntimeError("Missing-peer failure did not queue1101")
        phase["missing_peer_seconds"] = round(time.monotonic() - started, 3)
        self.compare(master, peer, 1, "INSTR:GAIN?", 1)
        self.no_errors(master)
        self.no_errors(peer)
        self.phases.append(phase)
        print(f"PASS {master}->{peer}: USB concurrency, address31, timeout/recovery", flush=True)
        # Restore each live setting before swapping roles.
        self.restore_settings()

    def restore_settings(self):
        errors = []
        for number, unit in self.units.items():
            before = unit["before"]
            if before is None:
                continue
            try:
                for name in ("GAIN", "DR", "MODE"):
                    current = configuration_fields(self.query(number, f"INSTR:{name}? 0"))
                    if len(before["configuration"][name]) != 4:
                        raise RuntimeError("Original configuration snapshot has the wrong field count")
                    for local, (actual, wanted) in enumerate(zip(current, before["configuration"][name]), 1):
                        if actual != wanted:
                            self.command(number, f"INSTR:{name} {wanted},{local}")
                    if configuration_fields(self.query(number, f"INSTR:{name}? 0")) != before["configuration"][name]:
                        raise RuntimeError(f"Original {name} configuration did not restore")
                address = before["queries"]["INSTR:RS485?"]
                self.command(number, "INSTR:RS485:SET " + address)
                if self.query(number, "INSTR:RS485?") != address:
                    raise RuntimeError("Original address did not restore")
                if self.query(number, "CAL? 0") != before["queries"]["CAL? 0"]:
                    raise RuntimeError("Calibration changed")
                self.no_errors(number)
            except Exception as error:
                errors.append(f"{number}: {error}")
        if errors:
            raise RuntimeError("Restoration failed: " + "; ".join(errors))


def run(args):
    args.output.mkdir(parents=True, exist_ok=True)
    test = Test(args.output, args.version)
    report = {"time_utc": datetime.now(timezone.utc).isoformat(), "passed": False,
              "restored": False, "cycles_per_direction": args.cycles,
              "scope": "Real two-board RS485 link over the shared USB hub; restore original addresses/settings"}
    started = time.monotonic()
    try:
        test.open()
        report["before"] = {number: unit["before"] for number, unit in test.units.items()}
        (args.output / "before.json").write_text(json.dumps(report["before"], indent=2) + "\n", encoding="utf-8")
        test.direction("0003", "0001", args.cycles)
        test.direction("0001", "0003", args.cycles)
        report["passed"] = True
    except BaseException as error:
        report.update(error=str(error), traceback=traceback.format_exc())
        raise
    finally:
        try:
            test.restore_settings()
            report["restored"] = True
            report["after"] = {}
            for number, unit in test.units.items():
                report["after"][number] = {
                    command: test.query(number, command) for command in
                    ("*IDN?", "INSTR:RS485?", "CAL? 0", "INSTR:GAIN? 0", "INSTR:DR? 0", "INSTR:MODE? 0", "MEAS:VOLT? 0", "SYST:ERR?")}
        except Exception as error:
            report.update(passed=False, restoration_error=str(error))
        cleanup_errors = close_units(test.units, test.trace)
        if cleanup_errors:
            report.update(passed=False, cleanup_errors=cleanup_errors)
        report.update(seconds=round(time.monotonic() - started, 3), counts=dict(test.counts), directions=test.phases)
        if test.latencies:
            report["forwarded_query_latency_ms"] = {
                "median": round(statistics.median(test.latencies), 3),
                "maximum": round(max(test.latencies), 3)}
        (args.output / "summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({k: v for k, v in report.items() if k not in ("before", "after", "traceback")}, indent=2), flush=True)
    if not report["passed"] or not report["restored"]:
        raise RuntimeError("Link qualification or restoration failed; inspect summary.json")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cycles", type=int, default=500)
    parser.add_argument("--version", default="V5.0.1")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.cycles < 24:
        parser.error("Use at least24 cycles to cover each channel/query combination")
    run(args)
