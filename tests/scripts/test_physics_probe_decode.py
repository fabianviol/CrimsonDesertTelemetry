import importlib.util
import copy
import math
import struct
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('physics_decode', Path(__file__).resolve().parents[2] / 'scripts/Decode-PhysicsProbe.py')
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)


def fixture(hit=True):
    shape, col = bytearray(0x200), bytearray(0x300)
    base = 0x140000000
    struct.pack_into('<Q', shape, 0, base + 0x530ACE8)
    struct.pack_into('<f', shape, 0x68, .1)
    struct.pack_into('<Q', col, 0, base + 0x5D13528)
    struct.pack_into('<I', col, 0xC, int(hit))
    struct.pack_into('<d', col, 0x10, .5 if hit else 1)
    struct.pack_into('<3f', col, 0x80, 0, 1, 0)
    struct.pack_into('<3d', col, 0x90, -529.5, 609.9, -419.5)
    struct.pack_into('<QII', col, 0xE0, 0x80000000010007C8, 0x010007C8, 0)
    struct.pack_into('<I', col, 0xF0, 0x39FFFFFF)
    return dict(schemaVersion=2, executableSha256=decoder.EXE_HASH, requestId='synthetic', pid=1,
                moduleBase=base, reason='diagnostic-segment-completed', segmentCalled=True,
                controlMatched=True, guardsIntact=True, originalsPreserved=True,
                segmentResultPlausible=True, callException=0, shapeHex=shape.hex(),
                segmentCollectorHex=col.hex(), player=[-10529, 609, -4419],
                segmentStart=[-10530, 611, -4420], segmentEnd=[-10529, 609, -4419])


class PhysicsDecodeTests(unittest.TestCase):
    def test_surface_radius_and_negative_tiles(self):
        r = decoder.decode(fixture())
        self.assertEqual(r['surfacePosition'], [-10529.5, 609.9, -4419.5])
        self.assertAlmostEqual(r['centerToSurface'], .1)
        self.assertTrue(r['geometryConsistent'])
        self.assertEqual(r['opticalVisibility'], 'not-classified')

    def test_handles_not_floating_point(self):
        r = decoder.decode(fixture())
        self.assertEqual(r['opaqueCollisionHandle'], '0x80000000010007C8')
        self.assertEqual(r['rawSubshapeSelector'], '0x39FFFFFF')

    def test_no_hit_does_not_decode_stale_contact(self):
        r = decoder.decode(fixture(False))
        self.assertEqual(r['collision'], 'no-hit')
        self.assertNotIn('surfacePosition', r)
        self.assertNotIn('opaqueCollisionHandle', r)

    def test_rejections_are_unknown(self):
        for key in ('segmentCalled', 'controlMatched', 'guardsIntact', 'originalsPreserved', 'segmentResultPlausible'):
            r = fixture(); r[key] = False; r['shapeHex'] = ''
            self.assertEqual(decoder.decode(r)['collision'], 'unknown')
        r = fixture(); r['callException'] = 1
        self.assertEqual(decoder.decode(r)['collision'], 'unknown')

    def test_wrong_build_rejected(self):
        r = fixture(); r['executableSha256'] = 'different'
        with self.assertRaises(ValueError): decoder.decode(r)

    def test_truncated_or_wrong_type_rejected(self):
        for value in ('00', '00'*0x200):
            r = fixture(); r['shapeHex'] = value
            with self.assertRaises(ValueError): decoder.decode(r)

    def test_bad_vectors_rejected(self):
        for value in ([0, 1], [math.nan, 1, 2], [1e9, 0, 0]):
            r = fixture(); r['segmentEnd'] = value
            with self.assertRaises(ValueError): decoder.decode(r)

    def test_incoherent_surface_is_not_silently_accepted(self):
        r = fixture(); c = bytearray.fromhex(r['segmentCollectorHex'])
        struct.pack_into('<d', c, 0x98, 602)
        r['segmentCollectorHex'] = c.hex()
        self.assertFalse(decoder.decode(r)['geometryConsistent'])

    def test_ray_contact_must_lie_on_segment(self):
        r = fixture(); r['primitive'] = 'native-ray'; r['shapeHex'] = ''
        c = bytearray.fromhex(r['segmentCollectorHex'])
        struct.pack_into('<3d', c, 0x90, -529.5, 610, -419.5)
        r['segmentCollectorHex'] = c.hex()
        result = decoder.decode(r)
        self.assertEqual(result['radius'], 0)
        self.assertTrue(result['geometryConsistent'])
        self.assertEqual(result['opticalVisibility'], 'not-classified')

    def test_ray_unknown_collector_rejected(self):
        r = fixture(); r['primitive'] = 'native-ray'
        c = bytearray.fromhex(r['segmentCollectorHex']); struct.pack_into('<Q', c, 0, 1)
        r['segmentCollectorHex'] = c.hex()
        with self.assertRaises(ValueError): decoder.decode(r)

    def test_observation_cannot_become_ray_clear(self):
        r = fixture(False); r['primitive'] = 'native-ray-observation'; r['segmentCalled'] = False
        self.assertEqual(decoder.decode(r)['collision'], 'unknown')

    def fan_fixture(self):
        r = fixture(False)
        r.update(mode='rayfan', controlCalled=True, controlStatus='diagnostic-ray-fan-completed', segmentCalled=False)
        sample = dict(endpoint=r['segmentEnd'], called=True, plausible=True,
                      guardsIntact=True, originalsPreserved=True, collectorHex=r['segmentCollectorHex'])
        r['fan'] = dict(completed=9, expected=9, samples=[copy.deepcopy(sample) for _ in range(9)])
        return r

    def test_fan_keeps_sample_counts_not_optical_percentage(self):
        r = decoder.decode(self.fan_fixture())
        self.assertTrue(r['fanComplete'])
        self.assertEqual(r['clearSamples'], 9)
        self.assertEqual(r['collision'], 'unknown')
        self.assertEqual(r['opticalVisibility'], 'not-classified')
        self.assertNotIn('attenuationFactor', r)

    def test_partial_fan_never_all_clear(self):
        r = self.fan_fixture(); r['fan']['completed'] = 2
        result = decoder.decode(r)
        self.assertFalse(result['fanComplete'])
        self.assertNotIn('clearSamples', result)

    def test_failed_fan_member_rejected(self):
        for field in ('called', 'plausible', 'guardsIntact', 'originalsPreserved'):
            r = self.fan_fixture(); r['fan']['samples'][4][field] = False
            self.assertFalse(decoder.decode(r)['fanComplete'])

    def test_fan_unknown_native_status_not_clear(self):
        r = self.fan_fixture(); r['controlStatus'] = 'unknown-fan-time-budget'
        self.assertFalse(decoder.decode(r)['fanComplete'])


if __name__ == '__main__':
    unittest.main()
