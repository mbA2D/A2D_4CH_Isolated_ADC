"""Adversarial USB qualification, restricted to development unit0003 on its hub.

Inputs are unconnected and RS485 terminals disconnected. Changes are volatile;
the final *RST restores stored calibration/address. Every phase checks RAM
canaries so an unexpected CPU reset cannot hide behind successful reconnection.
"""
import argparse
from collections import Counter, deque
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import random
import time
import traceback

import serial
from device_snapshot import identify


class Rig:
    def __init__(self, args, report):
        self.c, descriptor, self.identity = identify(args.port, "0003", "4977386D3659")
        if descriptor.location != "1-10.4.1.1.1.4" or self.identity.split(",")[-1] != args.version:
            self.c.close()
            raise RuntimeError("Wrong development image or hub path")
        report.update(identity=self.identity, usb_serial=descriptor.serial_number,
                      usb_location=descriptor.location)
        self.report = report
        self.buffer = bytearray()
        self.trace = deque(maxlen=40)
        self.c.timeout = 0.1
        self.counts = Counter()
        self.baseline_cal = self.query("CAL 0?")
        self.baseline_address = self.query("INSTR:RS485?")
        self.address = "7"
        self.marker = "0.12500,2.75000"
        self.marker_all = self.marker + "," + ",".join(self.baseline_cal.split(",")[2:])

    def send(self, payload):
        self.trace.append({"direction": "tx", "bytes": len(payload),
                           "prefix_hex": payload[:220].hex(), "sha256": hashlib.sha256(payload).hexdigest()})
        self.counts["tx_bytes"] += len(payload)
        if self.c.write(payload) != len(payload):
            raise RuntimeError("Short USB write")

    def receive(self, timeout=2):
        deadline = time.monotonic() + timeout
        while b"\n" not in self.buffer:
            if time.monotonic() >= deadline:
                raise RuntimeError("USB reply timeout")
            self.buffer.extend(self.c.read(max(1, min(self.c.in_waiting, 8192))))
            if len(self.buffer) > 16384:
                raise RuntimeError("Unbounded/unframed USB reply")
        line, _, rest = self.buffer.partition(b"\n")
        self.buffer[:] = rest
        if line.endswith(b"\r"):
            line = line[:-1]
        text = line.decode("ascii")
        self.trace.append({"direction": "rx", "line": text})
        self.counts["replies"] += 1
        return text

    def query(self, command, expected=None):
        self.send(command.encode("ascii") + b"\n")
        result = self.receive()
        if expected is not None and result != expected:
            raise RuntimeError(f"{command!r}: {result!r} != {expected!r}")
        return result

    def command(self, command):
        self.send(command.encode("ascii") + b"\n")
        self.query("*IDN?", self.identity)

    def install_canaries(self):
        self.command("CAL:VOLT 1,0,0.125,2.75,1.125")
        self.command("INSTR:RS485:SET 7")
        self.address = "7"
        self.check_canaries()

    def check_canaries(self):
        self.query("INSTR:RS485?", self.address)
        self.query("CAL 0?", self.marker_all)
        self.counts["canary_checks"] += 1

    def vector(self, command):
        answer = self.query(command)
        values = [float(v) for v in answer.split(",")]
        if len(values) != 4 or not all(math.isfinite(v) for v in values):
            raise RuntimeError(f"Invalid measurement: {answer!r}")
        self.counts["measurement_vectors"] += 1
        return values

    def state_cycle(self, index):
        gain = (1, 2, 4, 8)[index % 4]
        rate = (15, 60, 240)[(index // 4) % 3]
        mode = (index // 12) % 2
        self.send(f"INSTR:GAIN {gain},0\nINSTR:DR {rate},0\nINSTR:MODE {mode},0\n".encode())
        if mode == 0:
            self.send(b"*TRG\n")
        raw = self.vector("MEAS:VOLT:ADC 0?")
        calibrated = self.vector("MEAS:VOLT 0?")
        # Inputs are unconnected; allow output quantization and intervening noise.
        if abs(calibrated[0] - (raw[0] - 0.125) * 2.75) > 0.002:
            raise RuntimeError("Calibration/configuration state mismatch")
        self.counts["state_cycles"] += 1
        if index % 8 == 0:
            self.check_canaries()

    def backpressure(self):
        self.c.write_timeout = 1
        try:
            self.send(b"*IDN?\n" * 5000)
            completed = True
        except serial.SerialTimeoutException:
            completed = False  # Legitimate OUT backpressure while IN is unread.
        finally:
            self.c.write_timeout = 2
        time.sleep(0.3)
        self.c.dtr = False
        self.c.reset_output_buffer()
        self.c.reset_input_buffer()
        self.buffer.clear()
        time.sleep(0.2)
        self.c.dtr = True
        self.send(b"\nINSTR:RS485?\n")
        deadline = time.monotonic() + 5
        while self.receive() != self.address:
            if time.monotonic() >= deadline:
                raise RuntimeError("Backpressure recovery lost synchronization")
        self.query("*IDN?", self.identity)
        self.check_canaries()
        self.counts["backpressure_rounds"] += 1
        self.counts["unread_identity_requests"] += 5000
        self.report.setdefault("backpressure_writes_completed", []).append(completed)

    def targeted(self):
        self.query("CAL:VOLT 1,0,0,1e20,1", "ERROR:INVALID ARGUMENTS")
        self.check_canaries()
        self.query("CAL:VOLT 1,0,1,1e20,2", "ERROR:INVALID ARGUMENTS")
        self.check_canaries()
        for payload in (b"1EAF\n", b"1EAF_NOT_A_RESET\n", b"1EAF" + b"X" * 60 + b"\n"):
            for _ in range(3):
                self.c.dtr = False
                self.c.dtr = True
            self.send(payload)
            expected = "ERROR:INVALID COMMAND" if len(payload) > 32 else "ERROR:INVALID ARGUMENTS"
            if self.receive() != expected:
                raise RuntimeError("Malformed bootloader-prefix input was not rejected")
            self.query("*IDN?", self.identity)
            self.check_canaries()
        self.counts["targeted_regressions"] = 5

    def fuzz(self, rng, count):
        setters = [b"INSTR:LED 1", b"CAL:RESET 0", b"CAL:SAVE 0", b"INSTR:RS485:SET 31",
                   b"INSTR:GAIN 2,0", b"INSTR:DR 60,0", b"INSTR:MODE 0,0", b"CAL:VOLT 1,0,0,2.75,1"]
        bad_numbers = [b"-1", b"256", b"4294967296", b"1e99", b"nan", b"inf", b"0x10", b"+", b"1x"]
        integer_commands = [b"INSTR:LED %s", b"INSTR:RS485:SET %s", b"INSTR:GAIN %s,0", b"INSTR:DR %s,0", b"INSTR:MODE %s,0"]
        next_progress = time.monotonic() + 15
        for first in range(0, count, 32):
            batch, expected = [], []
            for _ in range(min(32, count - first)):
                category = rng.randrange(8)
                response = "ERROR:INVALID ARGUMENTS"
                if category == 0:
                    payload = rng.choice(setters) + b"?" + rng.choice((b"", b" \t\r"))
                elif category == 1:
                    payload = rng.choice(integer_commands) % rng.choice(bad_numbers)
                elif category == 2:
                    payload = b"MEAS:VOLT " + rng.choice((b"-1", b"129", b"999999999999999999")) + b"?"
                elif category == 3:
                    payload = rng.choice(setters + [b"*IDN?", b"*RST", b"*TRG"]) + b" EXTRA"
                elif category == 4:
                    payload = rng.choice(setters) + b"\0" + bytes(rng.choice((0, 13, 32, 63, 127, 255)) for _ in range(rng.randrange(80)))
                    response = "ERROR:LINE TOO LONG"
                elif category == 5:
                    payload = b"CAL:SAVE 0 " + b"X" * (rng.choice((200, 201, 256, 4096, 8192)) - 11)
                    response = "ERROR:LINE TOO LONG"
                elif category == 6:
                    size = rng.choice((1, 31, 32, 63, 64, 65, 127, 128, 198, 199))
                    payload = b"X" * size
                    response = "ERROR:INVALID COMMAND" if size >= 32 else response
                else:
                    payload = rng.choice((b"", b" \t\r", b"\r"))
                    response = None
                batch.append(payload + b"\n")
                if response is not None:
                    expected.append(response)
                self.counts["malformed_or_blank_frames"] += 1
            batch.append(b"*IDN?\n")
            expected.append(self.identity)
            self.send(b"".join(batch))
            for value in expected:
                actual = self.receive()
                if actual != value:
                    raise RuntimeError(f"Fuzz batch {first}: {actual!r} != {value!r}")
            self.check_canaries()
            if time.monotonic() >= next_progress:
                print(f"PASS {first + len(batch) - 1}/{count} adversarial frames; canaries intact", flush=True)
                next_progress = time.monotonic() + 15

    def soak(self, seconds):
        deadline = time.monotonic() + seconds
        progress = time.monotonic() + 15
        next_pressure = time.monotonic() + 45
        index = 0
        while time.monotonic() < deadline:
            self.state_cycle(index)
            if index % 25 == 0:
                self.send(b"X" * 65536 + b"\n")
                if self.receive() != "ERROR:LINE TOO LONG":
                    raise RuntimeError("64KiB frame did not recover")
                self.counts["discarded_64k_frames"] += 1
                self.check_canaries()
            if time.monotonic() >= next_pressure:
                self.backpressure()
                next_pressure = time.monotonic() + 45
            if time.monotonic() >= progress:
                print(f"PASS soak: {index + 1} state cycles, {max(0, int(deadline-time.monotonic()))}s remaining", flush=True)
                progress = time.monotonic() + 15
            index += 1
        self.check_canaries()

    def restore(self):
        self.command("*RST")
        self.query("CAL 0?", self.baseline_cal)
        self.query("INSTR:RS485?", self.baseline_address)

    def reconnect(self, cycles):
        for index in range(cycles):
            self.c.close()
            self.buffer.clear()
            self.c, descriptor, identity = identify(self.c.port, "0003", "4977386D3659")
            if descriptor.location != "1-10.4.1.1.1.4" or identity != self.identity:
                raise RuntimeError("Unexpected device after reopening")
            self.c.timeout = 0.1
            self.c.baudrate = (1200, 9600, 115200, 921600)[index % 4]
            self.c.parity = serial.PARITY_EVEN if index % 2 else serial.PARITY_NONE
            self.c.stopbits = serial.STOPBITS_TWO if index % 2 else serial.STOPBITS_ONE
            self.c.rts = bool(index % 2)
            self.check_canaries()
            self.counts["port_reopens"] += 1
        self.c.baudrate = 115200
        self.c.parity = serial.PARITY_NONE
        self.c.stopbits = serial.STOPBITS_ONE


def run(args):
    started = time.monotonic()
    report = {"time_utc": datetime.now(timezone.utc).isoformat(), "phase": args.phase,
              "seed": args.seed, "passed": False, "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    rig = None
    try:
        rig = Rig(args, report)
        rig.install_canaries()
        if args.phase == "targeted":
            rig.targeted()
        elif args.phase == "fuzz":
            rig.fuzz(random.Random(args.seed), args.cases)
        elif args.phase == "state":
            for i in range(args.cycles):
                rig.state_cycle(i)
                if i % 100 == 0:
                    print(f"PASS {i}/{args.cycles} state cycles", flush=True)
        elif args.phase == "reconnect":
            rig.reconnect(args.cycles)
        else:
            rig.soak(args.seconds)
        rig.check_canaries()
        rig.restore()
        report["passed"] = True
    except BaseException as error:
        report["error"] = str(error)
        report["traceback"] = traceback.format_exc()
        # Preserve failure evidence/canaries; do not hide a reset/hang with *RST.
        raise
    finally:
        report["seconds"] = round(time.monotonic() - started, 3)
        if rig is not None:
            report["counts"] = dict(rig.counts)
            report["last_transactions"] = list(rig.trace)
            rig.c.close()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({k: v for k, v in report.items() if k not in ("last_transactions", "traceback")}, indent=2), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--version", default="V2.1.3")
    parser.add_argument("--phase", choices=("targeted", "fuzz", "state", "reconnect", "soak"), required=True)
    parser.add_argument("--seed", type=int, default=2605)
    parser.add_argument("--cases", type=int, default=20000)
    parser.add_argument("--cycles", type=int, default=600)
    parser.add_argument("--seconds", type=int, default=600)
    parser.add_argument("--output", type=Path, required=True)
    run(parser.parse_args())
