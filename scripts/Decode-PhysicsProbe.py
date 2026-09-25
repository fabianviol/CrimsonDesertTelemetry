"""Offline physics.2 report decoder for the validated 25477059 layout.

Never opens the game. Hit handles are opaque, process-local collision data, NOT
lamp IDs. A contact (including one near the source) is NOT optical occlusion.
Layout evidence and exact-build disassembly: docs/PHYSICS_QUERY_RESEARCH.md.
"""
import argparse
import json
import math
import struct
from pathlib import Path

EXE_HASH = '57da440d72f4db974f25fef047cf84c4dadd999a88cb2a3c5af4c9bd67fde1e7'


def vec(value):
    if not isinstance(value, (list, tuple)) or len(value) != 3:
        raise ValueError('Expected three coordinates')
    v = [float(x) for x in value]
    if not all(math.isfinite(x) and abs(x) <= 1_000_000 for x in v):
        raise ValueError('Invalid coordinates')
    return v


def decode(report):
    if report.get('schemaVersion') != 2 or report.get('executableSha256', '').lower() != EXE_HASH:
        raise ValueError('Unverified report schema or executable')
    result = dict(requestId=report['requestId'], pid=report['pid'],
                  nativeStatus=report.get('reason'), collision='unknown',
                  opticalVisibility='not-classified')
    required = ('segmentCalled', 'controlMatched', 'guardsIntact',
                'originalsPreserved', 'segmentResultPlausible')
    if not all(report.get(key) is True for key in required) or report.get('callException') != 0:
        return result  # Do not decode zero/default/stale output as a no-hit.
    shape = bytes.fromhex(report['shapeHex'])
    collector = bytes.fromhex(report['segmentCollectorHex'])
    if len(shape) < 0x6C or len(collector) < 0x120:
        raise ValueError('Truncated native bytes')
    base = report['moduleBase']
    if struct.unpack_from('<Q', shape)[0] != base + 0x530ACE8 or struct.unpack_from('<Q', collector)[0] != base + 0x5D13528:
        raise ValueError('Unverified sphere or collector vtable')
    radius = struct.unpack_from('<f', shape, 0x68)[0]
    count = struct.unpack_from('<I', collector, 0xC)[0]
    fraction = struct.unpack_from('<d', collector, 0x10)[0]
    if not math.isfinite(radius) or not 0 < radius <= 10 or count not in (0, 1) or not math.isfinite(fraction) or not -1 <= fraction <= 1.00001:
        raise ValueError('Implausible radius or closest-hit result')
    start, end, player = (vec(report[key]) for key in ('segmentStart', 'segmentEnd', 'player'))
    delta = [b-a for a, b in zip(start, end)]
    length = math.dist(start, end)
    if not .05 <= length <= 50:
        raise ValueError('Segment outside diagnostic bounds')
    result.update(radius=radius, start=start, end=end, length=length, fraction=fraction,
                  collision='hit' if count else 'no-hit')
    if not count:
        return result  # Unused hit payload can retain unrelated stack bytes.
    normal = vec(struct.unpack_from('<3f', collector, 0x80))
    local = vec(struct.unpack_from('<3d', collector, 0x90))
    surface = [local[0] + math.trunc(player[0]/1000)*1000, local[1],
               local[2] + math.trunc(player[2]/1000)*1000]
    center = [s + fraction*d for s, d in zip(start, delta)]
    expected = [p + radius*n for p, n in zip(surface, normal)]
    residual = math.dist(center, expected)
    # Collector +0x80 is a 0xA0-byte closest-hit copy. The engine resolver uses
    # hit+0x60/+0x70 (=collector E0/F0); +0x68 low24 has an invalid sentinel.
    result.update(normal=normal, sweepCenter=center, surfacePosition=surface,
                  distanceBeforeSource=(1-fraction)*length,
                  centerToSurface=math.dist(center, surface), radiusNormalResidual=residual,
                  geometryConsistent=abs(math.dist(normal, [0, 0, 0])-1) < .01 and residual < .005,
                  opaqueCollisionHandle=f"0x{struct.unpack_from('<Q', collector, 0xE0)[0]:016X}",
                  rawBodyId=f"0x{struct.unpack_from('<I', collector, 0xE8)[0]:08X}",
                  rawSubshapeSelector=f"0x{struct.unpack_from('<I', collector, 0xF0)[0]:08X}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', required=True, type=Path)
    parser.add_argument('--pattern', required=True)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Refusing existing output')
    rows, seen = [], set()
    for path in sorted(args.directory.glob(args.pattern)):
        source = json.loads(path.read_text(encoding='utf-8-sig'))
        key = (source.get('pid'), source.get('requestId'))
        if key in seen:
            raise ValueError(f'Duplicate request: {path}')
        seen.add(key)
        rows.append(dict(file=str(path), **decode(source)))
    if not rows:
        parser.error('No reports matched')
    with args.out.open('x', encoding='utf-8') as stream:
        json.dump(dict(scope='Offline collision evidence, NOT optical visibility', reports=rows), stream, indent=2, allow_nan=False)
    print(json.dumps(dict(reports=len(rows), hits=sum(r['collision']=='hit' for r in rows),
                          noHits=sum(r['collision']=='no-hit' for r in rows),
                          unknown=sum(r['collision']=='unknown' for r in rows), saved=str(args.out))))


if __name__ == '__main__':
    main()
