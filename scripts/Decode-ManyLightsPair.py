"""Offline comparison of one fenced input/output diagnostic, NOT an API decoder.

Input occupancy/lifetime and color semantics are intentionally not inferred.
Input coordinate interpretation must be explicitly selected by the caller.
Known-position controls can test it. Output count applies ONLY to output.
"""
import argparse
import json
import math
import struct
from pathlib import Path

HEADER = struct.Struct('<12I10Q')
COUNT, STRIDE, SCENE, COUNTER = 32768, 48, 2816, 256
LIGHT_BYTES = COUNT * STRIDE
FILE_BYTES = HEADER.size + SCENE + 2 * LIGHT_BYTES + COUNTER


def decode(data, anchors=(), input_space='camera-relative'):
    if input_space not in ('world', 'camera-relative'):
        raise ValueError('Explicit input coordinate convention required')
    if len(data) != FILE_BYTES:
        raise ValueError('Wrong size / partial snapshot')
    h = HEADER.unpack_from(data)
    if h[:8] != (0x50445443, 1, HEADER.size, SCENE, COUNT, STRIDE, COUNTER, 15):
        raise ValueError('Unsupported header or missing paired/fenced flags')
    pid, frame, bank = h[8:11]
    captured, completed, fence, owner, inp, out, counter, build, process_start = h[12:21]
    if not all((pid, fence, owner, inp, out, counter, process_start)) or inp == out or inp == counter or build != 25477059:
        raise ValueError('Invalid resource/process/build provenance')
    if completed < captured:
        raise ValueError('Invalid completion timestamp')
    scene = data[HEADER.size:HEADER.size+SCENE]
    if struct.unpack_from('<I', scene, 0x20)[0] != frame:
        raise ValueError('Scene/header frame mismatch')
    camera = struct.unpack_from('<3f', scene, 0x80)
    forward = struct.unpack_from('<3f', scene, 0x90)
    if not all(map(math.isfinite, camera + forward)) or not 0.95 < sum(v*v for v in forward) < 1.05:
        raise ValueError('Invalid paired camera')
    base = HEADER.size + SCENE
    counter_data = struct.unpack_from('<64I', data, base + LIGHT_BYTES)
    output_count = counter_data[1]
    if output_count > COUNT:
        raise ValueError('Invalid OUTPUT valid count')

    def inspect(offset, count, space):
        rows, zero, other, invalid = [], 0, 0, 0
        for slot in range(count):
            p = offset + slot * STRIDE
            raw = data[p:p+STRIDE]
            if raw == bytes(STRIDE):
                zero += 1
                continue
            values = struct.unpack_from('<8f', raw)
            if struct.unpack_from('<I', raw, 12)[0] != 0x40490FDB:
                other += 1
                continue
            if not all(map(math.isfinite, values)):
                invalid += 1
                continue
            raw_position = values[:3]
            relative = tuple(raw_position[i]-camera[i] for i in range(3)) if space == 'world' else raw_position
            world = raw_position if space == 'world' else tuple(relative[i]+camera[i] for i in range(3))
            rows.append({'slot': slot, 'rawPosition': raw_position, 'relativePosition': relative,
                         'worldPositionCandidate': world,
                         'rawColor': values[4:8],
                         'behindCamera': sum(relative[i]*forward[i] for i in range(3)) < 0})
        return {'piCandidates': len(rows), 'behindCandidates': sum(r['behindCamera'] for r in rows),
                'zeroSlots': zero, 'otherMarkerSlots': other, 'nonfinitePiSlots': invalid}, rows

    output_stats, output_rows = inspect(base, output_count, 'camera-relative')
    input_stats, input_rows = inspect(base + LIGHT_BYTES + COUNTER, COUNT, input_space)
    matches = []
    for name, position in anchors:
        def near(rows):
            return [row for row in rows if math.dist(row['worldPositionCandidate'], position) <= 0.45]
        matches.append({'name': name, 'worldAnchor': position,
                        'inputCandidates': near(input_rows), 'outputMatches': near(output_rows)})
    return {'pid': pid, 'processStartFileTime': process_start, 'frame': frame, 'bank': bank,
            'capturedTick': captured, 'completedTick': completed, 'fence': fence,
            'resources': {'input': hex(inp), 'output': hex(out), 'counter': hex(counter)},
            'camera': camera, 'forward': forward, 'inputPositionSpace': input_space, 'outputValidCount': output_count,
            'outputCounterWords': counter_data, 'input': input_stats, 'output': output_stats,
            'anchors': matches,
            'scope': 'Same-boundary GPU bytes. Input PI candidates are NOT proven current lights. '
                     'Input coordinate convention is caller-selected. Input/output RGB are NOT normalized. '
                     'Renderer inclusion is distinct from geometric line-of-sight, on-screen visibility and ON/OFF.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--input-space', choices=('world', 'camera-relative'), required=True)
    parser.add_argument('--anchor', nargs=4, action='append', default=[], metavar=('NAME', 'X', 'Y', 'Z'))
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    anchors = [(a[0], tuple(map(float, a[1:]))) for a in args.anchor]
    result = json.dumps(decode(args.capture.read_bytes(), anchors, args.input_space), indent=2, allow_nan=False)
    if args.out:
        with args.out.open('x', encoding='utf-8') as output:
            output.write(result + '\n')
    print(result)


if __name__ == '__main__':
    main()
