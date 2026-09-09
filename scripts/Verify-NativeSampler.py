"""Compare the native sampler against the offline decoder on preserved captures.

Reads only artifacts that already exist; captures stay out of Git, so this is run
by hand rather than by CTest. Every value the native code produces must match the
Python decoder that all published results were derived with.

  python scripts/Verify-NativeSampler.py [--exe PATH]
"""
import argparse
import importlib.util
import json
import math
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('rb', ROOT/'scripts'/'Decode-SpatialReadback.py')
rb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rb)
f32 = rb.model.f32

OFFSETS = ((0, 0, 0), (2, 0, 0), (-2, 0, 0), (0, 5, 0), (0, -5, 0), (0, 0, 3), (0, 0, -3))


def captures():
    base = ROOT/'artifacts'/'light-research'
    for derived in sorted(base.glob('**/derived.json')):
        raw_files = list(derived.parent.glob('spatial-binding-*.json'))
        if not raw_files:
            continue
        raw = json.loads(raw_files[0].read_text())
        d = json.loads(derived.read_text())
        if 'transactions' in d and isinstance(d.get('transactions'), list) and 'decoded' in d['transactions'][0]:
            entry, decoded = raw['transactions'][0], d['transactions'][0]['decoded']
        elif 'textureReadback' in raw and d.get('giWindowOffset') is not None:
            entry, decoded = raw['textureReadback'], d
        else:
            continue
        yield derived.parent.relative_to(base), entry, decoded


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', type=Path,
                    default=ROOT/'build'/'native-package'/'Release'/'CrimsonDesertTelemetrySpatialSampleTests.exe')
    args = ap.parse_args()
    if not args.exe.exists():
        sys.exit('build the native sampler test first: %s' % args.exe)

    total = mismatches = 0
    for name, entry, decoded in captures():
        constants = bytes.fromhex(entry['bufferPair']['giHex'])
        offset = decoded['giWindowOffset']
        constants = constants[offset:offset+768]
        volume = bytes.fromhex(entry['packedTextureHex'])
        reference = decoded['gpuReference']['referenceWorldCandidate']
        clipmap = decoded['gpuReference']['selectedClipmap']
        published = decoded['skyVisibilityCandidate']

        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp/'constants.bin').write_bytes(constants)
            (tmp/'volume.bin').write_bytes(volume)
            worlds = [[reference[j]+d[j] for j in range(3)] for d in OFFSETS]
            (tmp/'offsets.txt').write_text('\n'.join('%.17g %.17g %.17g' % tuple(w) for w in worlds))
            out = subprocess.run([str(args.exe), '--vector', str(tmp)], capture_output=True, text=True)
            if out.returncode != 0:
                sys.exit('native sampler failed on %s: %s' % (name, out.stderr.strip()))

        lines = out.stdout.strip().splitlines()
        head = lines[0].split()
        native_status, native_clip = int(head[1]), int(head[2])
        native_world = [float(v) for v in head[3:6]]
        native_reference = float(head[6])
        print('== %s' % name)
        print('   reference: native %.17g | published %.17g | clipmap %d/%d'
              % (native_reference, published, native_clip, clipmap))
        total += 1
        if native_status != 0 or native_clip != clipmap:
            print('   MISMATCH status/clipmap'); mismatches += 1
        if native_reference != published:
            print('   MISMATCH reference value'); mismatches += 1
        for j in range(3):
            if abs(native_world[j]-reference[j]) > 1e-9:
                print('   MISMATCH world axis %d' % j); mismatches += 1

        for line, world in zip(lines[1:], worlds):
            native = float(line.split()[-1])
            expected = rb.sample_world(volume, constants, clipmap, world)
            total += 1
            flag = '' if native == expected else '   <-- MISMATCH'
            if flag:
                mismatches += 1
            print('   offset %+.0f %+.0f %+.0f : native %.17g | python %.17g%s'
                  % (world[0]-reference[0], world[1]-reference[1], world[2]-reference[2],
                     native, expected, flag))
    print()
    print('%d comparisons, %d mismatches' % (total, mismatches))
    return 1 if mismatches else 0


if __name__ == '__main__':
    sys.exit(main())
