"""Bounded inspector regressions; all project/state fixtures are generated."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import plistlib
import struct
import tempfile
import unittest
from unittest import mock


SPEC = importlib.util.spec_from_file_location(
    'garageband_state', Path(__file__).resolve().parents[1] / 'scripts/check_garageband_vab_state.py')
INSPECT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INSPECT)


def source_header(count, name=b'Software source', flags=2, root=261.6255653005986):
    return struct.pack('<4I3d6I', 1, flags, count, len(name), root, -.375, .625,
                       0, 0x3fc0000f, 1234, 0, 0, 0)


def software(pcm=b'\x00\x80\xff\xff\x00\x00\xff\x7f', name=b'Software source', loop=False):
    return source_header(len(pcm) // 2, name, 2 | bool(loop)) + pcm + name


def state(schema=9, kind=4, source=None, program=0, tone=0, gain=37.):
    if source is None:
        source = software() if kind == 4 else b''
    fixed = {1: 40, 2: 48, 3: 72, 4: 72, 5: 80, 6: 112, 7: 1024, 8: 1024, 9: 1024}[schema]
    result = bytearray(fixed + len(source))
    result[:8] = b'TSPUST01'
    struct.pack_into('<6I', result, 8, schema, len(result), kind, program, tone, len(source))
    result[32:32 + len(source)] = source
    n = len(source)
    struct.pack_into('<d', result, 32 + n, gain)
    if schema >= 2:
        struct.pack_into('<2I', result, 40 + n, 0x1fc0000f, 2)
    if schema >= 3:
        struct.pack_into('<2I2d', result, 48 + n, 0, 0, .25, .25)
    if schema >= 5:
        struct.pack_into('<2I', result, 72 + n, 0, 48)
    if schema >= 6:
        struct.pack_into('<2I3d', result, 80 + n, 0, 0, 1., .5, 500.)
    if schema >= 7:
        struct.pack_into('<2Id', result, 112 + n, 0, 0, .375)
    return bytes(result)


def change(payload, offset, value, fmt='<I'):
    result = bytearray(payload)
    struct.pack_into(fmt, result, offset, value)
    return bytes(result)


class GarageBandStateTests(unittest.TestCase):
    def test_raw_repeat_target_without_repeat_bit_is_supported(self):
        for packet_flags in (0, 4):
            with self.subTest(repeat_target_flags=packet_flags):
                adpcm = bytearray(48)
                for offset in (0, 16, 32):
                    adpcm[offset] = 12
                adpcm[17], adpcm[33] = packet_flags, 3
                header = bytearray(source_header(48, b'Raw', flags=1))
                struct.pack_into('<I', header, 40, 28)
                payload = state(kind=3, source=bytes(header) + adpcm + b'Raw')
                fields, _ = INSPECT.decode_payload(payload)
                sample = fields['adpcm_sample']
                self.assertTrue(sample['native_loop_supported'])
                self.assertEqual(sample['loop_start_samples'], 28)
                self.assertEqual(sample['adpcm_sha256'], hashlib.sha256(adpcm).hexdigest())

    def test_schema9_all_five_kinds_and_old_schema_migration(self):
        raw = source_header(16, b'Raw') + b'\0\1' + bytes(14) + b'Raw'
        cases = [(0, b'', 'Builtin'), (1, b'pBAVfixture', 'VAB'),
                 (2, bytes(448), 'PcmCycle'), (3, raw, 'AdpcmSample'),
                 (4, software(), 'SoftwarePcm')]
        for kind, source, label in cases:
            with self.subTest(kind=kind):
                payload = state(kind=kind, source=source, program=3 if kind == 1 else 0,
                                tone=1 if kind == 1 else 0)
                fields, bank = INSPECT.decode_payload(payload)
                self.assertEqual((fields['schema'], fields['kind'], fields['gain_percent']), (9, label, 37))
                self.assertEqual(bank, source if kind == 1 else b'')
        for schema in range(1, 9):
            fields, bank = INSPECT.decode_payload(state(schema, 0))
            self.assertEqual((fields['schema'], fields['kind'], bank), (schema, 'Builtin', b''))
        legacy, _ = INSPECT.decode_payload(struct.pack('<d', 99.))
        self.assertEqual((legacy['schema'], legacy['gain_percent']), ('legacy-gain-only', 99))

    def test_software_metadata_hash_without_samples(self):
        pcm = b'\x00\x80\xff\xff\0\0\xff\x7f'
        payload = state(source=software(pcm, '原音'.encode(), loop=True))
        fields, bank = INSPECT.decode_payload(payload)
        sample = fields['software_pcm_sample']
        self.assertEqual((sample['pcm_frames'], sample['pcm_bytes'], sample['sample_rate']), (4, 8, 44100))
        self.assertEqual(sample['pcm16le_sha256'], hashlib.sha256(pcm).hexdigest())
        self.assertEqual((sample['name'], sample['volume_left'], sample['volume_right']), ('原音', -.375, .625))
        self.assertEqual((sample['adsr1'], sample['adsr2'], sample['inspector_pitch']), (0x000f, 0x3fc0, 1234))
        self.assertEqual((sample['loop'], sample['loop_start_samples'], sample['reverb_send']), (True, 0, True))
        self.assertEqual(bank, b'')
        self.assertNotIn('samples', sample)
        self.assertLess(len(json.dumps(fields)), 3000)

    def test_schema_kind_caps_are_preserved(self):
        for schema in range(1, 9):
            with self.subTest(schema=schema):
                with self.assertRaises(ValueError):
                    INSPECT.decode_payload(state(schema, 4, software()))
                with self.assertRaises(ValueError):
                    INSPECT.decode_payload(state(schema, 1, bytes(INSPECT.MAX_BANK + 1)))
        with self.assertRaises(ValueError):
            INSPECT.decode_payload(state(9, 1, bytes(INSPECT.MAX_BANK + 1)))
        with self.assertRaises(ValueError):
            INSPECT.decode_payload(change(state(), 8, 10))
        with self.assertRaises(ValueError):
            INSPECT.decode_payload(change(state(), 16, 5))

    def test_bad_lengths_flags_metadata_and_loop_minimum(self):
        good = state()
        for offset in (12, 20, 24, 28, 32, 40, 44, 80):
            with self.subTest(offset=offset):
                with self.assertRaises(ValueError):
                    INSPECT.decode_payload(change(good, offset, 0xffffffff))
        for offset in (36, 72, 84, 88, 92):
            with self.subTest(reserved=offset):
                with self.assertRaises(ValueError):
                    INSPECT.decode_payload(change(good, offset, 4 if offset == 36 else 1))
        for offset, value in ((48, float('nan')), (48, 0.), (48, 1.e6 + 1),
                              (56, -1.01), (64, float('inf'))):
            with self.subTest(metadata=(offset, value)):
                with self.assertRaises(ValueError):
                    INSPECT.decode_payload(change(good, offset, value, '<d'))
        for invalid_name in (b'x\0y', b'\xc0\x80', b'\xed\xa0\x80'):
            with self.assertRaises(ValueError):
                INSPECT.decode_payload(state(source=software(name=invalid_name)))
        for frames in (1, 2, 3):
            with self.assertRaises(ValueError):
                INSPECT.decode_payload(state(source=software(bytes(frames * 2), loop=True)))
        for bad in (good[:-1], good + b'\0', state(source=software(b'')),
                    state(source=software(name=bytes(321)))):
            with self.assertRaises(ValueError):
                INSPECT.decode_payload(bad)

    def test_maximum_software_state_and_large_xml_project(self):
        # Exercise the full32MiB cap and its roughly44.8MB base64 XML fragment.
        # No Python list/tuple with millions of samples is ever constructed.
        pcm = b'\x00\x80\xff\x7f' * (INSPECT.MAX_SOFTWARE_PCM // 4)
        payload = state(source=software(pcm, b'x' * 320))
        self.assertEqual(len(payload), INSPECT.MAX_STATE)
        fields, _ = INSPECT.decode_payload(payload)
        self.assertEqual(fields['software_pcm_sample']['pcm_frames'], INSPECT.MAX_SOFTWARE_FRAMES)
        self.assertEqual(fields['software_pcm_sample']['pcm16le_sha256'], hashlib.sha256(pcm).hexdigest())
        xml = plistlib.dumps(dict(INSPECT.IDENTITY, version=0x100, name='TetoricaSPU', data=payload))
        self.assertGreater(len(xml), 2 * 1024 * 1024)
        self.assertLess(len(xml), INSPECT.MAX_XML)
        self.assertGreaterEqual(INSPECT.MAX_PROJECT_DATA, INSPECT.MAX_XML * 2)
        with tempfile.TemporaryDirectory() as directory:
            project = Path(directory) / 'ProjectData'
            project.write_bytes(b'opaque-prefix\0' + xml + b'\0opaque-suffix')
            records, file_record = INSPECT.scan_file(project)
        self.assertEqual(file_record['xml_fragments'], 1)
        self.assertEqual(file_record['unparsed_xml_fragments'], 0)
        self.assertEqual(len(records), 1)
        self.assertNotIn('error', records[0][0])
        self.assertEqual(records[0][0]['software_pcm_sample']['pcm16le_sha256'], hashlib.sha256(pcm).hexdigest())
        self.assertEqual(records[0][1], b'')

    def test_expected_vab_mode_is_unchanged(self):
        bank = b'pBAVoriginal-bank'
        payload = state(9, 1, bank, 3, 1, 99)
        xml = plistlib.dumps(dict(INSPECT.IDENTITY, version=0x100, name='TetoricaSPU', data=payload))
        with tempfile.TemporaryDirectory() as directory:
            project, expected = Path(directory) / 'ProjectData', Path(directory) / 'expected.vab'
            project.write_bytes(b'prefix' + xml)
            expected.write_bytes(bank)
            output = io.StringIO()
            with mock.patch('sys.argv', ['check_garageband_vab_state.py', str(project), '--expected-vab', str(expected)]):
                with contextlib.redirect_stdout(output):
                    self.assertEqual(INSPECT.main(), 0)
        report = json.loads(output.getvalue())
        self.assertTrue(report['passed'])
        self.assertEqual(report['mode'], 'expected-vab')
        self.assertEqual(report['expected_candidate_indices'], [0])
        self.assertFalse(report['active_selection_proven'])


if __name__ == '__main__':
    unittest.main()
