"""Synthetic algorithm controls; game recordings are separate evidence."""
import importlib.util
import math
from pathlib import Path
import random
import struct
import unittest

spec = importlib.util.spec_from_file_location('decoder',
    Path(__file__).resolve().parents[2] / 'scripts' / 'Decode-ExposureContext.py')
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def shader_ev(luminance, visibility):
    # Independent sequential FP32 implementation of inspected SSA 324..348.
    x = min(max(luminance, decoder.LOW), 7.0)
    u = decoder.saturate(f32(f32(x-decoder.PIVOT)*decoder.HIGH_SCALE))
    w = decoder.saturate(f32(f32(x-decoder.LOW)*decoder.LOW_SCALE))
    w2, w3 = f32(2*w), f32(3*w)
    a = f32(f32(w2-3.5) + f32(f32(3-w2)*u))
    b = f32(f32(w3-3) - f32(f32(w3-3)*u))
    root = f32(math.sqrt(decoder.saturate(f32(math.sqrt(visibility)))))
    return f32(f32(f32(math.log2(f32(luminance*8)))-b)-f32(f32(a-b)*root))


def cache(luminance=.0002, visibility=.45):
    luminance = f32(luminance)
    lanes = [1.0]*16
    lanes[5] = shader_ev(luminance, visibility)
    lanes[6] = float('nan')  # packed lane intentionally not float-compatible
    lanes[8] = lanes[15] = luminance
    data = struct.pack('<16f', *lanes).hex()
    return dict(source='engine-gpu-readback-cpu-cache', flags=31,
                wrapperStride=4, wrapperCount=32, innerMode=2,
                rawBeforeHex=data, rawAfterHex=data)


class ExposureContextTests(unittest.TestCase):
    def test_inverse_against_independent_fp32_sequence(self):
        rng = random.Random(25116796)
        cases = [(decoder.MIN_L, 0), (decoder.MIN_L, 1), (.01, .5), (7, 1)]
        cases += [(f32(10**rng.uniform(-6, 4)), rng.random()) for _ in range(3000)]
        for luminance, visibility in cases:
            result = decoder.infer_visibility(luminance, shader_ev(luminance, visibility))
            low, high = result['numericalInterval']
            self.assertLessEqual(low, visibility+1e-12)
            self.assertGreaterEqual(high, visibility-1e-12)
            self.assertAlmostEqual(result['skyVisibilityCandidate'], visibility, delta=1e-4)

    def test_no_implicit_layout_claim(self):
        self.assertEqual(decoder.decode_cache(cache())['reason'], 'shader-layout-not-assumed')

    def test_packed_nan_is_not_rejected(self):
        result = decoder.decode_cache(cache(), True)
        self.assertEqual(result['status'], 'candidate')
        self.assertFalse(result['sourceFrameAgeKnown'])
        self.assertIn('outside supported clipmaps', result['coverage'])

    def test_flags_layout_and_size_fail_closed(self):
        for key, value in [('flags', 15), ('flags', 0), ('wrapperStride', 16),
                           ('wrapperCount', 19), ('wrapperCount', 16385),
                           ('innerMode', 1), ('rawBeforeHex', 'aa'),
                           ('rawAfterHex', 'xx'), ('source', 'other')]:
            sample = cache(); sample[key] = value
            self.assertEqual(decoder.decode_cache(sample, True)['status'], 'unavailable')

    def test_controls_prevent_wrong_lane_decode(self):
        for lane, value in [(5, float('nan')), (8, 0), (8, -.1), (13, 0), (15, .2)]:
            sample = cache()
            raw = bytearray.fromhex(sample['rawBeforeHex'])
            struct.pack_into('<f', raw, lane*4, value)
            sample['rawBeforeHex'] = sample['rawAfterHex'] = raw.hex()
            self.assertEqual(decoder.decode_cache(sample, True)['status'], 'unavailable')

    def test_incompatible_model_not_clamped_to_visibility(self):
        for offset in (-3, 3):
            with self.assertRaisesRegex(ValueError, 'outside-shader-model'):
                decoder.infer_visibility(.01, decoder.forward_ev(.01, .5)+offset)

    def test_roundoff_and_floor(self):
        result = decoder.decode_cache(cache(decoder.MIN_L, 0), True)
        self.assertTrue(result['histogramAtShaderFloor'])
        self.assertLess(result['skyVisibilityCandidate'], 1e-12)

    def test_missing_cache_not_zero(self):
        result = decoder.decode_report({'format': 'private-ambient-probe-v2',
                                        'samples': [{'sequence': 1}]}, True)
        self.assertEqual(result['statusCounts'], {'unavailable': 1})
        self.assertNotIn('skyVisibilityCandidate', result['samples'][0])

    def test_nonprogressing_live_recording_is_not_evidence(self):
        for control in (False, None):
            with self.assertRaisesRegex(ValueError, 'progressing control'):
                decoder.decode_report({'format': 'private-exposure-context-v1',
                                       'controlProgressed': control,
                                       'samples': [{'exposureCache': cache()}]}, True)


if __name__ == '__main__':
    unittest.main()
