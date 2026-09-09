"""Private direct volume readback analysis. NOT local irradiance or a public feed.

v1 uses an unpaired CPU reference; v2 requires a fenced native-dispatch buffer pair.
R8_UNORM + linear WRAP at LOD0 comes from the documented SRV builder/sampler.
"""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import struct

_spec = importlib.util.spec_from_file_location('exposure_model', Path(__file__).with_name('Decode-ExposureContext.py'))
model = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(model)
DIMS = (64, 32, 264)
EXE = '4D99C15C58BD20A94D354D10AE395D1FAC777D59EF52CBA8080DC3FC8DC6F454'


def linear_wrap(data, coordinates, dims=DIMS):
    """8 spatial neighbors; normalized coordinates address texel centers at i+.5."""
    if len(data) != math.prod(dims) or len(coordinates) != 3 or not all(math.isfinite(c) for c in coordinates):
        raise ValueError('invalid-volume-or-coordinates')
    tex = [(c % 1.0)*n-.5 for c, n in zip(coordinates, dims)]
    low = [math.floor(v) for v in tex]
    frac = [v-i for v, i in zip(tex, low)]
    neighbors, value = [], 0.0
    for z in (0, 1):
        for y in (0, 1):
            for x in (0, 1):
                bits = (x, y, z)
                xyz = [(i+b) % n for i, b, n in zip(low, bits, dims)]
                weight = math.prod(f if b else 1-f for f, b in zip(frac, bits))
                raw = data[(xyz[2]*dims[1]+xyz[1])*dims[0]+xyz[0]]
                value += weight*raw/255.0
                neighbors.append(dict(xyz=xyz, byte=raw, weight=weight))
    return value, neighbors


def find_window(buffer, needle, align=4):
    """Locate a known CPU block inside a whole copied buffer.

    The live shader binds through descriptor tables, so no offset can be read
    from a root argument. The window is therefore found by content, and an
    ambiguous or absent match is reported rather than guessed.
    """
    hits, at = [], buffer.find(needle)
    while at != -1 and len(hits) <= 8:
        if at % align == 0:
            hits.append(at)
        at = buffer.find(needle, at+1)
    return hits


def decode(source, assume_layout=False):
    formats = ('private-spatial-readback-v1', 'private-spatial-readback-v2',
               'private-spatial-readback-v3', 'private-spatial-readback-v4',
               'private-spatial-readback-v5')
    paired = source.get('format') in formats[1:]
    whole = source.get('format') in ('private-spatial-readback-v4', 'private-spatial-readback-v5')
    if source.get('format') not in formats or source.get('executableSha256') != EXE:
        raise ValueError('wrong-format-or-build')
    if source.get('controlProgressed') is not True:
        raise ValueError('no-progressing-control')
    r = source['textureReadback']
    if (r.get('status') != ('gpu-complete-texture-and-buffers' if paired else 'gpu-complete-texture-only') or r.get('gpuCompleted') is not True or
            r.get('gpuCopyIssued') is not True or r.get('mapCalls') != 1 or r.get('fenceValue', 0) < 1):
        raise ValueError('no-completed-gpu-copy')
    raw = bytes.fromhex(r['packedTextureHex'])
    if len(raw) != math.prod(DIMS):
        raise ValueError('wrong-packed-volume-size')
    b = r['releaseBarrier']
    actual = (b['syncBefore'], b['syncAfter'], b['accessBefore'], b['accessAfter'] & 0xffffffff,
              b['layoutBefore'], b['layoutAfter'], b['flags'])
    if actual != (128, 0, 128, 0x80000000, 6, 1, 0):
        raise ValueError('wrong-release-tuple')
    o = r['context']
    if o.get('error') != 0 or o.get('frame') != r['frame'] or o.get('selectedForTextureReadback') is not True:
        raise ValueError('wrong-cpu-context')
    sampler = struct.unpack('<13I', bytes.fromhex(o['samplerHex']))
    if sampler != (0x14, 1, 1, 1, 0, 1, 8, 0, 0, 0x7f7fffff, 12, 4, 0):
        raise ValueError('sampler-changed')
    if bytes.fromhex(o['srvViewEntryRawHex'])[0] != 15:
        raise ValueError('typed-view-settings-changed')
    context = model.decode_spatial(dict(source='inline-voxel-gi-upload-source',
        status='stable-cpu-observation' if o.get('giCopiesMatch') is True else 'unstable',
        rawBeforeHex=o['giBeforeHex'], rawAfterHex=o['giAfterHex'], textureCpuDimensions=list(DIMS),
        textureResource=o['resource'], bankFlag=o['bankFlag']), assume_layout)
    if paired:
        pair = r['bufferPair']
        if (r.get('giGpuFramePaired') is not True or r.get('exposureGpuFramePaired') is not True or
                pair.get('requested') is not True or pair.get('copied') is not True or
                pair.get('nativeDispatchSeen') is not True or pair.get('nativeDispatches') != 1):
            raise ValueError('no-proven-buffer-pair')
        gpu_gi = bytes.fromhex(pair['giHex'])
        gpu_exposure = bytes.fromhex(pair['exposureHex'])
        if pair['giResource'] != o['giGpuResource'] or pair['exposureResource'] != o['exposureGpuResource']:
            raise ValueError('paired-resource-context-mismatch')
        if source.get('format') == 'private-spatial-readback-v3' and pair.get('rootThreadConflict') is not False:
            raise ValueError('root-thread-conflict')
        if not whole:
            if len(gpu_gi) != 768 or len(gpu_exposure) != 128:
                raise ValueError('wrong-gpu-buffer-sizes')
            for name, binding in (('gi', 'nativeSrv' if pair.get('giFromSrv') else 'nativeCbv'), ('exposure', 'nativeUav')):
                root = pair[name+'RootIndex']
                if (not isinstance(root, int) or not 0 <= root < 64 or len(pair[binding]) != 64 or
                        pair[binding][root] != pair[name+'Base']+pair[name+'Offset']):
                    raise ValueError('native-root-address-mismatch')
            windows = None
        else:
            # Same-submission pairing: whole buffers, offsets resolved here.
            if pair.get('pairing') != 'same-submission-not-binding-proven':
                raise ValueError('missing-pairing-label')
            if (len(gpu_gi) != pair.get('giBytes') or len(gpu_exposure) != pair.get('exposureBytes') or
                    not 768 <= len(gpu_gi) <= 65536 or not 128 <= len(gpu_exposure) <= 65536):
                raise ValueError('wrong-gpu-buffer-sizes')
            cpu_gi = bytes.fromhex(o['giBeforeHex'])
            gi_hits = find_window(gpu_gi, cpu_gi, 256)
            if len(gi_hits) != 1:
                raise ValueError('gi-window-absent' if not gi_hits else 'gi-window-ambiguous')
            windows = dict(giWindowOffset=gi_hits[0], giWindowSource='matched-cpu-gi-copy',
                           bindingProven=False, rootCorroboration=dict(
                               giBindingHits=pair.get('giBindingHits'),
                               exposureBindingHits=pair.get('exposureBindingHits'),
                               tableSets=pair.get('tableSets'), heapSets=pair.get('heapSets')))
            gpu_gi = gpu_gi[gi_hits[0]:gi_hits[0]+768]
            cpu_exposure = bytes.fromhex(o['exposureCacheHex'])
            exposure_hits = find_window(gpu_exposure, cpu_exposure, 4)
            if len(exposure_hits) == 1 and exposure_hits[0]+128 <= len(gpu_exposure):
                windows['exposureWindowOffset'] = exposure_hits[0]
                gpu_exposure = gpu_exposure[exposure_hits[0]:exposure_hits[0]+128]
            else:
                windows['exposureWindowUnavailable'] = (
                    'exposure-window-ambiguous' if exposure_hits else 'exposure-window-absent')
                gpu_exposure = None
        cpu_context = context
        context = model.decode_spatial(dict(source='paired-gpu-voxel-gi', status='fenced-gpu-readback',
            rawHex=gpu_gi.hex(), textureCpuDimensions=list(DIMS), textureResource=o['resource'],
            bankFlag=o['bankFlag']), assume_layout)
    result = dict(format='private-spatial-readback-derived-v1', publicTelemetry=False,
        status='direct-texture-with-unpaired-cpu-reference', frame=r['frame'], gpuTextureCompleted=True,
        giGpuFramePaired=False, exposureGpuFramePaired=False,
        caveat='Texture bytes are fenced. Sampling uses CPU GI constants, not paired GPU CB data. '
               'Candidate engine sky-visibility factor only, NOT room brightness, sun shadow or per-light occlusion.',
        textureSha256=hashlib.sha256(raw).hexdigest(), dimensions=list(DIMS),
        byteRange=[min(raw), max(raw)], distinctByteValues=len(set(raw)),
        byteHistogram=dict(sorted(Counter(raw).items())), cpuReference=context)
    if context['status'] != 'candidate':
        result['status'] = 'texture-only-reference-unavailable'
    elif context['shaderBranch'] == 'fallback-one':
        result.update(skyVisibilityCandidate=1.0, fallback=True)
    else:
        value, neighbors = linear_wrap(raw, context['sampleCoordinatesBeforeSampler'])
        result.update(sampledR8UnormCandidate=value, skyVisibilityCandidate=max(0.0, min(1.0, 1-value)),
                      neighbors=neighbors, fallback=False,
                      numericCaveat='CPU interpolation; hardware subtexel precision/fast math may differ.')
    if paired:
        result.update(status='paired-dispatch-texture-and-buffers' if context['status'] == 'candidate' else
                          'paired-texture-reference-unavailable', giGpuFramePaired=True, exposureGpuFramePaired=True,
            caveat='Texture, GI buffer and exposure output share the sampled native Dispatch and fence. '
                   'CPU scene frame/cache remains unpaired. Sky factor is NOT irradiance or source occlusion.',
            cpuReference=cpu_context, gpuReference=context,
            cpuGiEqualsGpu=gpu_gi == bytes.fromhex(o['giBeforeHex']))
        if windows is not None:
            result.update(windows, status='same-submission-texture-and-buffers'
                          if context['status'] == 'candidate' else 'same-submission-reference-unavailable',
                caveat='Texture, GI buffer and exposure output share one recording, one submission and one '
                       'fence with the selected native dispatch. Resource identity comes from the validated '
                       'native producer/consumer path, NOT from an observed root binding: this shader binds '
                       'through descriptor tables. Window offsets were matched against the CPU copies. '
                       'Sky factor is NOT irradiance, room brightness or source occlusion.')
        if gpu_exposure is None:
            result['gpuExposureInverseUnavailable'] = windows['exposureWindowUnavailable']
            return result
        # Interpret GPU stores as GPU stores, not by fabricating CPU-cache flags.
        lanes = struct.unpack('<32f', gpu_exposure)
        try:
            if (gpu_exposure[32:36] != gpu_exposure[60:64] or lanes[13] != 1 or
                    not all(math.isfinite(lanes[i]) for i in (0, 1, 5, 8, 13, 14, 15))):
                raise ValueError('gpu-exposure-store-control-mismatch')
            inverse = model.infer_visibility(lanes[8], lanes[5]) if assume_layout else None
            result['gpuExposureInverse'] = inverse
            if inverse is not None and 'skyVisibilityCandidate' in result:
                result['directInverseAbsoluteDifference'] = abs(result['skyVisibilityCandidate']-inverse['skyVisibilityCandidate'])
        except ValueError as exc:
            result['gpuExposureInverseUnavailable'] = str(exc)
    return result


def decode_series(source, assume_layout=False):
    """v5 carries one entry per repeated transaction. Each is decoded on its own.

    Repeats measure spread at one place. They are not an average, and a failed or
    undecodable entry is reported as such instead of being dropped from the set.
    """
    if source.get('format') != 'private-spatial-readback-v5':
        return None
    entries = source.get('transactions')
    if not isinstance(entries, list) or not entries:
        raise ValueError('missing-transactions')
    results, values = [], []
    for index, entry in enumerate(entries):
        single = dict(source)
        single['textureReadback'] = entry
        try:
            decoded = decode(single, assume_layout)
            results.append(dict(index=index, status=decoded['status'], decoded=decoded))
            if 'skyVisibilityCandidate' in decoded:
                values.append(decoded['skyVisibilityCandidate'])
        except ValueError as exc:
            results.append(dict(index=index, status='undecodable', reason=str(exc)))
    summary = dict(format='private-spatial-readback-series-v1', publicTelemetry=False,
                   requestedTransactions=source.get('requestedTransactions'),
                   completedTransactions=source.get('completedTransactions'),
                   intervalMilliseconds=source.get('transactionIntervalMilliseconds'),
                   decodedTransactions=len(values), transactions=results,
                   caveat='Repeated same-submission transactions at one place. Spread across entries '
                          'is measurement spread plus real scene change; it is not an accuracy bound, '
                          'not an average, and not room brightness or per-source occlusion.')
    if values:
        ordered = sorted(values)
        middle = len(ordered)//2
        summary.update(skyVisibilityMin=ordered[0], skyVisibilityMax=ordered[-1],
                       skyVisibilityMedian=ordered[middle] if len(ordered) % 2 else
                           (ordered[middle-1]+ordered[middle])/2,
                       skyVisibilitySpread=ordered[-1]-ordered[0])
    return summary


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--input', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--assume-adapt-exposure-layout', action='store_true')
    args = ap.parse_args()
    artifacts = Path(__file__).resolve().parents[1]/'artifacts'
    if args.out.exists() or not args.out.resolve().is_relative_to(artifacts):
        ap.error('Use a new output under product artifacts/')
    if args.input.stat().st_size > 32*1024*1024:
        ap.error('Input exceeds 32 MiB bound')
    data = args.input.read_bytes()
    parsed = json.loads(data)
    result = decode_series(parsed, args.assume_adapt_exposure_layout) or \
        decode(parsed, args.assume_adapt_exposure_layout)
    result.update(source=str(args.input.resolve()), sourceSha256=hashlib.sha256(data).hexdigest())
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('x', encoding='utf-8') as target:
        json.dump(result, target, indent=2, allow_nan=False)
    keys = ('decodedTransactions', 'skyVisibilityMin', 'skyVisibilityMedian', 'skyVisibilityMax',
            'skyVisibilitySpread') if 'transactions' in result else \
           ('status', 'frame', 'byteRange', 'giGpuFramePaired')
    print(json.dumps({k: result[k] for k in keys if k in result}))


if __name__ == '__main__':
    main()
