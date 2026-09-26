import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('pair', Path(__file__).resolve().parents[2] / 'scripts/Decode-ManyLightsPair.py')
pair = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pair)


class PairTests(unittest.TestCase):
    def fixture(self):
        data = bytearray(pair.FILE_BYTES)
        pair.HEADER.pack_into(data, 0, 0x50445443, 1, 128, 2816, 32768, 48, 256, 15,
                             123, 99, 0, 0, 1000, 1010, 2, 0x1000, 0x2000, 0x3000, 0x4000, 25477059, 12345, 0)
        struct.pack_into('<I', data, 128+0x20, 99)
        struct.pack_into('<3f', data, 128+0x80, 10, 20, 30)
        struct.pack_into('<3f', data, 128+0x90, 0, 0, 1)
        base = 128+2816
        struct.pack_into('<I', data, base+pair.LIGHT_BYTES+4, 1)
        struct.pack_into('<8f', data, base, 1, 2, 3, 3.14159265, 1, 2, 3, 0.001)
        # Position behind camera exists only upstream at slot150, not prefix1.
        struct.pack_into('<8f', data, base+pair.LIGHT_BYTES+256+150*48, 1, 2, -3, 3.14159265, 4, 5, 6, 0.001)
        return data

    def test_input_not_limited_by_output_counter(self):
        r = pair.decode(self.fixture(), [('rear', (11, 22, 27))])
        self.assertEqual(r['input']['piCandidates'], 1)
        self.assertEqual(r['input']['behindCandidates'], 1)
        self.assertEqual(r['anchors'][0]['inputCandidates'][0]['slot'], 150)
        self.assertEqual(r['anchors'][0]['outputMatches'], [])
        self.assertEqual(r['outputValidCount'], 1)

    def test_output_tail_not_current(self):
        data = self.fixture()
        struct.pack_into('<8f', data, 128+2816+150*48, 1, 2, -3, 3.14159265, 4, 5, 6, 0.001)
        self.assertEqual(pair.decode(data)['output']['piCandidates'], 1)

    def test_world_input_does_not_add_camera_twice(self):
        data = self.fixture()
        pos = 128+2816+pair.LIGHT_BYTES+256+150*48
        struct.pack_into('<3f', data, pos, 11, 22, 27)
        r = pair.decode(data, [('rear', (11, 22, 27))], 'world')
        self.assertEqual(r['anchors'][0]['inputCandidates'][0]['relativePosition'], (1, 2, -3))
        self.assertTrue(r['anchors'][0]['inputCandidates'][0]['behindCamera'])
        self.assertEqual(r['anchors'][0]['outputMatches'], [])

    def test_reject_invalid_provenance(self):
        for offset, fmt, value in [(0, '<I', 0), (28, '<I', 0), (56, '<Q', 999),
                                   (80, '<Q', 0x3000), (104, '<Q', 1), (128+0x20, '<I', 1),
                                   (128+2816+pair.LIGHT_BYTES+4, '<I', 32769)]:
            with self.subTest(offset=offset):
                data = self.fixture()
                struct.pack_into(fmt, data, offset, value)
                with self.assertRaises(ValueError): pair.decode(data)

    def test_partial_file(self):
        with self.assertRaises(ValueError): pair.decode(self.fixture()[:-1])

    def test_other_markers_and_nan_are_not_lights(self):
        data = self.fixture()
        base = 128+2816+pair.LIGHT_BYTES+256
        struct.pack_into('<f', data, base+12, 1)
        struct.pack_into('<8f', data, base+48, float('nan'), 2, 3, 3.14159265, 1, 1, 1, 1)
        r = pair.decode(data)
        self.assertEqual(r['input']['otherMarkerSlots'], 1)
        self.assertEqual(r['input']['nonfinitePiSlots'], 1)
        self.assertEqual(r['input']['piCandidates'], 1)


if __name__ == '__main__': unittest.main()
