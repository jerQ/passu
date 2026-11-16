import unittest

from passu_testlib import Port, passu


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
