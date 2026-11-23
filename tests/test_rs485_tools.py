import argparse
from collections import Counter,deque
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from rs485_link_test import Test,configuration_fields,close_units
import rs485_stress_test as stress


class BrokenPort:
    def __init__(self):self.closed=False
    @property
    def dtr(self):return False
    @dtr.setter
    def dtr(self,value):raise OSError("device disconnected")
    def close(self):self.closed=True


class Rs485ToolTests(unittest.TestCase):
    def test_cleanup_attempts_close_after_dtr_failure(self):
        ports=[BrokenPort(),BrokenPort()]
        errors=close_units({str(i):{"connection":p} for i,p in enumerate(ports)},io.StringIO())
        self.assertEqual(len(errors),2)
        self.assertTrue(all(p.closed for p in ports))

    def test_failed_stress_saves_summary_despite_disconnect_cleanup(self):
        ports=[BrokenPort(),BrokenPort()]
        class FakeStress:
            def __init__(self,*args):
                self.units={str(i):{"connection":p,"before":{}} for i,p in enumerate(ports)}
                self.trace=io.StringIO();self.counts=Counter();self.phases=[];self.digest=hashlib.sha256()
                self.records=0;self.tail=deque();self.latencies=[]
            def open(self):pass
            def timed_direction(self,*args):raise RuntimeError("link failed during soak")
            def restore_settings(self):raise RuntimeError("device still disconnected")
        with tempfile.TemporaryDirectory() as temp,patch.object(stress,"Stress",FakeStress):
            args=argparse.Namespace(output=Path(temp),version="V5.0.1",baud=230400,seconds=60)
            with self.assertRaisesRegex(RuntimeError,"link failed during soak"):
                stress.run(args)
            report=json.loads((Path(temp)/"summary.json").read_text(encoding="utf-8"))
            self.assertFalse(report["passed"])
            self.assertFalse(report["restored"])
            self.assertEqual(len(report["cleanup_errors"]),2)
            self.assertIn("device still disconnected",report["restoration_error"])
        self.assertTrue(all(p.closed for p in ports))

    def test_configuration_requires_exactly_four_fields(self):
        self.assertEqual(configuration_fields("1,2,4,8"),["1","2","4","8"])
        for reply in ("1,1,1","1,1,1,1,1",""):
            with self.assertRaises(RuntimeError):configuration_fields(reply)

    def test_restore_rejects_truncated_readback(self):
        with tempfile.TemporaryDirectory() as temp:
            test=Test(Path(temp))
            test.units={"0003":{"before":{"configuration":{"GAIN":["1"]*4},"queries":{}}}}
            test.query=lambda *args:"1,1,1"
            try:
                with self.assertRaisesRegex(RuntimeError,"four ADC configuration fields"):
                    test.restore_settings()
            finally:test.trace.close()

    def test_restore_verifies_setter_readback(self):
        with tempfile.TemporaryDirectory() as temp:
            test=Test(Path(temp))
            test.units={"0003":{"before":{"configuration":{"GAIN":["1"]*4},"queries":{}}}}
            test.query=lambda *args:"2,1,1,1"
            test.command=lambda *args:None
            try:
                with self.assertRaisesRegex(RuntimeError,"GAIN configuration did not restore"):
                    test.restore_settings()
            finally:test.trace.close()


if __name__=="__main__":unittest.main()
