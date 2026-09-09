"""A-B-A analysis: march camera to lantern for every transaction, place it on the walk."""
import importlib.util, json, math, struct
from pathlib import Path

root = Path('C:/DEV/CrimsonDesertTelemetry')
spec = importlib.util.spec_from_file_location('rb', root/'scripts'/'Decode-SpatialReadback.py')
rb = importlib.util.module_from_spec(spec); spec.loader.exec_module(rb)
f32 = rb.model.f32
ZS = rb.model.ir_float('3F6F07C200000000')
D = root/'artifacts'/'light-research'/'occlusion-aba-20260909'/'pid29632-lantern-playerhome'


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


def march(volume, a, b, step=0.25):
    steps = max(8, int(math.dist(a, b)/step))
    values, uncovered = [], 0
    for i in range(steps+1):
        f = i/steps
        value, _ = volume.sample([a[j]+(b[j]-a[j])*f for j in range(3)])
        if value is None:
            uncovered += 1
        else:
            values.append(value)
    inner = values[1:-1] if len(values) > 2 else values
    return (min(inner) if inner else None), uncovered, len(values)


raw = json.loads(next(D.glob('spatial-binding-*.json')).read_text())
track = [json.loads(l) for l in (D/'track.jsonl').read_text().splitlines() if l.strip()]
track = [t for t in track if 'camera' in t]
start = track[0]['monotonic']

print('%3s %28s %9s %8s %7s %8s  %s' %
      ('#', 'Kamera', 'Strecke', 'MIN', 'unbek.', 't[s]', 'Spieler x/z'))
rows = []
for index, entry in enumerate(raw['transactions']):
    gi_all = bytes.fromhex(entry['bufferPair']['giHex'])
    tex = bytes.fromhex(entry['packedTextureHex'])
    cpu = bytes.fromhex(entry['context']['giBeforeHex'])
    at = -1
    for off in range(0, len(gi_all)-768+1, 256):
        if gi_all[off:off+768] == cpu:
            at = off
            break
    volume = Volume(gi_all[at:at+768], tex)
    reference = volume.vec(0x2E0)[:3]
    camera = [reference[j]/volume.inv[j] for j in range(3)]
    # the lantern as the track saw it, nearest in time to this camera position
    best = min(track, key=lambda t: math.dist(camera, [t['camera']['x'], t['camera']['y'], t['camera']['z']]))
    lantern = best['target']['position'] if best.get('target') else None
    if lantern is None:
        print('%3d  Laterne im Feed nicht sichtbar' % index)
        continue
    target = [lantern['x'], lantern['y'], lantern['z']]
    low, uncovered, counted = march(volume, camera, target)
    rows.append((index, low))
    print('%3d %9.2f %8.2f %8.2f %9.1f %8s %6d %7.1f  %8.2f %9.2f' %
          (index, camera[0], camera[1], camera[2], math.dist(camera, target),
           ('%.6f' % low) if low is not None else 'n/a', uncovered,
           best['monotonic']-start, best['player']['x'], best['player']['z']))
print()
values = [r[1] for r in rows if r[1] is not None]
if values:
    print('Spanne der Minima: %.6f .. %.6f' % (min(values), max(values)))
