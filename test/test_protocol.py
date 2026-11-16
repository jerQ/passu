import importlib.util
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch

# Reader tests do not require pyserial or a connected device.
spec = importlib.util.spec_from_file_location("passu", Path(__file__).parents[1] / "tools/passu.py")
passu = importlib.util.module_from_spec(spec)
serial = types.ModuleType("serial")
tools = types.ModuleType("serial.tools")
tools.list_ports = types.SimpleNamespace()
with patch.dict(sys.modules, {"serial": serial, "serial.tools": tools}):
    spec.loader.exec_module(passu)


class Port:
    def __init__(self, *chunks):
        self.chunks = list(chunks)
        self.reads = 0

    def read(self, size):
        self.reads += 1
        if not self.chunks:
            raise AssertionError("reader discarded buffered data")
        return self.chunks.pop(0)


class ProtocolTests(unittest.TestCase):
    ID = "00112233445566778899aabbccddeeff"
    PHRASE = "apple3-orbit0-canopy5-mellow2-zigzag9"

    def frame(self, phrase=None, request_id=None):
        phrase = phrase or self.PHRASE
        checksum = passu.zlib.crc32(phrase.encode("ascii"))
        return (f"PASSU/1 {request_id or self.ID} 5 {len(phrase)} "
                f"{checksum:08x} {phrase}")

    def test_valid_and_unrelated(self):
        self.assertEqual(passu.parse_response(self.frame(), self.ID), self.PHRASE)
        self.assertIsNone(passu.parse_response(self.frame(), "f" * 32))

    def test_firmware_crc_vector(self):
        frame = (f"PASSU/1 {self.ID} 5 37 8999a807 " + self.PHRASE)
        self.assertEqual(passu.parse_response(frame, self.ID), self.PHRASE)

    def test_corruption(self):
        for bad in (self.frame().replace("apple3", "apple4"),
                    self.frame().replace(" 5 ", " 6 ", 1),
                    self.frame().removesuffix("-zigzag9"),
                    f"PASSU/1 {self.ID} malformed",
                    f"PASSU/1 {self.ID} ERROR entropy failure"):
            with self.subTest(frame=bad), self.assertRaises(SystemExit):
                passu.parse_response(bad, self.ID)

    def test_reader_ignores_stale_replies(self):
        data = ("PASSPHRASE: apple3\n" + self.frame(request_id="f" * 32)
                + "\n" + self.frame() + "\n").encode("ascii")
        self.assertEqual(passu.PassphraseReader(Port(data)).read_passphrase(1, False, self.ID), self.PHRASE)


if __name__ == "__main__":
    unittest.main()
