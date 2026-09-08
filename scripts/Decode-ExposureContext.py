"""Offline diagnostic for the measured AdaptExposureCS cache, NOT a public feed.

Input: Read-AmbientProbe.ps1 v2 JSON or Capture-ExposureContext.ps1 JSON.
The explicit layout assumption does not prove a live PSO, GPU frame age, texture
coverage, player-local illumination, or individual light-source visibility.
See docs/LOCAL_ILLUMINATION_RESEARCH.md for derivation and evidence.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct


def ir_float(hex_double):
    """DXIL float constants are printed as their exact double representation."""
    return struct.unpack('>d', bytes.fromhex(hex_double))[0]


MIN_L = ir_float('3EB0C6F7A0000000')  # float32 1e-6
LOW = ir_float('3F1A36E2E0000000')  # float32 1e-4
PIVOT = ir_float('3F847AE140000000')  # float32 .01
HIGH_SCALE = ir_float('3FC24FD700000000')
LOW_SCALE = ir_float('405940A580000000')
# Diagnostic tolerance for FP32/GPU log and arithmetic; not a physical accuracy.
EV_ERROR = 2e-5
LAYOUT = 'adapt-exposure-ac80bf15-25116796'


def saturate(x):
    return min(1.0, max(0.0, x))


def curve(luminance):
    if not math.isfinite(luminance) or luminance < MIN_L:
        raise ValueError('invalid-histogram-luminance')
    x = min(7.0, max(LOW, luminance))
    u = saturate((x - PIVOT) * HIGH_SCALE)
    w = saturate((x - LOW) * LOW_SCALE)
    a = 2*w - 3.5 + (3 - 2*w)*u
    b = (3*w - 3)*(1 - u)
    return a, b


def forward_ev(luminance, visibility):
    """Reference algebra; DXIL op23 Log is log2, NOT natural logarithm."""
    if not math.isfinite(visibility) or not 0 <= visibility <= 1:
        raise ValueError('invalid-visibility')
    a, b = curve(luminance)
    return math.log2(8*luminance) - b - (a-b)*visibility**0.25


def infer_visibility(luminance, ev):
    if not math.isfinite(ev):
        raise ValueError('nonfinite-ev')
    a, b = curve(luminance)
    slope = a-b
    if abs(slope) < 0.01:
        raise ValueError('ill-conditioned-inverse')
    root = (math.log2(8*luminance) - b - ev)/slope
    error = EV_ERROR/abs(slope)
    # Do not turn incompatible/torn data into plausible 0 or 1 by clamping.
    if root < -error or root > 1+error:
        raise ValueError('outside-shader-model')
    value = saturate(root)**4
    return {
        'skyVisibilityCandidate': value,
        'numericalInterval': [saturate(root-error)**4, saturate(root+error)**4],
        'fourthRootBeforeRoundoffClamp': root,
        'evResidual': ev-forward_ev(luminance, value),
        'coverage': 'unknown; shader also returns 1 outside supported clipmaps',
    }


def decode_cache(cache, assume_layout=False):
    result = {'status': 'unavailable', 'reason': 'cache-unavailable',
              'gpuFramePaired': False, 'sourceFrameAgeKnown': False}
    if not isinstance(cache, dict):
        return result
    try:
        if cache.get('source') != 'engine-gpu-readback-cpu-cache':
            raise ValueError('unexpected-source')
        if cache.get('flags') != 31:
            raise ValueError('cache-not-stable')
        before = bytes.fromhex(cache['rawBeforeHex'])
        after = bytes.fromhex(cache['rawAfterHex'])
        if len(before) != 64 or before != after:
            raise ValueError('cache-size-or-copy-mismatch')
        if (cache.get('wrapperStride') != 4 or cache.get('innerMode') != 2 or
                not 20 <= cache.get('wrapperCount', 0) <= 16384):
            raise ValueError('cache-layout-mismatch')
        result['rawHex'] = before.hex().upper()
        if not assume_layout:
            raise ValueError('shader-layout-not-assumed')
        lanes = struct.unpack('<16f', before)
        # Packed lane6 can be NaN; never validate all lanes as floating point.
        if not all(math.isfinite(lanes[i]) for i in (0, 1, 5, 8, 13, 14, 15)):
            raise ValueError('nonfinite-semantic-lane')
        if lanes[0] <= 0 or lanes[1] <= 0 or lanes[14] < 0:
            raise ValueError('invalid-semantic-lane')
        if before[32:36] != before[60:64] or lanes[13] != 1:
            raise ValueError('shader-store-control-mismatch')
        result.update(infer_visibility(lanes[8], lanes[5]))
        result.update(status='candidate', reason=None, layoutAssumption=LAYOUT,
                      histogramLuminanceClamped=lanes[8],
                      histogramAtShaderFloor=lanes[8] == MIN_L,
                      preAdaptationEv=lanes[5], adaptedExposure=lanes[0])
    except (ValueError, KeyError, TypeError, struct.error) as exc:
        result.update(status='unavailable', reason=str(exc))
    return result


def decode_report(source, assume_layout=False):
    if source.get('format') not in ('private-ambient-probe-v2',
                                   'private-exposure-context-v1'):
        raise ValueError('Expected validated ambient v2 or exposure context JSON')
    if source['format'] == 'private-exposure-context-v1' and source.get('controlProgressed') is not True:
        raise ValueError('Live recording has no progressing control')
    samples = source.get('samples')
    if not isinstance(samples, list) or not 1 <= len(samples) <= 1200:
        raise ValueError('Expected 1..1200 bounded samples')
    rows = []
    for sample in samples:
        rows.append({**{k: sample[k] for k in ('sequence', 'frame', 'capturedTick',
                                              'camera') if k in sample},
                     **decode_cache(sample.get('exposureCache'), assume_layout)})
    return {
        'format': 'private-exposure-context-derived-v1',
        'layoutAssumed': assume_layout, 'publicTelemetry': False,
        'scope': 'engine clipmap reference-origin sky-visibility model; NOT player irradiance',
        'caveat': 'Inverse of shader intermediate, not direct texture readback. '
                  'GPU age and clipmap coverage unknown; 1 may be fallback. '
                  'Histogram input is scene color and is clamped, not ambient RGB. '
                  'Not source-to-camera occlusion, direct sun shadow or lux.',
        'statusCounts': dict(Counter(row['status'] for row in rows)),
        'samples': rows,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--input', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--assume-adapt-exposure-layout', action='store_true')
    args = ap.parse_args()
    artifacts = Path(__file__).resolve().parents[1] / 'artifacts'
    if args.out.exists() or not args.out.resolve().is_relative_to(artifacts):
        ap.error('Use a fresh output under product artifacts/')
    if args.input.stat().st_size > 32*1024*1024:
        ap.error('Input exceeds 32 MiB diagnostic bound')
    data = args.input.read_bytes()
    report = decode_report(json.loads(data), args.assume_adapt_exposure_layout)
    report.update(source=str(args.input.resolve()), sourceSha256=hashlib.sha256(data).hexdigest())
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('x', encoding='utf-8') as target:
        json.dump(report, target, indent=2, allow_nan=False)
    print(json.dumps({'output': str(args.out), 'counts': report['statusCounts']}))


if __name__ == '__main__':
    main()
