"""Shared fixtures for the Python host tests.

Loads tools/passu.py without pyserial or a connected device, and provides
a fake serial port that returns scripted chunks.
"""
import importlib.util
from pathlib import Path
import sys
import types
from unittest.mock import patch


def load_passu():
    spec = importlib.util.spec_from_file_location(
        "passu", Path(__file__).parents[1] / "tools/passu.py")
    module = importlib.util.module_from_spec(spec)
    serial = types.ModuleType("serial")
    tools = types.ModuleType("serial.tools")
    tools.list_ports = types.SimpleNamespace()
    with patch.dict(sys.modules, {"serial": serial, "serial.tools": tools}):
        spec.loader.exec_module(module)
    return module


passu = load_passu()


class Port:
    """Serial port stand-in: each read() returns the next scripted chunk."""

    def __init__(self, *chunks):
        self.chunks = list(chunks)
        self.reads = 0

    def read(self, size):
        self.reads += 1
        if not self.chunks:
            raise AssertionError("reader discarded buffered data")
        return self.chunks.pop(0)
