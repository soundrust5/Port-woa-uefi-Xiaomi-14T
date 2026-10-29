import unittest
from collect_probe_ramoops import unpack_capture


class CaptureFramingTests(unittest.TestCase):
    def test_binary_roundtrip(self):
        data = bytes(range(256)) + b'\n\x00'
        self.assertEqual(unpack_capture(data+b'\nTOKEN:0\n', 'TOKEN'), (0, data))

    def test_remote_error_is_not_success(self):
        data = b'cat: missing file\n'
        self.assertEqual(unpack_capture(data+b'\nTOKEN:1\n', 'TOKEN'), (1, data))

    def test_missing_or_truncated_trailer(self):
        for packet in (b'partial binary', b'data\nTOKEN:', b'data\nTOKEN:0', b'data\nTOKEN:999\n'):
            self.assertEqual(unpack_capture(packet, 'TOKEN'), (None, packet))


if __name__ == '__main__':
    unittest.main(verbosity=2)
