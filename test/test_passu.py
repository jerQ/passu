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


class ReaderTests(unittest.TestCase):
    def test_batched_replies_need_one_read(self):
        port = Port(b"abacus3\nzoom7\n")
        reader = passu.PassphraseReader(port)
        self.assertEqual(reader.read_passphrase(1, True), "abacus3")
        self.assertEqual(reader.read_passphrase(1, True), "zoom7")
        self.assertEqual(port.reads, 1)

    def test_partial_next_reply_survives(self):
        port = Port(b"PASSPHRASE: abacus3\nPASS", b"PHRASE: zoom7\r\n")
        reader = passu.PassphraseReader(port)
        self.assertEqual(reader.read_passphrase(1, False), "abacus3")
        self.assertEqual(reader.read_passphrase(1, False), "zoom7")


if __name__ == "__main__":
    unittest.main()
