"""Survey segment minima from the camera to every light in every preserved capture.

Uses per-point clipmap selection so distant sources are reachable. Reads only
artifacts that already exist. This looks for whether the criterion separates cases
at all, not for a threshold.
"""
import importlib.util, json, math, struct
from pathlib import Path

root = Path('C:/DEV/CrimsonDesertTelemetry')
spec = importlib.util.spec_from_file_location('rb', root/'scripts'/'Decode-SpatialReadback.py')
rb = importlib.util.module_from_spec(spec); spec.loader.exec_module(rb)
f32 = rb.model.f32
ZS = rb.model.ir_float('3F6F07C200000000')


class Volume:
    def __init__(self, gi, tex):
        self.gi, self.tex = gi, tex
        self.inv = self.vec(0x10)[:3]
        o1, r1 = self.vec(0x140+16), self.vec(0x240+16)
        self.anchor = [r1[j]/o1[3] for j in range(3)]

    def vec(self, off):
        return struct.unpack_from('<4f', self.gi, off)

    def level(self, world):
        w = [world[j]-self.anchor[j] for j in range(3)]
        for lvl in range(1, 8):
            o, r = self.vec(0x140+lvl*16), self.vec(0x240+lvl*16)
            if not (0 < o[3] <= 1e4):
                return None
            if all(int(f32(o[j]-rad)) <= math.floor(f32(f32(w[j]*o[3])+r[j])) < int(f32(o[j]+rad))
                   for j, rad in enumerate((63, 31, 63))):
                return lvl
        return None

    def sample(self, world):
        lvl = self.level(world)
        if lvl is None or lvl > 3:
            return None, lvl
        scale = 1.0/(1 << lvl)
        uv = [world[j]*self.inv[j] for j in range(3)]
        z = f32(uv[2]*scale)
        fr = f32(z-math.floor(z))
        if fr < 0:
            fr = f32(1+fr)
        tz = f32(f32(float(lvl*66+1)+f32(fr*64))*ZS)
        v, _ = rb.linear_wrap(self.tex, [f32(uv[0]*scale), f32(uv[1]*scale), tz])
        return max(0.0, min(1.0, 1-v)), lvl


def march(volume, a, b, step=0.5):
    length = math.dist(a, b)
    steps = max(4, int(length/step))
    values, levels = [], set()
    for i in range(steps+1):
        f = i/steps
        value, lvl = volume.sample([a[j]+(b[j]-a[j])*f for j in range(3)])
        levels.add(lvl)
        if value is not None:
            values.append(value)
    inner = values[1:-1] if len(values) > 2 else values
    return (min(inner) if inner else None), length, sorted(x for x in levels if x)


rows = []
for derived in sorted((root/'artifacts'/'light-research').glob('**/derived.json')):
    snapshot = derived.parent/'pre-capture-snapshot.json'
    raws = list(derived.parent.glob('spatial-binding-*.json'))
    if not snapshot.exists() or not raws:
        continue
    raw, d = json.loads(raws[0].read_text()), json.loads(derived.read_text())
    if 'transactions' in d and d['transactions'] and 'decoded' in d['transactions'][0]:
        entry, dec = raw['transactions'][0], d['transactions'][0]['decoded']
    else:
        entry, dec = raw['textureReadback'], d
    gi = bytes.fromhex(entry['bufferPair']['giHex'])
    gi = gi[dec['giWindowOffset']:][:768]
    volume = Volume(gi, bytes.fromhex(entry['packedTextureHex']))
    camera = dec['gpuReference']['referenceWorldCandidate']
    lights = json.loads(snapshot.read_text()).get('lights', {}).get('sources', [])
    name = derived.parent.name[:34]
    for index, light in enumerate(lights):
        p = light['position']
        target = [p['x'], p['y'], p['z']]
        low, length, levels = march(volume, camera, target)
        rows.append((name, index, light.get('kind'), length, low, levels))

print('%-34s %3s %-6s %7s %10s  %s' % ('Aufnahme', '#', 'kind', 'gu', 'min', 'Ebenen'))
for name, index, kind, length, low, levels in rows:
    print('%-34s %3d %-6s %7.1f %10s  %s'
          % (name, index, kind, length, ('%.6f' % low) if low is not None else 'n/a', levels))
values = [r[4] for r in rows if r[4] is not None]
if values:
    print()
    print('%d Strecken. Unter 0.01: %d   ueber 0.05: %d   dazwischen: %d'
          % (len(values), sum(v < 0.01 for v in values), sum(v > 0.05 for v in values),
             sum(0.01 <= v <= 0.05 for v in values)))
