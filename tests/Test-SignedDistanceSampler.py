"""Analytical CPU sampler checks; synthetic data only, no retained captures.

The oracle is the normalized linear-sampler texel-center convention, not a
second implementation of Decode-SignedDistance.sample. At level 0, texel
centers are .125 + .25*k game units. X/Y wrap across their complete texture
dimensions. Z samples the real guard slices of each 130-slice level.

Run: python -B tests/Test-SignedDistanceSampler.py
"""

import importlib.util
from pathlib import Path
import struct
import sys
import unittest


sys.dont_write_bytecode = True
SCRIPT = Path(__file__).resolve().parents[1] / 'scripts' / 'Decode-SignedDistance.py'
SPEC = importlib.util.spec_from_file_location('signed_distance_sampler', SCRIPT)
DECODER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DECODER)


class SignedDistanceSamplerTests(unittest.TestCase):
    def setUp(self):
        self.volume = bytearray(128 * 64 * 130 * 8 * 2)
        self.constants = bytearray(768)
        struct.pack_into('<4f', self.constants, 0x10, 1 / 32, 1 / 16, 1 / 32, 0)

    def put(self, level, x, y, physical_slice, value):
        """Write a literal texture location, including guard slices 0 and 129."""
        offset = (((level * 130 + physical_slice) * 64 + y) * 128 + x) * 2
        struct.pack_into('<e', self.volume, offset, value)

    def sample(self, level, point):
        return DECODER.sample(self.volume, self.constants, level, point)

    def guards(self, level):
        base = level * 64
        for physical_slice, value in ((0, -8), (1, 24), (128, 32), (129, 56)):
            self.put(level, 0, 0, physical_slice, base + value)
        return base

    def test_texel_centers_in_every_level(self):
        # Explicit world positions, texture locations and expected stored values.
        cases = (
            ((.125, .125, .125), (0, 0, 1), -7),
            ((3.125, 2.625, 1.875), (12, 10, 8), 13),
            ((31.875, 15.875, 31.875), (127, 63, 128), 29),
        )
        for level in range(8):
            for point, location, value in cases:
                with self.subTest(level=level, point=point):
                    expected = value + 64 * level
                    self.put(level, *location, expected)
                    scaled = tuple(component * (2 ** level) for component in point)
                    self.assertEqual(self.sample(level, scaled), expected)

    def test_all_eight_interpolation_weights(self):
        # At this point the X/Y/Z weights are 1/4, 1/2, 3/4.
        # Interpolating the two Z planes gives 3 and 24, hence 18.75.
        corners = (
            (1, 2, 4, 0), (2, 2, 4, 2),
            (1, 3, 4, 4), (2, 3, 4, 10),
            (1, 2, 5, 16), (2, 2, 5, 22),
            (1, 3, 5, 28), (2, 3, 5, 38),
        )
        for x, y, physical_slice, value in corners:
            self.put(0, x, y, physical_slice, value)
        self.assertEqual(self.sample(0, (.4375, .75, 1.0625)), 18.75)

    def test_x_wrap_uses_floor_for_negative_coordinates(self):
        self.put(0, 127, 0, 1, 8)
        self.put(0, 0, 0, 1, 24)
        cases = ((-.125, 8), (-.0625, 12), (0, 16), (.0625, 20), (.125, 24))
        for period in (-64, -32, 0, 32, 64):
            for offset, expected in cases:
                with self.subTest(x=period + offset):
                    self.assertEqual(self.sample(0, (period + offset, .125, .125)), expected)

    def test_y_wrap_uses_its_64_texel_dimension(self):
        self.put(0, 0, 63, 1, -4)
        self.put(0, 0, 0, 1, 12)
        cases = ((-.125, -4), (-.0625, 0), (0, 4), (.0625, 8), (.125, 12))
        for period in (-32, -16, 0, 16, 32):
            for offset, expected in cases:
                with self.subTest(y=period + offset):
                    self.assertEqual(self.sample(0, (.125, period + offset, .125)), expected)

    def test_xy_wrap_corner_interpolates_four_texels(self):
        for x, y, value in ((127, 63, 0), (0, 63, 4), (127, 0, 12), (0, 0, 24)):
            self.put(0, x, y, 1, value)
        self.assertEqual(self.sample(0, (0, 0, .125)), 10)
        self.assertEqual(self.sample(0, (-32, -16, .125)), 10)

    def test_lower_z_boundary_reads_real_lower_guard(self):
        for level in range(8):
            base = self.guards(level)
            scale = 2 ** level
            for z, expected in ((0, 8), (.0625, 16), (.125, 24)):
                with self.subTest(level=level, z=z):
                    self.assertEqual(self.sample(level, (.125 * scale, .125 * scale, z * scale)),
                                     base + expected)

    def test_upper_z_boundary_reads_real_upper_guard(self):
        # Includes level 7, whose upper guard is the final physical slice 1039.
        for level in range(8):
            base = self.guards(level)
            scale = 2 ** level
            for z, expected in ((31.875, 32), (31.9375, 38), (31.96875, 41), (-.0625, 38)):
                with self.subTest(level=level, z=z):
                    self.assertEqual(self.sample(level, (.125 * scale, .125 * scale, z * scale)),
                                     base + expected)

    def test_z_period_boundary_returns_to_lower_guard(self):
        # Guard values deliberately disagree with wrapped content. The boundary
        # must blend lower guard/first content, not last content/first content.
        for level in range(8):
            base = self.guards(level)
            scale = 2 ** level
            for z in (-96, -32, 0, 32, 96):
                with self.subTest(level=level, z=z):
                    self.assertEqual(self.sample(level, (.125 * scale, .125 * scale, z * scale)),
                                     base + 8)

    def test_negative_world_center_in_all_three_axes(self):
        self.put(0, 127, 63, 128, -12)
        self.assertEqual(self.sample(0, (-.125, -.125, -.125)), -12)

    def test_zero_weight_nonfinite_neighbors_do_not_poison_center(self):
        for physical_slice in (1, 2):
            for y in (0, 1):
                for x in (0, 1):
                    self.put(0, x, y, physical_slice, float('nan'))
        self.put(0, 0, 0, 1, 7)
        self.assertEqual(self.sample(0, (.125, .125, .125)), 7)


if __name__ == '__main__':
    unittest.main(verbosity=2)
