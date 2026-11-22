import struct
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import MagicMock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from device_snapshot import identify
from usb_upload import check_image, dfu_interfaces, require_single_maple, reset_to_bootloader, update


class UploadTargetTests(unittest.TestCase):
    def test_new_application_uses_scpi_boot_command(self):
        for version in ("V2.1.3", "V3.0.0"):
            connection = MagicMock()
            connection.write.return_value = len(b"SYST:BOOT\n")
            reset_to_bootloader(connection, version)
            connection.write.assert_called_once_with(b"SYST:BOOT\n")
            self.assertIs(connection.dtr, True)
            connection.flush.assert_not_called()

    def test_short_boot_command_write_is_rejected(self):
        connection = MagicMock()
        connection.write.return_value = 3
        with self.assertRaisesRegex(RuntimeError, "Short write"):
            reset_to_bootloader(connection, "V2.1.3")

    def test_transition_uses_legacy_reset_for_old_running_image(self):
        connection = MagicMock()
        with patch("usb_upload.time.sleep"):
            reset_to_bootloader(connection, "V2.1.2")
        connection.write.assert_called_once_with(b"1EAF")
        self.assertIs(connection.dtr, False)

    def test_unknown_running_version_cannot_choose_reset_protocol(self):
        connection = MagicMock()
        with self.assertRaisesRegex(RuntimeError, "Unrecognized running version"):
            reset_to_bootloader(connection, "UNKNOWN")
        connection.write.assert_not_called()

    def test_scpi_update_keeps_dtr_handle_open_until_dfu_is_detected(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            binary = folder / "firmware.bin"
            binary.write_bytes(struct.pack("<II", 0x20005000, 0x08002101) + b"0003\0V2.1.3\0")
            writer = folder / "writer.exe"
            writer.write_bytes(b"More than one")
            args = SimpleNamespace(binary=binary, serial="0003", version="V2.1.3", port="COM10",
                                   usb_serial="EXPECTED", dfu_util=folder / "dfu.exe",
                                   maple_dfu_util=writer, allow_settings_reset=False)
            state = {"usb_serial": "EXPECTED", "usb_location": "1-2", "port": "COM10",
                     "identity": "A2D Electronics,4CH Isolated ADC,0003,V2.1.3",
                     "queries": {"CAL 0?": "saved", "INSTR:RS485?": "7"}}
            connection = MagicMock()
            connection.write.return_value = len(b"SYST:BOOT\n")
            listing = 'Found DFU: [1eaf:0003] path="1-2", alt=2, name="Flash 0x8002000"'
            probes = []
            def process(command, **kwargs):
                if command[-1] == "-l":
                    probes.append(command)
                    if len(probes) <= 2:
                        self.assertFalse(connection.close.called, "DTR handle closed before DFU appeared")
                    return SimpleNamespace(stdout="" if len(probes) == 1 else listing, stderr="", returncode=0)
                self.assertTrue(connection.close.called)
                return SimpleNamespace(stdout="finished!", stderr="", returncode=0)
            descriptor = SimpleNamespace(location="1-2", device="COM10", serial_number="EXPECTED")
            with patch("usb_upload.snapshot", return_value=state), \
                 patch("usb_upload.identify", return_value=(connection, descriptor, state["identity"])), \
                 patch("usb_upload.subprocess.run", side_effect=process), \
                 patch("usb_upload.list_ports.comports", return_value=[descriptor]), \
                 patch("usb_upload.time.sleep"):
                update(args)
            connection.close.assert_called_once()
            connection.write.assert_called_once_with(b"SYST:BOOT\n")
            self.assertEqual(len(probes), 3)

    def test_wrong_usb_identity_is_not_opened(self):
        port = SimpleNamespace(device="COM10", serial_number="OTHER")
        with patch("device_snapshot.list_ports.comports", return_value=[port]), patch("device_snapshot.serial.Serial") as opened:
            with self.assertRaisesRegex(RuntimeError, "USB identity mismatch"):
                identify("COM10", "0003", "EXPECTED")
            opened.assert_not_called()

    def test_wrong_firmware_identity_stops_after_idn(self):
        port = SimpleNamespace(device="COM10", serial_number="EXPECTED")
        connection = MagicMock()
        connection.readline.return_value = b"A2D Electronics,4CH Isolated ADC,0001,V1.0.2\r\n"
        with patch("device_snapshot.list_ports.comports", return_value=[port]), patch("device_snapshot.serial.Serial", return_value=connection):
            with self.assertRaisesRegex(RuntimeError, "Unexpected identity"):
                identify("COM10", "0003", "EXPECTED")
            connection.write.assert_called_once_with(b"*IDN?\n")
            connection.close.assert_called_once()

    def test_dfu_requires_exact_physical_path_and_alternate(self):
        listing = '\n'.join([
            'Found DFU: [1eaf:0003] path="1-1", alt=2, name="Flash 0x8002000", serial="LLM 003"',
            'Found DFU: [1eaf:0003] path="1-10.4", alt=1, name="Flash 0x8005000", serial="LLM 003"',
            'Found DFU: [1eaf:0003] path="1-10.4", alt=2, name="Flash 0x8002000", serial="LLM 003"',
        ])
        self.assertEqual(dfu_interfaces(listing, "1-10.4"), [{"path": "1-10.4", "serial": "LLM 003"}])
        self.assertEqual(dfu_interfaces(listing, "1-2"), [])

    def test_wrong_dfu_flash_address_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "expected application address"):
            dfu_interfaces('Found DFU: [1eaf:0003] path="1-10.4", alt=2, name="Flash 0x8005000"', "1-10.4")

    def test_legacy_writer_refuses_multiple_or_unidentified_devices(self):
        target = 'Found DFU: [1eaf:0003] path="1-2", alt=2, name="Flash 0x8002000"'
        require_single_maple(target, "1-2")
        for listing in (target + '\nFound DFU: [1eaf:0003] path="1-1"',
                        target + '\nCannot open DFU device 1eaf:0003',
                        'Found DFU: [1eaf:0003] alt=2', ''):
            with self.assertRaises(RuntimeError):
                require_single_maple(listing, "1-2")

    def test_wrong_image_offset_and_identity_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "firmware.bin"
            binary.write_bytes(struct.pack("<II", 0x20005000, 0x08002101) + b"0003\0V1.0.3\0")
            check_image(binary, "0003", "V1.0.3")
            with self.assertRaisesRegex(RuntimeError, "serial is absent"):
                check_image(binary, "0001", "V1.0.3")
            with self.assertRaisesRegex(RuntimeError, "version is absent"):
                check_image(binary, "0003", "V2.0.0")
            binary.write_bytes(struct.pack("<II", 0x20005000, 0x08000101) + b"0003\0V1.0.3\0")
            with self.assertRaisesRegex(RuntimeError, "not linked"):
                check_image(binary, "0003", "V1.0.3")

    def test_oversize_image_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "firmware.bin"
            binary.write_bytes(bytes(54 * 1024 + 1))
            with self.assertRaisesRegex(RuntimeError, "does not fit"):
                check_image(binary, "0003")


if __name__ == "__main__":
    unittest.main()
