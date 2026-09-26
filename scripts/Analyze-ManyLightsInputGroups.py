"""Offline group/color controls for fenced ManyLights pairs; never a public feed.

Rules come from preserved PIX PSO475 (fa2075b2f713d5706aafe00ce7045e80).
Live agreement tests those rules, not the identity of the current GPU shader.
Only ordinary nonnegative RGB is converted; exposure-dependent special records,
blue-noise position selection, frustum/HiZ and other rejection rules are NOT replayed.
"""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import struct

spec = importlib.util.spec_from_file_location('pair_decode', Path(__file__).with_name('Decode-ManyLightsPair.py'))
pair = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pair)


def dx_float(hex_double):
    return struct.unpack('>d', bytes.fromhex(hex_double))[0]


# Exact literals in PSO475: luminance floor at lines725-728, matrix1086-1105.
WEIGHTS = tuple(map(dx_float, ('3FCB38CDA0000000', '3FE6E29740000000', '3FB279AAE0000000')))
FLOOR = dx_float('3FA99999A0000000')
MATRIX = tuple(tuple(map(dx_float, row)) for row in (
    ('3FE39EADE0000000', '3FD5BA8820000000', '3FA840E180000000'),
    ('3FB1F8A0A0000000', '3FED52D240000000', '3F8B8BAC80000000'),
    ('3F951D68C0000000', '3FBC0D6F60000000', '3FEBD566C0000000')))


def convert_rgb(rgb):
    if not all(math.isfinite(x) and x >= 0 for x in rgb):
        raise ValueError('Exposure-dependent special RGB is not implemented')
    floor = sum(rgb[i] * WEIGHTS[i] for i in range(3)) * FLOOR
    clipped = [max(x, floor) for x in rgb]
    return tuple(sum(row[i] * clipped[i] for i in range(3)) for row in MATRIX)


def analyze(data, anchors):
    decoded = pair.decode(data, anchors, 'world')
    # This is SAME structure-counter DWORD0, not output DWORD1 and not the
    # distinct producer/free-list counter investigated in September6 research.
    bound = decoded['outputCounterWords'][0]
    if bound > pair.COUNT:
        raise ValueError('Input dispatch bound exceeds capacity')
    base = pair.HEADER.size + pair.SCENE + pair.LIGHT_BYTES + pair.COUNTER
    records = [struct.unpack_from('<8f4i', data, base + i * pair.STRIDE) for i in range(bound)]
    groups, singles, member_skips, unsupported = [], [], [], []
    for slot, record in enumerate(records):
        count = max(0, min(record[11], 32767))
        if record[10] == -2 and count:
            # Refuse malformed capture groups rather than reading retained tail.
            if slot + count >= bound:
                raise ValueError('Group children cross input dispatch bound')
            member_slots = [i for i in range(slot + 1, slot + 1 + count) if records[i][7] < 0]
            members = [records[i] for i in member_slots]
            if any(not all(map(math.isfinite, r[:8])) for r in members):
                raise ValueError('Nonfinite group member')
            rgb = tuple(sum(r[4+i] for r in members) for i in range(3))
            group = {'headerSlot': slot, 'declaredCount': count, 'memberSlots': member_slots,
                     'summedInputRgb': rgb, 'convertedRgb': None}
            if members and all(x >= 0 for x in rgb):
                group['convertedRgb'] = convert_rgb(rgb)
                group['memberPositionBounds'] = [[min(r[i] for r in members), max(r[i] for r in members)]
                                                 for i in range(3)]
            groups.append(group)
        else:
            look_w = struct.unpack_from('<e', data, base + slot * pair.STRIDE + 46)[0]
            if record[7] < 0 and look_w < 0:
                member_skips.append(slot)  # skipped as standalone; may participate in header group
            elif all(math.isfinite(x) and x >= 0 for x in record[4:7]):
                if any(record[4:7]):
                    singles.append({'slot': slot, 'convertedRgb': convert_rgb(record[4:7])})
            else:
                unsupported.append(slot)

    results = []
    for anchor in decoded['anchors']:
        full = {r['slot'] for r in anchor['inputCandidates']}
        prefix = {i for i in full if i < bound}
        matched_groups = [g for g in groups if prefix.intersection(g['memberSlots'])]
        candidates = [{'kind': 'group', **g} for g in matched_groups]
        candidates += [{'kind': 'standalone', **s} for s in singles if s['slot'] in prefix]
        for candidate in candidates:
            rgb = candidate['convertedRgb']
            errors = [] if rgb is None else [
                {'outputSlot': o['slot'], 'outputRgb': o['rawColor'][:3],
                 'maxAbsoluteRgbError': max(abs(rgb[j] - o['rawColor'][j]) for j in range(3))}
                for o in anchor['outputMatches']]
            candidate['outputColorComparisons'] = sorted(errors, key=lambda e: e['maxAbsoluteRgbError'])
        results.append({'name': anchor['name'], 'position': anchor['worldAnchor'],
                        'fullCapacityPiMatches': len(full), 'withinInputBoundPiMatches': len(prefix),
                        'outsideInputBoundPiMatches': len(full-prefix),
                        'outputMatches': len(anchor['outputMatches']), 'candidates': candidates})
    return {'scope': __doc__.strip(), 'captureSha256': hashlib.sha256(data).hexdigest(),
            'pid': decoded['pid'], 'frame': decoded['frame'], 'fence': decoded['fence'],
            'inputDispatchBound': bound, 'outputValidCount': decoded['outputValidCount'],
            'groupHeadersInBound': len(groups), 'standaloneSkippedMemberRecords': len(member_skips),
            'unsupportedSpecialSlots': unsupported, 'anchors': results,
            'limits': 'Not a live-light inventory. Position lookup remains a tolerance match. '
                      'Group output positions require additional constants/noise. '
                      'Colors use double precision and allow GPU float-rounding differences.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--anchor', nargs=4, action='append', required=True, metavar=('NAME','X','Y','Z'))
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    anchors = [(a[0], tuple(map(float, a[1:]))) for a in args.anchor]
    result = json.dumps(analyze(args.capture.read_bytes(), anchors), indent=2, allow_nan=False)
    if args.out:
        with args.out.open('x', encoding='utf-8') as output:
            output.write(result + '\n')
    print(result)


if __name__ == '__main__':
    main()
