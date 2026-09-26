import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('groups', Path(__file__).resolve().parents[2] / 'scripts/Analyze-ManyLightsInputGroups.py')
groups = importlib.util.module_from_spec(spec)
spec.loader.exec_module(groups)


class GroupTests(unittest.TestCase):
    def fixture(self):
        pair = groups.pair
        d = bytearray(pair.FILE_BYTES)
        pair.HEADER.pack_into(d, 0, 0x50445443, 1, 128, 2816, 32768, 48, 256, 15,
                             123, 99, 0, 0, 1000, 1010, 2, 0x1000, 0x2000, 0x3000, 0x4000, 25477059, 12345, 0)
        struct.pack_into('<I', d, 128+32, 99)
        struct.pack_into('<3f', d, 128+128, 10, 20, 30)
        struct.pack_into('<3f', d, 128+144, 0, 0, 1)
        b = 128+2816
        struct.pack_into('<2I', d, b+pair.LIGHT_BYTES, 4, 1)
        self.inp = b+pair.LIGHT_BYTES+256
        # Header has no PI marker, but defines two following member slots.
        struct.pack_into('<2i', d, self.inp+40, -2, 2)
        for slot in (1, 2, 100):
            struct.pack_into('<8f', d, self.inp+slot*48, 11, 22, 27, 3.14159265, 1, .2, .01, -.001)
            struct.pack_into('<e', d, self.inp+slot*48+46, -1)
        rgb = groups.convert_rgb((2, .4, .02))
        struct.pack_into('<8f', d, b, 1, 2, -3, 3.14159265, *rgb, .005)
        return d

    def run_fixture(self, data):
        return groups.analyze(data, [('rear', (11, 22, 27))])

    def test_header_groups_members_and_excludes_retained_tail(self):
        r = self.run_fixture(self.fixture())
        a = r['anchors'][0]
        self.assertEqual((a['fullCapacityPiMatches'], a['withinInputBoundPiMatches'], a['outsideInputBoundPiMatches']), (3, 2, 1))
        self.assertEqual(r['standaloneSkippedMemberRecords'], 2)
        self.assertEqual(len(a['candidates']), 1)
        c = a['candidates'][0]
        self.assertEqual(c['memberSlots'], [1, 2])
        self.assertLess(c['outputColorComparisons'][0]['maxAbsoluteRgbError'], 1e-6)

    def test_only_negative_color_w_members_are_summed(self):
        d = self.fixture()
        struct.pack_into('<f', d, self.inp+2*48+28, .01)
        a = self.run_fixture(d)['anchors'][0]
        self.assertEqual(a['candidates'][0]['memberSlots'], [1])
        self.assertEqual(a['candidates'][1]['kind'], 'standalone')

    def test_bad_prefix_and_group_bounds_refused(self):
        for offset, value in ((128+2816+groups.pair.LIGHT_BYTES, 32769), (None, 4)):
            d = self.fixture()
            struct.pack_into('<I', d, self.inp+44 if offset is None else offset, value)
            with self.assertRaises(ValueError): self.run_fixture(d)

    def test_nonfinite_member_refused(self):
        d = self.fixture()
        struct.pack_into('<f', d, self.inp+48+16, float('nan'))
        with self.assertRaises(ValueError): self.run_fixture(d)

    def test_exposure_dependent_color_not_guessed(self):
        with self.assertRaises(ValueError): groups.convert_rgb((-1, 0, 0))

    def test_measured_bowl_group_color_control(self):
        rgb = groups.convert_rgb((1.6353611946105957, .23108363151550293, .004843412432819605))
        expected = (1.0823436975479126, .32690340280532837, .08137127757072449)
        self.assertLess(max(abs(rgb[i]-expected[i]) for i in range(3)), 3e-7)


if __name__ == '__main__': unittest.main()
