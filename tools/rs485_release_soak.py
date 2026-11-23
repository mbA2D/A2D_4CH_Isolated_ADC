"""Multi-hour release qualification on the two explicitly identified hub units.

Uses the real firmware and serial ports. No calibration, saved-address or serial
commands are written. The common stress runner restores the original live setup.
"""
import argparse
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import re
import time

from serial.tools import list_ports
from device_snapshot import identify
from rs485_link_test import TARGETS
from scpi_errors import parse_error
import rs485_stress_test as base


class ReleaseStress(base.Stress):
    def __init__(self, output, version):
        super().__init__(output, version)
        self.setter_index = 0
        self.next_measurements = time.monotonic() + 30
        self.last_activity = time.monotonic()

    def activity(self):
        now = time.monotonic()
        gap = now-self.last_activity
        self.last_activity = now
        self.counts["max_host_activity_gap_ms"] = max(self.counts["max_host_activity_gap_ms"], round(gap*1000))
        if gap > 10:
            raise RuntimeError("Host inactivity exceeded10s; elapsed sleep/stall time cannot qualify as active stress")

    def query(self, number, command):
        self.activity()
        value = super().query(number, command)
        self.activity()
        return value

    def checked_remote(self, master, peer, header, local, *, long=False, address=1):
        result = super().checked_remote(master, peer, header, local, long=long, address=address)
        if header.startswith("MEAS:"):
            if not re.fullmatch(r"-?\d+\.\d{4}", result):
                raise RuntimeError("Malformed measurement reply")
            raw_limit = 2.048 / int(self.canaries[peer]["GAIN"][local - 1])
            if header == "MEAS:VOLT:ADC?":
                low, high = -raw_limit, raw_limit
            else:
                offset, gain = map(float, self.units[peer]["before"]["queries"]["CAL? 0"].split(",")[2*(local-1):2*local])
                low, high = (-raw_limit-offset)*gain, (raw_limit-offset)*gain
            if not low-.001 <= float(result) <= high+.001:
                raise RuntimeError("Measurement outside the configured ADC range")
        return result

    def timed_direction(self, master, peer, seconds):
        now = time.monotonic()
        self.next_invalid = now + min(120, seconds / 3)
        self.next_reopen = now + min(180, seconds / 2)
        return super().timed_direction(master, peer, seconds)

    def concurrency(self, master, peer):
        headers = ("MEAS:VOLT?", "MEAS:VOLT:ADC?", "INSTR:GAIN?", "INSTR:DR?", "INSTR:MODE?", "CAL?")
        header = headers[self.counts["mixed_USB_RS485_rounds"] % len(headers)]
        self.units[master]["connection"].write(b"INSTR:GAIN? 5\n")
        answer = self.query(peer, header + " 0")
        if header.startswith("MEAS:"):
            values = answer.split(",")
            if len(values) != 4 or not all(math.isfinite(float(v)) for v in values):
                raise RuntimeError("Invalid simultaneous USB measurement")
        elif header == "CAL?":
            if answer != self.units[peer]["before"]["queries"]["CAL? 0"]:
                raise RuntimeError("Calibration changed")
        elif answer != ",".join(self.canaries[peer][header[6:-1]]):
            raise RuntimeError("USB configuration changed during RS485")
        if self.receive(master) != self.canaries[peer]["GAIN"][0]:
            raise RuntimeError("RS485 reply failed during simultaneous USB ADC work")
        self.counts["forwarded_queries"] += 1
        self.counts["mixed_USB_RS485_rounds"] += 1
        now = time.monotonic()
        if now >= self.next_invalid:
            self.invalid_input(master, peer)
            self.next_invalid = time.monotonic() + 300
        if now >= self.next_reopen:
            self.reopen(master, peer)
            self.next_reopen = time.monotonic() + 900
        if now >= self.next_measurements:
            self.compare_measurements(master, peer)
            self.next_measurements = time.monotonic() + 60

    def compare_measurements(self, master, peer):
        cal = list(map(float, self.units[peer]["before"]["queries"]["CAL? 0"].split(",")))
        for local in range(1, 5):
            self.command(peer, f"INSTR:MODE 0,{local}")
            try:
                # All replies now read one completed, frozen conversion. This
                # avoids mistaking independent conversion noise for corruption.
                raw1 = self.checked_remote(master, peer, "MEAS:VOLT:ADC?", local)
                board = self.checked_remote(master, peer, "MEAS:VOLT?", local)
                raw2 = self.checked_remote(master, peer, "MEAS:VOLT:ADC?", local)
                direct_raw = self.query(peer, f"MEAS:VOLT:ADC? {local}")
                direct_board = self.query(peer, f"MEAS:VOLT? {local}")
                if raw1 != raw2 or raw2 != direct_raw or board != direct_board:
                    raise RuntimeError("Frozen forwarded/direct ADC readings differ")
                offset, gain = cal[2*(local-1):2*local]
                # Includes the published four/five-decimal reply rounding only.
                if abs(float(board)-(float(raw1)-offset)*gain) > .0004:
                    raise RuntimeError("Frozen calibrated/raw measurement relation failed")
                self.counts["direct_and_calibrated_measurement_checks"] += 1
                self.counts["frozen_single_shot_measurement_checks"] += 1
            finally:
                self.command(peer, f"INSTR:MODE {self.canaries[peer]['MODE'][local-1]},{local}")

    def pressure(self, master, peer):
        before = self.counts["peer_USB_writes_backpressured"]
        super().pressure(master, peer)
        if self.counts["peer_USB_writes_backpressured"] == before:
            raise RuntimeError("USB backpressure was not observed; coverage is unexercised")

    def session_recovery(self, master, peer):
        c = self.units[master]["connection"]
        # The first fence proves dispatch progress; the next stays queued behind
        # a missing-peer transaction until cancellation or the one-second timeout.
        c.write(b"*IDN?\nMEAS:VOLT? 9\n*IDN?\n")
        if self.receive(master) != self.units[master]["before"]["identity"]:
            raise RuntimeError("Missing cancellation dispatch fence")
        old_timeout = c.timeout
        try:
            c.timeout = .2
            if c.readline():
                raise RuntimeError("Missing-peer transaction was not pending")
        finally:
            c.timeout = old_timeout
        c.dtr = False
        c.reset_input_buffer()
        time.sleep(.03)
        c.dtr = True
        started = time.monotonic()
        if self.query(master, "*IDN?") != self.units[master]["before"]["identity"] or time.monotonic()-started > .5:
            raise RuntimeError("Session did not cancel the outstanding transaction")
        self.no_errors(master)
        self.record(event="verified_pending_cancel", master=master, peer=peer,
                    pending_fence_wait_ms=200, recovery_ms=round((time.monotonic()-started)*1000,3))
        self.counts["verified_pending_transaction_cancellations"] += 1
        # Generate a slow real-peer reply, drop DTR before it completes, and do
        # not purge host RX after the delayed response should have arrived.
        self.command(peer, "INSTR:DR 15,1")
        self.command(peer, "INSTR:MODE 0,1")
        try:
            c.write(b"*IDN?\nMEAS:VOLT? 5\n*IDN?\n")
            if self.receive(master) != self.units[master]["before"]["identity"]:
                raise RuntimeError("Missing delayed-reply dispatch fence")
            try:
                c.timeout = .01
                early = c.readline()
            finally:
                c.timeout = old_timeout
            if early:
                if not math.isfinite(float(early)) or self.receive(master) != self.units[master]["before"]["identity"]:
                    raise RuntimeError("Invalid early measurement reply")
                self.counts["delayed_reply_cancel_not_exercised"] += 1
            else:
                c.dtr = False
                c.reset_input_buffer()
                time.sleep(.2)
                if c.in_waiting:
                    raise RuntimeError("Delayed response escaped the closed USB session")
                c.dtr = True
                if self.query(master, "*IDN?") != self.units[master]["before"]["identity"]:
                    raise RuntimeError("Stale reply survived the USB session transition")
                self.counts["delayed_peer_reply_session_recoveries"] += 1
                self.counts[f"delayed_peer_reply_recoveries_master_{master}"] += 1
                self.record(event="delayed_reply_session_recovery", master=master, peer=peer)
        finally:
            c.dtr = True
            self.command(peer, "INSTR:DR " + self.canaries[peer]["DR"][0] + ",1")
            self.command(peer, "INSTR:MODE 1,1")
        self.checked_remote(master, peer, "INSTR:GAIN?", 2)
        self.check_canaries(master, peer)
        self.counts["master_session_recoveries"] += 1

    def setters(self, master, peer, local):
        # Rotate both family and physical input, independent of the loop modulus.
        name = ("GAIN", "DR", "MODE")[self.setter_index % 3]
        local = self.setter_index // 3 % 4 + 1
        self.setter_index += 1
        original = self.canaries[peer][name][local - 1]
        alternate = {"GAIN": "2" if original != "2" else "4", "DR": "60", "MODE": "0"}[name]
        prior = self.canaries[peer][name].copy()
        for value in (alternate, original):
            self.command(master, f"INSTR:{name} {value},{local + 4}")
            wanted = self.canaries[peer][name].copy()
            wanted[local - 1] = value
            actual = self.query(peer, f"INSTR:{name}? 0")
            # Transmission completion is not a peer execution acknowledgement.
            deadline = time.monotonic() + 1
            while actual != ",".join(wanted) and time.monotonic() < deadline:
                if actual != ",".join(prior):
                    raise RuntimeError("Remote setter affected an unintended input")
                time.sleep(.005)
                actual = self.query(peer, f"INSTR:{name}? 0")
            if actual != ",".join(wanted):
                raise RuntimeError("Remote setter was not applied")
            prior = wanted
            if name == "MODE" and value == "0":
                self.checked_remote(master, peer, "MEAS:VOLT:ADC?", local)
        self.checked_remote(master, peer, f"INSTR:{name}?", local)
        self.counts["checked_forwarded_setters"] += 2
        self.counts[f"checked_{name}_channel{local}_setters"] += 2

    def invalid_input(self, master, peer):
        for number in (master, peer):
            c = self.units[number]["connection"]
            c.write(b"X" * 2048 + b"\n*IDN?\nSYST:ERR?\n")
            if self.receive(number) != self.units[number]["before"]["identity"]:
                raise RuntimeError("Oversized USB input emitted unsolicited/stale output")
            if parse_error(self.receive(number))[0] != -363:
                raise RuntimeError("Wrong oversized-input diagnostic")
            self.no_errors(number)
        c = self.units[peer]["connection"]
        alternate = "2" if self.canaries[peer]["GAIN"][0] != "2" else "4"
        c.write(f"INSTR:GAIN {alternate},".encode("ascii"))
        for local in range(1, 5):
            self.checked_remote(master, peer, "INSTR:GAIN?", local)
        c.write(b"X\n*IDN?\nSYST:ERR?\n")
        if self.receive(peer) != self.units[peer]["before"]["identity"]:
            raise RuntimeError("Incomplete USB command blocked RS485")
        if parse_error(self.receive(peer))[0] != -128:
            raise RuntimeError("Wrong invalid-channel diagnostic")
        self.check_canaries(master, peer)
        self.counts["oversized_USB_recoveries"] += 2
        self.counts["fragmented_USB_RS485_recoveries"] += 1
        self.record(event="invalid_input_recovery", master=master, peer=peer)

    def reopen(self, master, peer):
        for number in (master, peer):
            c = self.units[number]["connection"]
            self.units[master]["connection"].write(b"INSTR:GAIN? 5\n")
            c.dtr = False
            c.close()
            time.sleep(.3)
            uid, location = TARGETS[number]
            ports = [p for p in list_ports.comports() if p.serial_number == uid]
            if len(ports) != 1 or ports[0].location != location:
                raise RuntimeError("USB target moved during port reopen")
            replacement, descriptor, identity = identify(ports[0].device, number, uid)
            self.units[number]["connection"] = replacement
            if identity != self.units[number]["before"]["identity"]:
                raise RuntimeError("Identity changed on reopen")
            if number == peer and self.receive(master) != self.canaries[peer]["GAIN"][0]:
                raise RuntimeError("Peer USB close prevented RS485 service")
            self.checked_remote(master, peer, "INSTR:GAIN?", 2)
            self.check_canaries(master, peer)
            self.counts["actual_USB_port_reopens"] += 1
        self.record(event="port_reopen_recovery", master=master, peer=peer)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hours", type=float, default=4)
    parser.add_argument("--baud", type=int, default=230400)
    parser.add_argument("--version", default="V5.0.1")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--image-manifest", type=Path, required=True)
    args = parser.parse_args()
    if args.hours <= 0:
        parser.error("hours must be positive")
    args.seconds = args.hours * 3600
    manifest = json.loads(args.image_manifest.read_text(encoding="utf-8"))
    if manifest["version"] != args.version or manifest["baud"] != args.baud:
        parser.error("Version/baud does not match the verified image manifest")
    if hashlib.sha256(Path(manifest["binary"]).read_bytes()).hexdigest() != manifest["sha256"]:
        parser.error("Verified image checksum changed")
    for number, (uid, location) in TARGETS.items():
        receipt = manifest["uploads"][number]
        if receipt["usb_serial"] != uid or receipt["usb_location"] != location or receipt["sha256"] != manifest["sha256"]:
            parser.error("Upload receipt does not match the target and image")
        after = json.loads(Path(receipt["after_snapshot"]).read_text(encoding="utf-8"))
        if hashlib.sha256(Path(receipt["after_snapshot"]).read_bytes()).hexdigest() != receipt["after_snapshot_sha256"]:
            parser.error("Upload snapshot checksum changed")
        if after["usb_serial"] != uid or after["usb_location"] != location or after["identity"] != f"A2D Electronics,4CH Isolated ADC,{number},{args.version}":
            parser.error("Upload verification snapshot does not match")
    args.verified_image = manifest
    base.Stress = ReleaseStress
    wake = None
    if os.name == "nt":
        wake = ctypes.windll.kernel32.SetThreadExecutionState
        wake.argtypes, wake.restype = [ctypes.c_uint], ctypes.c_uint
        if not wake(0x80000001):
            raise RuntimeError("Could not request system wakefulness for the soak")
    try:
        base.run(args)
    finally:
        if wake:
            wake(0x80000000)
