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


def v3_report(from_srv=False):
    r = paired_report()
    r['format'] = 'private-spatial-readback-v3'
    pair = r['textureReadback']['bufferPair']
    srv, table = [0]*64, [0]*64
    if from_srv:
        srv[2] = pair['nativeCbv'][2]
        pair['nativeCbv'] = [0]*64
    pair.update(nativeSrv=srv, nativeTable=table, descriptorHeaps=[0]*4, giFromSrv=from_srv,
                rootThreadConflict=False, tableSets=0, heapSets=1,
                rootSetsBeforeExposure=2, rootSetsInsideExposure=0)
    return r


def v4_report(gi_at=4096, exposure_at=2048, copies=1):
    """Whole-buffer, same-submission pairing: offsets are found, not bound."""
    r = v3_report()
    r['format'] = 'private-spatial-readback-v4'
    t = r['textureReadback']
    pair = t['bufferPair']
    cpu_gi = bytes.fromhex(t['context']['giBeforeHex'])
    whole_gi = bytearray(65536)
    for n in range(copies):
        whole_gi[gi_at+n*8192:gi_at+n*8192+768] = cpu_gi
    lanes = bytes.fromhex(pair['exposureHex'])
    whole_exposure = bytearray(65536)
    if exposure_at is not None:
        whole_exposure[exposure_at:exposure_at+128] = lanes
    t['context']['exposureCacheHex'] = lanes[:64].hex()
    pair.update(giHex=bytes(whole_gi).hex(), exposureHex=bytes(whole_exposure).hex(),
                giBytes=65536, exposureBytes=65536, giBindingHits=0, exposureBindingHits=0,
                tableSets=98, heapSets=6, pairing='same-submission-not-binding-proven')
    return r


def v5_report(count=3, broken=None):
    """A repeated series: one entry per transaction, each independently decodable."""
    base = v4_report()
    entries = []
    for i in range(count):
        single = v4_report()
        t = single['textureReadback']
        t['frame'] = 42+i
        t['context']['frame'] = 42+i
        t['fenceValue'] = i+1
        if broken is not None and i == broken:
            t['gpuCompleted'] = False
        entries.append(t)
    return dict(format='private-spatial-readback-v5', executableSha256=decoder.EXE,
                controlProgressed=True, requestedTransactions=count, completedTransactions=count,
                transactionIntervalMilliseconds=500, transactions=entries,
                textureReadback=base['textureReadback'])


def with_native(offsets=((2, 0, 0), (0, -5, 0)), break_reference=False, break_offset=None):
    """A v4 report carrying natively sampled values the decoder must recompute."""
    r = v4_report()
    t = r['textureReadback']
    decoded = decoder.decode(v4_report(), True)
    volume = bytes.fromhex(t['packedTextureHex'])
    constants = bytes.fromhex(t['bufferPair']['giHex'])[decoded['giWindowOffset']:][:768]
    world = decoded['gpuReference']['referenceWorldCandidate']
    clip = decoded['gpuReference']['selectedClipmap']
    entries = []
    for index, delta in enumerate(offsets):
        at = [world[j]+delta[j] for j in range(3)]
        value = decoder.sample_world(volume, constants, clip, at)
        if break_offset == index:
            value += 0.25
        entries.append(dict(delta=list(delta), status=0, skyVisibility=value))
    reference = decoded['skyVisibilityCandidate'] + (0.5 if break_reference else 0)
    t['nativeSamples'] = dict(available=True, giWindowOffset=decoded['giWindowOffset'],
                              status=0, clipmap=clip, world=world, reference=reference,
                              offsets=entries)
    return r


def segment_world(free=0.30, wall=None, wall_span=None, size=(64, 32, 264)):
    """A synthetic volume plus constants where clipmap 1 covers everything.

    x runs along the marched segment. `wall` fills a span of x with a low value.
    """
    constants = bytearray(768)

    def lane(offset, index, value):
        struct.pack_into('<f', constants, offset+index*4, value)
    for axis, value in enumerate((0.03125, 0.0625, 0.03125)):
        lane(0x10, axis, value)
    for axis in range(3):
        lane(0x130, axis, 0.0)          # wrapped: the anchor itself
        lane(0x2E0, axis, 0.0)
    for level in range(1, 8):
        for axis in range(3):
            lane(0x140+level*16, axis, 0.0)
            lane(0x240+level*16, axis, 0.0)
        lane(0x140+level*16, 3, 2.0**(2-level))
    byte = round((1-free)*255)
    volume = bytearray([byte])*(size[0]*size[1]*size[2])
    if wall is not None:
        low = round((1-wall)*255)
        for x in range(*wall_span):
            for z in range(size[2]):
                for y in range(size[1]):
                    volume[(z*size[1]+y)*size[0]+(x % size[0])] = low
    return bytes(volume), bytes(constants)


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

    def test_v3_pairs_like_v2(self):
        # The widened root window changes where bindings are seen, not the model.
        d = decoder.decode(v3_report(), True)
        self.assertTrue(d['giGpuFramePaired'] and d['exposureGpuFramePaired'])
        self.assertEqual(d['skyVisibilityCandidate'], decoder.decode(paired_report(), True)['skyVisibilityCandidate'])

    def test_v3_root_srv_binding(self):
        # GI bound as a root SRV must validate against nativeSrv, not nativeCbv.
        d = decoder.decode(v3_report(True), True)
        self.assertTrue(d['giGpuFramePaired'])

    def test_v3_root_srv_must_match_its_own_array(self):
        r = v3_report(True)
        r['textureReadback']['bufferPair']['nativeSrv'] = [0]*64
        with self.assertRaises(ValueError):
            decoder.decode(r, True)

    def test_v3_root_thread_conflict_rejected(self):
        for value in (True, None):
            r = v3_report()
            r['textureReadback']['bufferPair']['rootThreadConflict'] = value
            with self.assertRaises(ValueError):
                decoder.decode(r, True)

    def test_v2_still_accepted(self):
        self.assertTrue(decoder.decode(paired_report(), True)['giGpuFramePaired'])

    def test_v4_finds_the_window(self):
        d = decoder.decode(v4_report(), True)
        self.assertEqual(d['giWindowOffset'], 4096)
        self.assertEqual(d['exposureWindowOffset'], 2048)
        self.assertFalse(d['bindingProven'])
        self.assertEqual(d['status'], 'same-submission-texture-and-buffers')
        self.assertEqual(d['rootCorroboration']['tableSets'], 98)
        # The located window must reproduce the v3 result exactly.
        self.assertEqual(d['skyVisibilityCandidate'], decoder.decode(v3_report(), True)['skyVisibilityCandidate'])
        self.assertEqual(d['gpuExposureInverse'], decoder.decode(v3_report(), True)['gpuExposureInverse'])

    def test_v4_absent_and_ambiguous_gi_window(self):
        r = v4_report()
        r['textureReadback']['bufferPair']['giHex'] = ('00'*65536)
        with self.assertRaises(ValueError):
            decoder.decode(r, True)
        with self.assertRaises(ValueError):
            decoder.decode(v4_report(copies=2), True)

    def test_v4_exposure_window_absent_is_reported_not_guessed(self):
        d = decoder.decode(v4_report(exposure_at=None), True)
        self.assertEqual(d['gpuExposureInverseUnavailable'], 'exposure-window-absent')
        self.assertNotIn('gpuExposureInverse', d)
        self.assertEqual(d['giWindowOffset'], 4096)

    def test_v4_requires_the_pairing_label_and_sizes(self):
        for key, value in (('pairing', 'binding-proven'), ('giBytes', 1024), ('exposureBytes', 99)):
            r = v4_report()
            r['textureReadback']['bufferPair'][key] = value
            with self.assertRaises(ValueError):
                decoder.decode(r, True)

    def test_v4_still_checks_resource_identity(self):
        r = v4_report()
        r['textureReadback']['bufferPair']['giResource'] = 888
        with self.assertRaises(ValueError):
            decoder.decode(r, True)

    def test_v5_series_decodes_each_transaction(self):
        d = decoder.decode_series(v5_report(3), True)
        self.assertEqual(d['decodedTransactions'], 3)
        self.assertEqual(len(d['transactions']), 3)
        self.assertEqual([t['decoded']['frame'] for t in d['transactions']], [42, 43, 44])
        # Identical inputs must produce zero spread, never an invented average.
        self.assertEqual(d['skyVisibilitySpread'], 0.0)
        self.assertEqual(d['skyVisibilityMin'], d['skyVisibilityMax'])
        self.assertEqual(d['completedTransactions'], 3)

    def test_v5_reports_an_undecodable_entry_instead_of_dropping_it(self):
        d = decoder.decode_series(v5_report(3, broken=1), True)
        self.assertEqual(d['decodedTransactions'], 2)
        self.assertEqual(len(d['transactions']), 3)
        self.assertEqual(d['transactions'][1]['status'], 'undecodable')
        self.assertEqual(d['transactions'][1]['reason'], 'no-completed-gpu-copy')

    def test_v5_requires_transactions(self):
        r = v5_report(1)
        r['transactions'] = []
        with self.assertRaises(ValueError):
            decoder.decode_series(r, True)

    def test_v5_single_entry_matches_the_v4_result(self):
        series = decoder.decode_series(v5_report(1), True)
        single = decoder.decode(v4_report(), True)
        self.assertEqual(series['transactions'][0]['decoded']['skyVisibilityCandidate'],
                         single['skyVisibilityCandidate'])

    def test_series_helper_ignores_older_formats(self):
        self.assertIsNone(decoder.decode_series(v4_report(), True))
        self.assertIsNone(decoder.decode_series(paired_report(), True))

    def test_native_sampler_agreement_is_recomputed(self):
        d = decoder.decode(with_native(), True)
        self.assertEqual(d['nativeSampler']['checked'], 3)
        self.assertTrue(d['nativeSampler']['agrees'])
        self.assertEqual(d['nativeSampler']['disagreements'], [])

    def test_native_sampler_reference_disagreement_is_reported(self):
        d = decoder.decode(with_native(break_reference=True), True)
        self.assertFalse(d['nativeSampler']['agrees'])
        self.assertEqual(d['nativeSampler']['disagreements'][0]['delta'], [0, 0, 0])

    def test_native_sampler_offset_disagreement_is_reported(self):
        d = decoder.decode(with_native(break_offset=1), True)
        self.assertFalse(d['nativeSampler']['agrees'])
        bad = d['nativeSampler']['disagreements'][0]
        self.assertEqual(bad['delta'], [0, -5, 0])
        self.assertAlmostEqual(bad['native'] - bad['recomputed'], 0.25)

    def test_native_sampler_absent_or_unavailable_is_not_claimed(self):
        self.assertNotIn('nativeSampler', decoder.decode(v4_report(), True))
        r = with_native()
        r['textureReadback']['nativeSamples']['available'] = False
        self.assertNotIn('nativeSampler', decoder.decode(r, True))

    def test_march_clear_segment(self):
        volume, constants = segment_world(free=0.30)
        r = decoder.march_segment(volume, constants, [0, 0, 0], [12, 0, 0])
        self.assertEqual(r['verdict'], 'clear')
        self.assertGreater(r['minimum'], decoder.CLEARLY_FREE)
        self.assertEqual(r['lowRun'], 0.0)
        self.assertEqual(r['uncovered'], 0)

    def test_march_reports_a_thick_wall_as_blocked(self):
        volume, constants = segment_world(free=0.30, wall=0.0, wall_span=(5, 9))
        r = decoder.march_segment(volume, constants, [0, 0, 0], [12, 0, 0])
        self.assertEqual(r['verdict'], 'blocked')
        self.assertGreaterEqual(r['lowRun'], 1.0)

    def test_march_will_not_claim_a_blockage_when_both_ends_are_enclosed(self):
        # The failure mode measured in game: free air under a roof reads as low as
        # solid rock, so a low minimum there proves nothing about a wall between.
        volume, constants = segment_world(free=0.002)
        r = decoder.march_segment(volume, constants, [0, 0, 0], [12, 0, 0])
        self.assertEqual(r['verdict'], 'unknown-enclosed')
        self.assertLess(r['endpointMax'], decoder.CLEARLY_FREE)

    def test_march_reports_uncovered_points_instead_of_skipping_them(self):
        volume, constants = segment_world(free=0.30)
        # Push the far end outside every clipmap by using a huge offset.
        r = decoder.march_segment(volume, constants, [0, 0, 0], [4000, 0, 0])
        self.assertEqual(r['verdict'], 'unknown-uncovered')
        self.assertGreater(r['uncovered'], 0)

    def test_march_refuses_a_segment_shorter_than_its_endpoint_margins(self):
        volume, constants = segment_world(free=0.30)
        self.assertEqual(decoder.march_segment(volume, constants, [0, 0, 0], [1, 0, 0])['verdict'],
                         'unknown-too-short')

    def test_select_clipmap_prefers_the_finest_covering_level(self):
        _, constants = segment_world()
        self.assertEqual(decoder.select_clipmap(constants, [0, 0, 0]), 1)

    def test_pair_no_layout(self):
        d = decoder.decode(paired_report(), False)
        self.assertTrue(d['giGpuFramePaired'])
        self.assertNotIn('skyVisibilityCandidate', d)
        self.assertIsNone(d['gpuExposureInverse'])


if __name__ == '__main__':
    unittest.main()
