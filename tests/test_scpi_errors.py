import sys
import unittest
from pathlib import Path
from unittest.mock import MagicMock
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from scpi_errors import expect_error, parse_error


class ErrorClientTests(unittest.TestCase):
    def test_error_reply_has_a_number_and_quoted_message(self):
        self.assertEqual(parse_error('-222,"Data out of range"'), (-222, "Data out of range"))
        self.assertEqual(parse_error('1001,"ADC read failed"'), (1001, "ADC read failed"))
        for text in ('ERROR:ADC READ', '0,No error', '0,2', 'word,"No error"', '0'):
            with self.assertRaises(RuntimeError):
                parse_error(text)

    def test_fence_detects_unsolicited_output(self):
        c = MagicMock()
        c.readline.return_value = b"ERROR:INVALID ARGUMENTS\r\n"
        with self.assertRaisesRegex(RuntimeError, "before identity fence"):
            expect_error(c, "IDENTITY", "BAD", -113)
        self.assertEqual(c.readline.call_count, 1)

    def test_error_is_read_only_in_response_to_query(self):
        c = MagicMock()
        c.readline.side_effect = [b"IDENTITY\r\n", b'-113,"Undefined header"\r\n']
        self.assertEqual(expect_error(c, "IDENTITY", "BAD", -113), -113)
        c.write.assert_called_once_with(b"BAD\n*IDN?\nSYST:ERR?\n")

    def test_wrong_error_fails_the_check(self):
        c = MagicMock()
        c.readline.side_effect = [b"IDENTITY\r\n", b'0,"No error"\r\n']
        with self.assertRaisesRegex(RuntimeError, "expected"):
            expect_error(c, "IDENTITY", "BAD", (-102, -113))
