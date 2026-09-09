"""Synthetic controls only, no live sky-visibility proof."""
import importlib.util
from pathlib import Path
import math
import struct
import unittest

spec = importlib.util.spec_from_file_location('readback', Path(__file__).resolve().parents[2]/'scripts'/'Decode-SpatialReadback.py')
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)


def report():
    gi = bytearray(768)
    for offset, values in ((0x10, (.03125, .0625, .03125, 0)), (0x2e0, (.3, .4, .5, 0))):
        struct.pack_into('<4f', gi, offset, *values)
    for i in range(8):
        struct.pack_into('<4f', gi, 0x140+i*16, *((0, 0, 0, 1) if i == 1 else (1000, 1000, 1000, 1)))
    return dict(format='private-spatial-readback-v1', executableSha256=decoder.EXE, controlProgressed=True,
        textureReadback=dict(status='gpu-complete-texture-only', gpuCompleted=True, gpuCopyIssued=True,
            mapCalls=1, fenceValue=1, frame=42, packedTextureHex=(bytes([64])*math.prod(decoder.DIMS)).hex(),
            releaseBarrier=dict(syncBefore=128, syncAfter=0, accessBefore=128, accessAfter=-2147483648,
                layoutBefore=6, layoutAfter=1, flags=0), context=dict(error=0, frame=42,
                selectedForTextureReadback=True, giCopiesMatch=True, giBeforeHex=gi.hex(), giAfterHex=gi.hex(),
                resource=123, bankFlag=0, srvViewEntryRawHex='0f'+'00'*79,
                samplerHex=struct.pack('<13I', 0x14, 1, 1, 1, 0, 1, 8, 0, 0, 0x7f7fffff, 12, 4, 0).hex())))


def paired_report():
    r = report()
    r['format'] = 'private-spatial-readback-v2'
    t = r['textureReadback']
    t.update(status='gpu-complete-texture-and-buffers', giGpuFramePaired=True, exposureGpuFramePaired=True)
    t['context'].update(giGpuResource=456, exposureGpuResource=789)
    cbv, uav = [0]*64, [0]*64
    cbv[2], uav[3] = 4096+256, 65536+1024
    lanes = [1.0]*32
    lanes[8] = lanes[15] = .0002
    lanes[5] = decoder.model.forward_ev(.0002, 1-64/255)
    lanes[6] = float('nan')  # packed non-float field is deliberately tolerated
    t['bufferPair'] = dict(requested=True, copied=True, nativeDispatchSeen=True, nativeDispatches=1,
        giHex=t['context']['giBeforeHex'], exposureHex=struct.pack('<32f', *lanes).hex(),
        giResource=456, exposureResource=789, giBase=4096, exposureBase=65536, giOffset=256,
        exposureOffset=1024, giRootIndex=2, exposureRootIndex=3, nativeCbv=cbv, nativeUav=uav)
    return r


class SpatialReadbackTests(unittest.TestCase):
    def test_centers(self):
        data = bytes(range(8))
        for z in range(2):
            for y in range(2):
                for x in range(2):
                    v, _ = decoder.linear_wrap(data, ((x+.5)/2, (y+.5)/2, (z+.5)/2), (2, 2, 2))
                    self.assertAlmostEqual(v, data[(z*2+y)*2+x]/255)

    def test_wrap_negative_and_seam(self):
        for pos in ((0, 0, 0), (1, -1, 2), (-10, 20, -30), (.5, .5, .5)):
            v, neighbors = decoder.linear_wrap(bytes(range(8)), pos, (2, 2, 2))
            self.assertAlmostEqual(v, 3.5/255)
            self.assertAlmostEqual(sum(n['weight'] for n in neighbors), 1)

    def test_validation(self):
        for raw, coordinates in ((b'', (0, 0, 0)), (b'12345678', (float('nan'), 0, 0))):
            with self.assertRaises(ValueError):
                decoder.linear_wrap(raw, coordinates, (2, 2, 2))

    def test_direct_unpaired(self):
        r = decoder.decode(report(), True)
        self.assertAlmostEqual(r['skyVisibilityCandidate'], 1-64/255)
        self.assertFalse(r['giGpuFramePaired'])
        self.assertFalse(r['exposureGpuFramePaired'])
        self.assertFalse(r['fallback'])
        self.assertEqual(r['byteRange'], [64, 64])

    def test_no_layout_no_sample(self):
        r = decoder.decode(report(), False)
        self.assertEqual(r['status'], 'texture-only-reference-unavailable')
        self.assertNotIn('skyVisibilityCandidate', r)

    def test_fence_and_control_guards(self):
        for key, value in (('gpuCompleted', False), ('gpuCopyIssued', False), ('mapCalls', 0), ('fenceValue', 0)):
            r = report()
            r['textureReadback'][key] = value
            with self.assertRaises(ValueError):
                decoder.decode(r, True)
        r = report()
        r['controlProgressed'] = False
        with self.assertRaises(ValueError):
            decoder.decode(r, True)

    def test_binding_and_context_guards(self):
        for key, value in (('samplerHex', '00'*52), ('srvViewEntryRawHex', '00'*80), ('frame', 41)):
            r = report()
            r['textureReadback']['context'][key] = value
            with self.assertRaises(ValueError):
                decoder.decode(r, True)
        r = report()
        r['textureReadback']['context']['giCopiesMatch'] = False
        self.assertNotIn('skyVisibilityCandidate', decoder.decode(r, True))

    def test_gpu_pair_inverse(self):
        r = decoder.decode(paired_report(), True)
        self.assertTrue(r['giGpuFramePaired'] and r['exposureGpuFramePaired'])
        self.assertTrue(r['cpuGiEqualsGpu'])
        self.assertLess(r['directInverseAbsoluteDifference'], .00001)
        self.assertAlmostEqual(r['skyVisibilityCandidate'], 1-64/255)

    def test_gpu_reference_not_cpu(self):
        r = paired_report()
        r['textureReadback']['context']['giBeforeHex'] = '00'*768
        r['textureReadback']['context']['giCopiesMatch'] = False
        d = decoder.decode(r, True)
        self.assertFalse(d['cpuGiEqualsGpu'])
        self.assertEqual(d['gpuReference']['status'], 'candidate')
        self.assertAlmostEqual(d['skyVisibilityCandidate'], 1-64/255)

    def test_pair_flags(self):
        for k, v in (('copied', False), ('nativeDispatchSeen', False), ('nativeDispatches', 2)):
            r = paired_report()
            r['textureReadback']['bufferPair'][k] = v
            with self.assertRaises(ValueError):
                decoder.decode(r, True)

    def test_pair_binding_and_resource(self):
        for k, v in (('giOffset', 0), ('giRootIndex', 64), ('giResource', 888), ('exposureHex', '00'*64)):
            r = paired_report()
            r['textureReadback']['bufferPair'][k] = v
            with self.assertRaises(ValueError):
                decoder.decode(r, True)

    def test_gpu_store_guard(self):
        r = paired_report()
        r['textureReadback']['bufferPair']['exposureHex'] = '00'*128
        self.assertEqual(decoder.decode(r, True)['gpuExposureInverseUnavailable'], 'gpu-exposure-store-control-mismatch')

    def test_pair_no_layout(self):
        d = decoder.decode(paired_report(), False)
        self.assertTrue(d['giGpuFramePaired'])
        self.assertNotIn('skyVisibilityCandidate', d)
        self.assertIsNone(d['gpuExposureInverse'])


if __name__ == '__main__':
    unittest.main()
