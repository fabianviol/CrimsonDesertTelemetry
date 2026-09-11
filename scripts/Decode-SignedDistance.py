"""Sample the engine's signed distance clipmap from a live R16 copy.

Addressing follows the sky-visibility volume's, at double resolution. The consumer
shader builds the z texel as `level*130 + 1 + frac(z)*128` over a total depth of
1040, and the world-to-normalised step reuses the same inverse extents at 0x10 of
the 768-byte GI constants, so a level spans 32 x 16 x 32 game units across
128 x 64 x 128 texels.

That predicts `cellSize(L) = 0.25 * 2^L`, which is independently what the constants
themselves carry: their per-level w at 0x140 + 16L is 1/cellSize.

The stored value is a signed distance in game units, clamped per level at
1.5*sqrt(2) cell sizes. Live surface crossings established negative inside and
positive in free space; see docs/SDF_VARIANT_A.md for the measured calibration.

  python scripts/Decode-SignedDistance.py PAYLOAD.bin PAYLOAD.json [--level L]
"""
import argparse
import json
import math
import struct
import sys

WIDTH, HEIGHT, DEPTH_PER_LEVEL, LEVELS = 128, 64, 130, 8
TOTAL_DEPTH = DEPTH_PER_LEVEL * LEVELS
CONTENT = 128
CELL_BASE = 0.25


def cell_size(level):
    return CELL_BASE * (1 << level)


def clamp_distance(level):
    """The per-level saturation value, measured as 1.5*sqrt(2) cell sizes."""
    return 1.5 * math.sqrt(2.0) * cell_size(level)


def half_to_float(bits):
    sign = (bits >> 15) & 1
    exponent = (bits >> 10) & 0x1F
    mantissa = bits & 0x3FF
    if exponent == 0:
        value = mantissa * 2.0 ** -24
    elif exponent == 31:
        value = float('inf') if mantissa == 0 else float('nan')
    else:
        value = (1024 + mantissa) * 2.0 ** (exponent - 25)
    return -value if sign else value


def texel(volume, x, y, z):
    index = ((z * HEIGHT + y) * WIDTH + x) * 2
    return half_to_float(volume[index] | (volume[index + 1] << 8))


def inverse_extents(constants):
    return struct.unpack_from('<4f', constants, 0x10)[:3]


def anchor(constants):
    """The world position the clipmap origins are measured from.

    `relative` divided by the level's own origin scale recovers it, and the result
    is identical across levels, which is what makes it usable away from the
    reference point.
    """
    origin = struct.unpack_from('<4f', constants, 0x140 + 16)
    relative = struct.unpack_from('<4f', constants, 0x240 + 16)
    return [relative[j] / origin[3] for j in range(3)]


def sample(volume, constants, level, world):
    """Trilinear sample at a world position, wrapping toroidally like the shader."""
    inverse = inverse_extents(constants)
    scale = 1.0 / (1 << level)
    normalised = [world[j] * inverse[j] * scale for j in range(3)]

    def wrapped(value, count):
        fraction = value - math.floor(value)
        return fraction * count

    fx, fy = wrapped(normalised[0], WIDTH), wrapped(normalised[1], HEIGHT)
    fz = wrapped(normalised[2], CONTENT)
    x0, y0, z0 = int(fx), int(fy), int(fz)
    tx, ty, tz = fx - x0, fy - y0, fz - z0
    base = level * DEPTH_PER_LEVEL + 1
    total = 0.0
    for dz in (0, 1):
        for dy in (0, 1):
            for dx in (0, 1):
                weight = (tx if dx else 1 - tx) * (ty if dy else 1 - ty) * (tz if dz else 1 - tz)
                if weight == 0.0:
                    continue
                total += weight * texel(volume,
                                        (x0 + dx) % WIDTH,
                                        (y0 + dy) % HEIGHT,
                                        base + (z0 + dz) % CONTENT)
    return total


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('payload')
    parser.add_argument('metadata')
    parser.add_argument('--level', type=int, default=0)
    parser.add_argument('--at', nargs=3, type=float, metavar=('X', 'Y', 'Z'),
                        help='world position; defaults to the clipmap anchor')
    parser.add_argument('--along', nargs=3, type=float, metavar=('DX', 'DY', 'DZ'),
                        help='walk this direction from --at and print a profile')
    parser.add_argument('--steps', type=int, default=24)
    parser.add_argument('--step', type=float, default=0.25)
    arguments = parser.parse_args()

    with open(arguments.payload, 'rb') as stream:
        volume = stream.read()
    if len(volume) != WIDTH * HEIGHT * TOTAL_DEPTH * 2:
        sys.exit('payload is %d bytes, expected %d' % (len(volume), WIDTH * HEIGHT * TOTAL_DEPTH * 2))
    with open(arguments.metadata, encoding='utf-8') as stream:
        meta = json.load(stream)
    constants = bytes.fromhex(meta['contextAfter']['giBeforeHex'])

    origin = arguments.at or anchor(constants)
    print('clipmap anchor      %.3f %.3f %.3f' % tuple(anchor(constants)))
    print('inverse extents     %s' % (inverse_extents(constants),))
    print('level %d: cell %.3f gu, clamp %.4f gu' %
          (arguments.level, cell_size(arguments.level), clamp_distance(arguments.level)))
    print()

    if not arguments.along:
        print('value at %.3f %.3f %.3f: %.5f' % (origin[0], origin[1], origin[2],
                                                 sample(volume, constants, arguments.level, origin)))
        return 0

    length = math.sqrt(sum(d * d for d in arguments.along))
    direction = [d / length for d in arguments.along]
    print('   t        x         y         z        value')
    for i in range(arguments.steps + 1):
        t = i * arguments.step
        point = [origin[j] + direction[j] * t for j in range(3)]
        value = sample(volume, constants, arguments.level, point)
        bar = '#' * min(40, int(abs(value) / max(clamp_distance(arguments.level), 1e-9) * 40))
        print('%5.2f %9.2f %9.2f %9.2f %10.5f  %s%s' %
              (t, point[0], point[1], point[2], value, '-' if value < 0 else ' ', bar))
    return 0


if __name__ == '__main__':
    sys.exit(main())
