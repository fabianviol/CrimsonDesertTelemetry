"""Occlusion analysis for one capture: march from given viewpoints to a target.

Uses the shared marcher in Decode-SpatialReadback rather than its own copy. Marching
from SEVERAL viewpoints through EVERY volume of a series separates a geometric
difference from a temporal one, because the volume refreshes between transactions.

  python scripts/Analyze-SegmentOcclusion.py CAPTURE_DIR TX TY TZ  CX CY CZ [CX CY CZ ...]
"""
import importlib.util
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('rb', ROOT/'scripts'/'Decode-SpatialReadback.py')
rb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rb)


def volumes(directory):
    raw = json.loads(next(Path(directory).glob('spatial-binding-*.json')).read_text())
    entries = raw.get('transactions') or [raw['textureReadback']]
    for entry in entries:
        constants = bytes.fromhex(entry['bufferPair']['giHex'])
        cpu = bytes.fromhex(entry['context']['giBeforeHex'])
        at = next((o for o in range(0, len(constants)-767, 256)
                   if constants[o:o+768] == cpu), None)
        if at is None:
            continue
        yield bytes.fromhex(entry['packedTextureHex']), constants[at:at+768]


def main():
    if len(sys.argv) < 8 or (len(sys.argv)-5) % 3:
        sys.exit(__doc__)
    directory = sys.argv[1]
    target = [float(v) for v in sys.argv[2:5]]
    viewpoints = [[float(v) for v in sys.argv[i:i+3]] for i in range(5, len(sys.argv), 3)]
    print('target %.3f %.3f %.3f' % tuple(target))
    for index, (volume, constants) in enumerate(volumes(directory)):
        for number, camera in enumerate(viewpoints):
            r = rb.march_segment(volume, constants, camera, target)
            print('  vol %d  view %d  %6.1f gu  min %-10s lowRun %4.1f  endMax %-8s %s'
                  % (index, number, math.dist(camera, target),
                     ('%.6f' % r['minimum']) if r.get('minimum') is not None else 'n/a',
                     r.get('lowRun', 0),
                     ('%.4f' % r['endpointMax']) if r.get('endpointMax') is not None else 'n/a',
                     r['verdict']))


if __name__ == '__main__':
    main()
