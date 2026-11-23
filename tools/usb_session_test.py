"""USB boot/session regressions, restricted to unit0003 on its original hub."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import time
import traceback
import serial
from serial.tools import list_ports
from hardware_stress import Rig


def reopen(rig):
    descriptor = next(p for p in list_ports.comports() if p.device == rig.c.port)
    if (descriptor.serial_number, descriptor.location) != ('4977386D3659', '1-10.4.1.1.1.4'):
        raise RuntimeError('Wrong USB identity/path before reopening')
    rig.c = serial.Serial(rig.c.port, 115200, timeout=0.1, write_timeout=2)
    rig.buffer.clear()


def run(args):
    report = {'time_utc': datetime.now(timezone.utc).isoformat(), 'passed': False,
              'script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'checks': []}
    rig = None
    started = time.monotonic()
    try:
        rig = Rig(args, report)
        rig.install_canaries()
        # Former false-reset cases, including magic inside a discarded frame.
        for prefix in (b'', b'X' * 200):
            for _ in range(3):
                rig.c.dtr = False
                rig.c.dtr = True
            rig.send(prefix + b'1EAF')
            time.sleep(0.02)
            rig.send(b'_NOT_A_RESET\n')
            expected = -363 if prefix else (-102, -113)
            rig.query("", expected)
            rig.check_canaries()
            report['checks'].append('fragmented legacy reset token rejected, prefix bytes=' + str(len(prefix)))
        for line in ('SYST:BOOT?', 'SYST:BOOT 1', 'SYST:BOOT EXTRA', 'SYST:BOOT;*IDN?',
                     'SYST:BOOT\x00', 'SYST:BOOT' + ' ' * 200):
            expected = -363 if '\x00' in line or len(line) > 199 else (-102, -113)
            rig.query(line, expected)
            rig.check_canaries()
            report['checks'].append('rejected malformed boot command: ' + repr(line))

        cases = ((b'INSTR:RS485:SET 3', b'1\n', (-102, -113)),
                 (b'X' * 200, b'*IDN?\n', rig.identity),
                 (b'SYST:BO', b'OT\n', (-102, -113)))
        for index in range(args.cycles):
            for prefix, tail, expected in cases:
                rig.send(prefix)
                time.sleep(0.005)
                mode = ('dtr', 'close', 'explicit_close')[index % 3]
                if mode != 'close':
                    rig.c.dtr = False
                if mode != 'dtr':
                    rig.c.close()
                time.sleep((0, 0.001, 0.02, 0.1)[index % 4])
                if mode != 'dtr':
                    reopen(rig)
                else:
                    rig.c.dtr = True
                rig.send(tail)
                if isinstance(expected, tuple):
                    rig.query("", expected)
                elif rig.receive() != expected:
                    raise RuntimeError(f'Stale frame across {mode}, cycle{index}')
                rig.check_canaries()
                report['checks'].append(f'partial/overflow command discarded across {mode}, cycle{index}, prefix bytes{len(prefix)}')

        # Received bytes while DTR is low cannot become commands at the next open.
        rig.c.dtr = False
        rig.send(b'INSTR:RS485:SET 31\nSYST:BOOT\n')
        time.sleep(0.05)
        rig.c.dtr = True
        rig.check_canaries()
        report['checks'].append('closed-session setter and boot command discarded')
        for _ in range(100):
            rig.c.dtr = False
            rig.send(b'INSTR:RS485:SET 31\n')
            rig.c.dtr = True  # No idle gap: exercise an OUT completion near EP0.
            rig.check_canaries()
        report['checks'].append('100 immediate closed-write/open cycles leave address unchanged')

        # Cancel both the outstanding remote query and its already-buffered setter.
        rig.command('INSTR:RS485:SET 0')
        rig.address = '0'
        for mode in ('dtr', 'close', 'explicit_close'):
            rig.send(b'MEAS:VOLT? 5\nINSTR:RS485:SET 31\n')
            time.sleep(0.05)
            if mode != 'close':
                rig.c.dtr = False
            if mode == 'dtr':
                rig.c.dtr = True
            else:
                rig.c.close()
                reopen(rig)
            before = time.monotonic()
            rig.query('*IDN?', rig.identity)
            elapsed = time.monotonic() - before
            if elapsed > 0.5:
                raise RuntimeError('New session waited for the old forwarding timeout')
            rig.check_canaries()
            time.sleep(1.05)
            if rig.buffer or rig.c.in_waiting:
                raise RuntimeError('Old forwarding timeout/reply reached the new session')
            report['checks'].append(f'old forward and queued setter cancelled across {mode}, fresh reply in{elapsed:.3f}s')
        rig.query('MEAS:VOLT? 5', 1101)
        rig.check_canaries()
        report['checks'].append('new forwarding still works after cancellation')

        # Do not allow old device-side TX to follow a new session's first query.
        rig.c.write_timeout = 1
        try:
            rig.send(b'*IDN?\n' * 5000)
        except serial.SerialTimeoutException:
            pass
        finally:
            rig.c.write_timeout = 2
        rig.c.dtr = False
        rig.c.reset_output_buffer()
        time.sleep(0.05)
        rig.c.reset_input_buffer()
        rig.buffer.clear()
        rig.c.dtr = True
        rig.query('INSTR:RS485?', '0')
        rig.check_canaries()
        report['checks'].append('unread USB replies discarded before the new session reply')
        rig.restore()
        report['passed'] = True
    except BaseException as error:
        report.update(error=str(error), traceback=traceback.format_exc())
        raise
    finally:
        if rig is not None:
            report['counts'] = dict(rig.counts)
            report['last_transactions'] = list(rig.trace)
            rig.c.close()
        report['seconds'] = round(time.monotonic() - started, 3)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n')
        print(f"{len(report['checks'])} USB boot/session checks; passed={report['passed']}", flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--version', default='V5.0.0')
    parser.add_argument('--cycles', type=int, default=36)
    parser.add_argument('--output', type=Path, required=True)
    run(parser.parse_args())
