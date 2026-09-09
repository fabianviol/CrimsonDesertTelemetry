"""Survey segment occlusion from the camera to every rendered light in every capture.

Reads only preserved artifacts. Uses the shared marcher in Decode-SpatialReadback,
so uncovered points, enclosure and low-run thickness are handled in one place
instead of being duplicated and diverging here.

There is no ground truth in these captures. This shows how the evidence is
distributed, not how often a verdict is right.
"""
import importlib.util
import json
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('rb', ROOT/'scripts'/'Decode-SpatialReadback.py')
rb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rb)


def captures():
    for derived in sorted((ROOT/'artifacts'/'light-research').glob('**/derived.json')):
        snapshot = derived.parent/'pre-capture-snapshot.json'
        raws = list(derived.parent.glob('spatial-binding-*.json'))
        if not snapshot.exists() or not raws:
            continue
        raw, d = json.loads(raws[0].read_text()), json.loads(derived.read_text())
        if 'transactions' in d and d['transactions'] and 'decoded' in d['transactions'][0]:
            entry, dec = raw['transactions'][0], d['transactions'][0]['decoded']
        elif 'textureReadback' in raw and d.get('giWindowOffset') is not None:
            entry, dec = raw['textureReadback'], d
        else:
            continue
        constants = bytes.fromhex(entry['bufferPair']['giHex'])
        constants = constants[dec['giWindowOffset']:][:768]
        # The rendered feed is what a consumer sees. An earlier version of this
        # survey marched to lights.sources, the much shorter authored array.
        sources = json.loads(snapshot.read_text()).get(
            'lights', {}).get('rendered', {}).get('sources') or []
        yield (derived.parent.name[:32], bytes.fromhex(entry['packedTextureHex']), constants,
               dec['gpuReference']['referenceWorldCandidate'], sources)


def main():
    verdicts = Counter()
    print('%-32s %3s %7s %10s %7s %8s  %s' %
          ('capture', '#', 'gu', 'min', 'lowRun', 'endMax', 'verdict'))
    for name, volume, constants, camera, sources in captures():
        for index, source in enumerate(sources):
            p = source['position']
            r = rb.march_segment(volume, constants, camera, [p['x'], p['y'], p['z']])
            verdicts[r['verdict']] += 1
            print('%-32s %3d %7.1f %10s %7.1f %8s  %s' %
                  (name, index, r.get('length', 0),
                   ('%.6f' % r['minimum']) if r.get('minimum') is not None else 'n/a',
                   r.get('lowRun', 0),
                   ('%.4f' % r['endpointMax']) if r.get('endpointMax') is not None else 'n/a',
                   r['verdict']))
    print()
    for verdict, count in verdicts.most_common():
        print('%-22s %d' % (verdict, count))
    print('total %d. No ground truth here: distribution only.' % sum(verdicts.values()))


if __name__ == '__main__':
    main()
