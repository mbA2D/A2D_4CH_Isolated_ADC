import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import MagicMock, patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from provision_identity import provision

class ProvisionTests(unittest.TestCase):
    def test_invalid_serial_is_rejected_before_device_access(self):
        with patch("provision_identity.list_ports.comports") as ports:
            for value in ("3","+003","00003","abcd"):
                with self.assertRaisesRegex(RuntimeError,"four digits"):provision("COM10",value,"UID")
            ports.assert_not_called()

    def test_wrong_usb_identity_and_path_never_open_the_port(self):
        target=SimpleNamespace(device="COM10",serial_number="UID",location="1-2")
        with patch("provision_identity.list_ports.comports",return_value=[target]),patch("provision_identity.serial.Serial") as opened:
            with self.assertRaisesRegex(RuntimeError,"USB identity mismatch"):provision("COM10","0003","OTHER")
            with self.assertRaisesRegex(RuntimeError,"USB path changed"):provision("COM10","0003","UID","1-3")
            opened.assert_not_called()

    def execute(self, replies):
        descriptor=SimpleNamespace(device="COM10",serial_number="UID",location="1-2")
        connection=MagicMock()
        connection.__enter__.return_value=connection
        connection.readline.side_effect=[v.encode()+b"\r\n" for v in replies]
        with patch("provision_identity.list_ports.comports",return_value=[descriptor]),patch("provision_identity.serial.Serial",return_value=connection):
            result=provision("COM10","0003","UID","1-2")
        return result,connection

    def test_provision_and_readback_after_settings_reload(self):
        after="A2D Electronics,4CH Isolated ADC,0003,V3.0.0"
        for before in (after,after.replace("0003","UNASSIGNED")):
            result,c=self.execute([before,"OK",after,after])
            self.assertTrue(result["passed"])
            self.assertEqual([a.args[0] for a in c.write.call_args_list],[b"*IDN?\n",b"SYST:SER:SET 0003\n",b"*IDN?\n",b"*RST\n",b"*IDN?\n"])

    def test_a_different_existing_serial_is_never_provisioned(self):
        with self.assertRaisesRegex(RuntimeError,"Unexpected identity"):
            self.execute(["A2D Electronics,4CH Isolated ADC,0001,V3.0.0"])

    def test_new_provisioning_only_reads_query_replies(self):
        after = "A2D Electronics,4CH Isolated ADC,0003,V5.0.0"
        for before in (after, after.replace("0003", "UNASSIGNED")):
            result, c = self.execute([before, '0,"No error"', after, after, '0,"No error"'])
            self.assertTrue(result["passed"])
            self.assertEqual([a.args[0] for a in c.write.call_args_list],
                [b"*IDN?\n", b"*CLS\n", b"SYST:SER:SET 0003\n", b"SYST:ERR?\n",
                 b"*IDN?\n", b"*RST\n", b"*IDN?\n", b"SYST:ERR?\n"])

    def test_new_queued_failure_cannot_report_success(self):
        before = "A2D Electronics,4CH Isolated ADC,UNASSIGNED,V5.0.0"
        with self.assertRaisesRegex(RuntimeError, "Storage fault"):
            self.execute([before, '-320,"Storage fault"'])
        after = before.replace("UNASSIGNED", "0003")
        with self.assertRaisesRegex(RuntimeError, "Settings reload reported"):
            self.execute([before, '0,"No error"', after, after, '1004,"Settings reload failed"'])

    def test_bad_readback_is_not_reported_as_success(self):
        before="A2D Electronics,4CH Isolated ADC,UNASSIGNED,V3.0.0"
        after=before.replace("UNASSIGNED","0003")
        for replies in ([before,"ERROR:STORAGE WRITE"],[before,"OK",before],[before,"OK",after,before]):
            with self.assertRaises(RuntimeError):self.execute(replies)

if __name__=="__main__":unittest.main()
