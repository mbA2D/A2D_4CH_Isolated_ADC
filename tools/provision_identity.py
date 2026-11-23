"""Provision a blank unit's serial once, then read it back from persistent storage."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import serial
from serial.tools import list_ports

def provision(port, device_serial, usb_serial, expected_location=None):
    if not re.fullmatch(r'[0-9]{4}', device_serial):
        raise RuntimeError('Serial must contain exactly four digits')
    descriptor=next((p for p in list_ports.comports() if p.device==port),None)
    if descriptor is None or descriptor.serial_number!=usb_serial:
        raise RuntimeError('USB identity mismatch; refusing to open the port')
    if expected_location and descriptor.location!=expected_location:
        raise RuntimeError('USB path changed; refusing provisioning')
    with serial.Serial(port,115200,timeout=2,write_timeout=2) as connection:
        def query(line):
            connection.write(line.encode()+b'\n')
            return connection.readline().decode('ascii').strip()
        before=query('*IDN?')
        fields=before.split(',')
        if len(fields)!=4 or fields[:2]!=['A2D Electronics','4CH Isolated ADC'] or fields[2] not in ('UNASSIGNED',device_serial):
            raise RuntimeError('Unexpected identity; refusing to overwrite it')
        if not re.fullmatch(r'V[0-9]+\.[0-9]+\.[0-9]+', fields[3]):
            raise RuntimeError('Unrecognized firmware version; refusing provisioning')
        queued_errors = tuple(map(int, fields[3][1:].split('.'))) >= (5, 0, 0)
        if queued_errors:
            connection.write(b'*CLS\n')
            connection.write(('SYST:SER:SET '+device_serial+'\n').encode('ascii'))
            status = query('SYST:ERR?')
            if status != '0,"No error"':
                raise RuntimeError('Persistent serial provisioning failed: '+status)
        elif query('SYST:SER:SET '+device_serial)!='OK':
            raise RuntimeError('Persistent serial provisioning failed')
        after=query('*IDN?')
        if after.split(',')!=fields[:2]+[device_serial,fields[3]]:
            raise RuntimeError('Programmed serial did not read back correctly')
        connection.write(b'*RST\n')
        restored=query('*IDN?')
        if restored!=after:
            raise RuntimeError('Programmed serial did not survive settings reload')
        if queued_errors and query('SYST:ERR?') != '0,"No error"':
            raise RuntimeError('Settings reload reported an error')
    return {'time_utc':datetime.now(timezone.utc).isoformat(),'port':port,'usb_serial':usb_serial,
            'usb_location':descriptor.location,'before':before,'after':after,'after_reset':restored,'passed':True}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True)
    parser.add_argument('--serial',required=True)
    parser.add_argument('--usb-serial',required=True)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    result=provision(args.port,args.serial,args.usb_serial)
    if args.output:args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
