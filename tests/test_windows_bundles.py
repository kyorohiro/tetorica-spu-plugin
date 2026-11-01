# SPDX-License-Identifier: BSD-3-Clause
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('check_windows_bundles',
    Path(__file__).resolve().parents[1] / 'scripts/check_windows_bundles.py')
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


def image(dll=True):
    data = bytearray(1024)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 60, 128)
    data[128:132] = b'PE\0\0'
    struct.pack_into('<HH', data, 132, 0x8664, 1)
    struct.pack_into('<HH', data, 148, 240, 0x0002 | (0x2000 if dll else 0))
    struct.pack_into('<H', data, 152, 0x20b)
    struct.pack_into('<H', data, 220, 2)
    return data


class PeChecks(unittest.TestCase):
    def inspect(self, data, dll=True):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'synthetic.bin'
            path.write_bytes(data)
            return CHECK.inspect_pe(path, dll)

    def test_amd64_app_and_vst3_flags(self):
        for dll in (False, True):
            result = self.inspect(image(dll), dll)
            self.assertEqual((result['machine'], result['dll'], result['subsystem']), ('AMD64', dll, 2))

    def test_rejects_wrong_arch_magic_bounds_and_file_kind(self):
        invalid = [b'', image()[:64], image()[:200]]
        for offset, fmt, value in ((0, '<H', 0), (60, '<I', 0xffffffff), (128, '<I', 0),
                                   (132, '<H', 0x14c), (134, '<H', 0),
                                   (148, '<H', 111), (150, '<H', 0x2000), (152, '<H', 0x10b)):
            data = image()
            struct.pack_into(fmt, data, offset, value)
            invalid.append(data)
        for data in invalid:
            with self.subTest(length=len(data)), self.assertRaises(ValueError):
                self.inspect(data)
        with self.assertRaises(ValueError):
            self.inspect(image(False), True)
        with self.assertRaises(ValueError):
            self.inspect(image(True), False)


if __name__ == '__main__':
    unittest.main()
