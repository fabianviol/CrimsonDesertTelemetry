"""Decode the engine's 1024-byte PrecomputedAmbientConstantBuffer.

Layout, from `GenerateAmbientFromEnvironmentAtmosphericScatteringCS` and the
`{ 8 x float4, [56 x float4] }` declaration its consumers share: eight sets of
eight `float4`. Within a set, slots 0..5 are three channels of two `float4`,
slot 6 holds each channel's ninth coefficient in x/y/z, and slot 7 is a
separately computed summary that is not part of the harmonics.

That gives three channels of nine coefficients, matching the producer's
groupshared `SHColor2`, which DXC split into two vector members and one scalar
member per channel.

WHAT IS ESTABLISHED: the split into 3 x 9, and that coefficient 0 is the DC
term -- it is the largest by magnitude in every channel of every populated set
measured so far, as it must be for a smooth environment.

WHAT IS NOT: the order of coefficients 1..8. No shader in the local listings
reads slots 0..6, so nothing here pins the basis convention. Do not evaluate a
direction from this until that is settled.

  python scripts/Decode-AmbientSH.py BUFFER.bin
"""
import struct
import sys

SET_STRIDE = 8          # float4 per set
SETS = 8
CHANNELS = 'RGB'


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
        print('=== set %d  (slots %d..%d) ===' % (index, index * SET_STRIDE,
                                                  index * SET_STRIDE + 7))
        if not carries_harmonics(one_set):
            # A set whose harmonic slots are empty is carrying something else;
            # slot 56 in the lantern capture holds a bare RGB triple.
            for slot, values in enumerate(one_set):
                if any(v != 0.0 for v in values):
                    print('  slot %-2d %s' % (index * SET_STRIDE + slot,
                                              ' '.join('%12.6f' % v for v in values)))
            print('  no harmonics in slots 0..6 of this set')
            print()
            continue
        coefficients, direct, peak = describe(one_set)
        for channel in range(3):
            print('  %s %s' % (CHANNELS[channel],
                               ' '.join('%10.6f' % v for v in coefficients[channel])))
        print('  largest coefficient per channel at index %s%s'
              % (peak, '' if peak == [0, 0, 0] else '   <-- NOT the DC term, check the layout'))
        print('  DC  R=%.6f G=%.6f B=%.6f   relative to R: %s'
              % (direct[0], direct[1], direct[2],
                 ' '.join('%.3f' % (v / direct[0]) for v in direct)))
        print('  slot 7 (not harmonics): %s'
              % ' '.join('%.6f' % v for v in one_set[7]))
        print()
    return 0


if __name__ == '__main__':
    sys.exit(main())
