import unittest

from passu_testlib import Port, passu


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


    def test_device_error_advice(self):
        want = ("Device reported: entropy source failed, halting. Try again; "
                "if it keeps happening, don't use the device")
        for request_id in (None, self.ID):
            port = Port(b"\nDEVICE-ERROR: entropy source failed, halting\n")
            with self.subTest(request_id=request_id), \
                    self.assertRaises(SystemExit) as caught:
                passu.PassphraseReader(port).read_passphrase(1, False, request_id)
            self.assertEqual(caught.exception.code, want)


if __name__ == "__main__":
    unittest.main()
