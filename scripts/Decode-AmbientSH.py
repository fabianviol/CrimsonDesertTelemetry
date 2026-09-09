"""Decode the engine's 1024-byte PrecomputedAmbientConstantBuffer.

Layout, from `GenerateAmbientFromEnvironmentAtmosphericScatteringCS` and the
`{ 8 x float4, [56 x float4] }` declaration its consumers share: eight sets of
eight `float4`. Within a set, slots 0..5 are three channels of two `float4`,
slot 6 holds each channel's ninth coefficient in x/y/z, and slot 7 is a
separately computed summary that is not part of the harmonics.

The sets are not eight of the same thing. The shader writes this frame's
projection, scaled by 1/6144, into `_renderFlags.x * 8 + 8`, then sums SIX ring
entries at `i * 8 + 8` for i in 0..5 and stores that sum unscaled into slots
0..6. So sets 1..6 are a six-frame ring, set 0 is their sum, and set 7 lies
outside the ring with unknown contents.

The stored values are moments against the L0-L2 basis, NOT canonical spherical
harmonic coefficients: the producer samples a uniform grid in projected space
and applies no solid-angle weight, so the integration measure is missing.

That gives three channels of nine coefficients, matching the producer's
groupshared `SHColor2`, which DXC split into two vector members and one scalar
member per channel.

The basis order is not inferred: the producer multiplies each lane by a literal
constant, and all six distinct constants match the textbook real spherical
harmonic normalisations to float32 precision. Lane 0 carries 0.2820948, which is
Y00 itself, so the direct term is identified by its own constant rather than by
being the largest. Several sign conventions for real SH are in use, so rather than
appealing to a textbook: this engine's convention is the one listed in BASIS below,
with z as the polar axis.

  python scripts/Decode-AmbientSH.py BUFFER.bin
"""
import struct
import sys

SET_STRIDE = 8          # float4 per set
SETS = 8
CHANNELS = 'RGB'
RING = range(1, 7)      # sets 1..6, the six-frame ring the shader sums


def role(index):
    if index == 0:
        return 'the sum of the six ring entries, unscaled'
    if index in RING:
        return 'ring entry %d of 6' % index
    return 'outside the ring, contents unknown'


# Lane -> (name, the producer's own expression in the sampled direction d).
BASIS = [
    ('Y00  ', '0.2820948'),
    ('Y1-1 ', '-0.4886025 * d.y'),
    ('Y10  ', '+0.4886025 * d.z'),
    ('Y11  ', '-0.4886025 * d.x'),
    ('Y2-2 ', '+1.0925484 * d.x * d.y'),
    ('Y2-1 ', '-1.0925484 * d.y * d.z'),
    ('Y20  ', '0.9461747 * d.z * d.z - 0.3153916'),
    ('Y21  ', '-1.0925484 * d.x * d.z'),
    ('Y22  ', '0.5462742 * (d.x * d.x - d.y * d.y)'),
]


def sets_from_buffer(data):
    """The eight sets, each as a list of eight (x, y, z, w) tuples."""
    if len(data) < 1024:
        raise ValueError('expected 1024 bytes, got %d' % len(data))
    values = struct.unpack('<256f', data[:1024])
    slots = [values[i * 4:i * 4 + 4] for i in range(64)]
    return [slots[s * SET_STRIDE:(s + 1) * SET_STRIDE] for s in range(SETS)]


def harmonics(one_set):
    """Three channels of nine coefficients from a set's slots 0..6."""
    out = []
    for channel in range(3):
        pair = list(one_set[channel * 2]) + list(one_set[channel * 2 + 1])
        out.append(pair + [one_set[6][channel]])
    return out


def is_populated(one_set):
    return any(value != 0.0 for slot in one_set for value in slot)


def carries_harmonics(one_set):
    """A set holds harmonics only if every channel's own pair carries something."""
    return all(any(v != 0.0 for v in one_set[channel * 2] + one_set[channel * 2 + 1])
               for channel in range(3))


def l1_orientation(channel):
    """Which way a channel's L1 band points.

    The producer weights lane 1 by -0.4886*y, lane 2 by +0.4886*z and lane 3 by
    -0.4886*x, so the band maps to (-L11, -L1m1, +L10). This is the ORIENTATION of
    the L1 band, not the physical first moment of the radiance: that would need the
    solid-angle measure the producer does not apply.
    """
    return (-channel[3], -channel[1], channel[2])


def describe(one_set):
    coefficients = harmonics(one_set)
    direct = [channel[0] for channel in coefficients]
    peak = [max(range(9), key=lambda i: abs(channel[i])) for channel in coefficients]
    return coefficients, direct, peak


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    with open(sys.argv[1], 'rb') as stream:
        data = stream.read()

    for index, one_set in enumerate(sets_from_buffer(data)):
        if not is_populated(one_set):
            continue
        print('=== set %d  (slots %d..%d) - %s ===' % (index, index * SET_STRIDE,
                                                      index * SET_STRIDE + 7,
                                                      role(index)))
        if not carries_harmonics(one_set):
            # Not every channel's pair is populated, so do not force the split.
            for slot, values in enumerate(one_set):
                if any(v != 0.0 for v in values):
                    print('  slot %-2d %s' % (index * SET_STRIDE + slot,
                                              ' '.join('%12.6f' % v for v in values)))
            print('  no harmonics in slots 0..6 of this set')
            print()
            continue
        coefficients, direct, peak = describe(one_set)
        for lane, (name, expression) in enumerate(BASIS):
            print('  %s %s   %s' % (name,
                                    ' '.join('%10.6f' % channel[lane] for channel in coefficients),
                                    expression))
        print('  largest coefficient per channel at index %s' % peak)
        print('  Y00 R=%.6f G=%.6f B=%.6f   relative to R: %s'
              % (direct[0], direct[1], direct[2],
                 ' '.join('%.3f' % (v / direct[0]) for v in direct)))
        print('  L1 band orientation, red channel (x, y, z): %.6f %.6f %.6f'
              % l1_orientation(coefficients[0]))
        print('  slot 7 (not harmonics): %s'
              % ' '.join('%.6f' % v for v in one_set[7]))
        print()
    return 0


if __name__ == '__main__':
    sys.exit(main())
