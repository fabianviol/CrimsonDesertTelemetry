"""Variant A: sphere-trace camera to light through the live distance clipmap.

The field stores approximate signed distance in game units, clamped per level at
1.5*sqrt(2) cell sizes. Neither conservatism of its interpolated distance nor the
forced minimum step is guaranteed: the 2026-09-12 camp regression skips a negative
interval. This script reproduces Variant A for diagnosis; a CLEAR verdict is not
a geometric acceptance proof. See docs/SOURCE_VISIBILITY_REGRESSION.md.
Level selection matters because each
level wraps toroidally over its own window, so a point outside that window aliases
onto the wrong texels; the finest level whose window contains the point is used.

The trace reports the smallest distance it saw and where, so an endpoint self-hit
at the light's own fixture can be told apart from a wall in between.

  python scripts/Trace-SignedDistance.py PAYLOAD.bin PAYLOAD.json --to X Y Z
  python scripts/Trace-SignedDistance.py PAYLOAD.bin PAYLOAD.json --lights lights.tsv --at T
"""
import argparse
import importlib.util
import json
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location('sdf', os.path.join(HERE, 'Decode-SignedDistance.py'))
sdf = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sdf)

# One level spans this many game units before it wraps, at level 0.
BASE_EXTENT = (32.0, 16.0, 32.0)


def level_window(constants, level):
    """(centre, half extent) of a level's non-aliasing window, in world units."""
    origin = struct.unpack_from('<4f', constants, 0x140 + 16 * level)
    centre = [origin[j] / origin[3] for j in range(3)] if origin[3] else [0.0, 0.0, 0.0]
    half = [BASE_EXTENT[j] * (1 << level) / 2.0 for j in range(3)]
    return centre, half


def finest_level(constants, point, margin=0.0):
    """The finest level whose window contains the point, or None."""
    for level in range(sdf.LEVELS):
        centre, half = level_window(constants, level)
        if all(abs(point[j] - centre[j]) <= half[j] - margin for j in range(3)):
            return level
    return None


def camera_of(meta):
    scene = bytes.fromhex(meta['contextAfter']['sceneHex'])
    return list(struct.unpack_from('<3f', scene, 0x80))


def trace(volume, constants, origin, target, hit_tolerance, minimum_step,
          iteration_bound, start_offset, end_margin):
    """Sphere trace, stopping short of the target so its own fixture is not a hit."""
    delta = [target[j] - origin[j] for j in range(3)]
    total = math.sqrt(sum(d * d for d in delta))
    if total <= start_offset + end_margin:
        return {'verdict': 'too-short', 'length': total}
    direction = [d / total for d in delta]
    travelled = start_offset
    limit = total - end_margin
    closest, closest_at, samples = float('inf'), 0.0, []
    for _ in range(iteration_bound):
        point = [origin[j] + direction[j] * travelled for j in range(3)]
        level = finest_level(constants, point)
        if level is None:
            return {'verdict': 'uncovered', 'at': travelled, 'length': total, 'samples': samples}
        value = sdf.sample(volume, constants, level, point)
        samples.append((travelled, level, value))
        if value < closest:
            closest, closest_at = value, travelled
        if value <= hit_tolerance:
            return {'verdict': 'blocked', 'at': travelled, 'value': value, 'level': level,
                    'length': total, 'closest': closest, 'closestAt': closest_at,
                    'samples': samples}
        travelled += max(value, minimum_step)
        if travelled >= limit:
            return {'verdict': 'clear', 'length': total, 'closest': closest,
                    'closestAt': closest_at, 'samples': samples}
    return {'verdict': 'iteration-bound', 'at': travelled, 'length': total,
            'closest': closest, 'closestAt': closest_at, 'samples': samples}


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('payload')
    parser.add_argument('metadata')
    parser.add_argument('--to', nargs=3, type=float, required=True, metavar=('X', 'Y', 'Z'))
    parser.add_argument('--from', dest='origin', nargs=3, type=float, metavar=('X', 'Y', 'Z'),
                        help='defaults to the camera in the payload metadata')
    parser.add_argument('--tolerance', type=float, default=0.0,
                        help='a sample at or below this counts as a hit')
    parser.add_argument('--min-step', type=float, default=0.05)
    parser.add_argument('--iterations', type=int, default=256)
    parser.add_argument('--start-offset', type=float, default=0.5)
    parser.add_argument('--end-margin', type=float, default=1.0)
    parser.add_argument('--profile', action='store_true')
    arguments = parser.parse_args()

    with open(arguments.payload, 'rb') as stream:
        volume = stream.read()
    with open(arguments.metadata, encoding='utf-8') as stream:
        meta = json.load(stream)
    constants = bytes.fromhex(meta['contextAfter']['giBeforeHex'])
    origin = arguments.origin or camera_of(meta)

    result = trace(volume, constants, origin, arguments.to, arguments.tolerance,
                   arguments.min_step, arguments.iterations,
                   arguments.start_offset, arguments.end_margin)
    print('from %.2f %.2f %.2f  to %.2f %.2f %.2f  length %.2f gu'
          % (origin[0], origin[1], origin[2], arguments.to[0], arguments.to[1],
             arguments.to[2], result['length']))
    print('verdict: %s' % result['verdict'])
    for key in ('at', 'value', 'level', 'closest', 'closestAt'):
        if key in result:
            print('  %-10s %s' % (key, result[key]))
    if arguments.profile:
        print('       t  level      value')
        for travelled, level, value in result.get('samples', []):
            print('  %6.2f %6d %10.5f' % (travelled, level, value))
    return 0


if __name__ == '__main__':
    sys.exit(main())
