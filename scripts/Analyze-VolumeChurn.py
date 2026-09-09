"""What changed between transactions? Uses only preserved captures.

Tests three candidate causes of the drift seen at a fixed camera position:
  a) the clipmap re-centred            -> origin/relative constants move
  b) a global change (exposure/sky)     -> most voxels shift
  c) local occluders such as clouds     -> a minority of voxels change
"""
import importlib.util, json, math, struct
from pathlib import Path

root = Path('C:/DEV/CrimsonDesertTelemetry')
spec = importlib.util.spec_from_file_location('rb', root/'scripts'/'Decode-SpatialReadback.py')
rb = importlib.util.module_from_spec(spec); spec.loader.exec_module(rb)
DIMS = (64, 32, 264)

def series(rel):
    d = root/'artifacts'/'light-research'/rel
    raw = json.load(open(next(Path(d).glob('spatial-binding-*.json'))))
    der = json.load(open(Path(d)/'derived.json'))
    out = []
    for entry, dec in zip(raw['transactions'], der['transactions']):
        d0 = dec['decoded']
        gi = bytes.fromhex(entry['bufferPair']['giHex'])[d0['giWindowOffset']:][:768]
        out.append(dict(tex=bytes.fromhex(entry['packedTextureHex']), gi=gi,
                        value=d0['skyVisibilityCandidate'],
                        clip=d0['gpuReference']['selectedClipmap'],
                        coords=d0['gpuReference'].get('sampleCoordinatesBeforeSampler'),
                        neighbors=d0.get('neighbors', [])))
    return out

def vec(buf, off):
    return struct.unpack_from('<4f', buf, off)

for label, rel in (('TAG  (drift 0.087)', 'series-20260909/run3-abyss-DAY-pid33700'),
                   ('NACHT(drift 0.016)', 'series-20260909/run1-abyss-open-sky-pid9464')):
    S = series(rel)
    print('=== %s ===' % label)
    sel = S[0]['clip']
    print('  clipmap %d | origin/relative pro Transaktion:' % sel)
    for i, e in enumerate(S):
        o = vec(e['gi'], 0x140 + sel*16)[:4]
        r = vec(e['gi'], 0x240 + sel*16)[:3]
        inv = vec(e['gi'], 0x10)[:3]
        print('   %d value %.6f  origin %.3f %.3f %.3f scale %.1f  rel %.3f %.3f %.3f  inv %.6f'
              % (i, e['value'], o[0], o[1], o[2], o[3], r[0], r[1], r[2], inv[0]))
    print('  Texturvergleich gegen Transaktion 0:')
    base = S[0]['tex']
    total = len(base)
    for i, e in enumerate(S[1:], 1):
        t = e['tex']
        diff = sum(1 for a, b in zip(base, t) if a != b)
        mean = sum(abs(a-b) for a, b in zip(base, t))/total
        print('   %d: %7d von %d Bytes verschieden (%.2f%%), mittlere Abweichung %.3f'
              % (i, diff, total, 100*diff/total, mean))
    print('  Nachbarn des Messpunkts (Byte-Werte je Transaktion):')
    for i, e in enumerate(S):
        bs = [n['byte'] for n in e['neighbors']]
        print('   %d %s' % (i, bs))
    print()
