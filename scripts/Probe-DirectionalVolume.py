"""Offline directional probe of the ALREADY captured volumes.

Reuses the shipped decoder's world->texture mapping and its linear-WRAP sampler.
No new game capture. Assumption stated explicitly: the clipmap selected for the
camera position is held fixed for nearby offsets; that is checked by reporting
gu-per-texel so the offsets stay small against the clipmap cell size.
"""
import importlib.util, json, math, struct
from pathlib import Path

root = Path('C:/DEV/CrimsonDesertTelemetry')
spec = importlib.util.spec_from_file_location('rb', root/'scripts'/'Decode-SpatialReadback.py')
rb = importlib.util.module_from_spec(spec); spec.loader.exec_module(rb)
f32 = rb.model.f32

RUNS = [
    ('run1 offen  ', 'roof-control-20260909/run1-outside-pid32544'),
    ('run2 Wand   ', 'roof-control-20260909/run2-camera-turned-pid9488'),
    ('run3 Dach   ', 'roof-control-20260909/run3-under-roof-pid14576'),
]

def constants(raw):
    pair = raw['textureReadback']['bufferPair']
    gi = bytes.fromhex(pair['giHex'])
    der = json.load(open(Path(raw['__dir__'])/'derived.json'))
    off = der['giWindowOffset']
    return gi[off:off+768], der

def vec(buf, offset):
    return struct.unpack_from('<4f', buf, offset)

def sample_at(texture, gi, world, selected):
    inv = vec(gi, 0x10)[:3]
    uv = [world[j]*inv[j] for j in range(3)]
    scale = 1.0/(1 << selected)
    z = f32(uv[2]*scale)
    fraction = f32(z-math.floor(z))
    if fraction < 0:
        fraction = f32(1+fraction)
    tex_z = f32(f32(float(selected*66+1)+f32(fraction*64)) * rb.model.ir_float('3F6F07C200000000'))
    coords = [f32(uv[0]*scale), f32(uv[1]*scale), tex_z]
    value, _ = rb.linear_wrap(texture, coords)
    return max(0.0, min(1.0, 1-value))

for label, rel in RUNS:
    d = root/'artifacts'/'light-research'/rel
    raw = json.load(open(next(d.glob('spatial-binding-*.json'))))
    raw['__dir__'] = str(d)
    gi, der = constants(raw)
    texture = bytes.fromhex(raw['textureReadback']['packedTextureHex'])
    world = der['gpuReference']['referenceWorldCandidate']
    selected = der['gpuReference']['selectedClipmap']
    inv = vec(gi, 0x10)[:3]
    scale = 1.0/(1 << selected)
    # world units per texel along each axis, at the selected clipmap
    per_texel = [1.0/(inv[j]*scale*n) for j, n in zip(range(3), (64, 32, 264))]
    base = sample_at(texture, gi, world, selected)
    print('=== %s  clipmap %d ===' % (label, selected))
    print('   Kamera %.1f / %.1f / %.1f   Basiswert %.6f (Decoder: %.6f)'
          % (world[0], world[1], world[2], base, der['skyVisibilityCandidate']))
    print('   gu pro Texel: X %.2f  Y %.2f  Z %.2f' % tuple(per_texel))
    for dist in (2.0, 5.0, 10.0):
        row = []
        for name, axis, sign in (('+X', 0, 1), ('-X', 0, -1), ('+Y', 1, 1), ('-Y', 1, -1), ('+Z', 2, 1), ('-Z', 2, -1)):
            w = list(world); w[axis] += sign*dist
            row.append('%s %.6f' % (name, sample_at(texture, gi, w, selected)))
        print('   %5.1f gu:  %s' % (dist, '  '.join(row)))
    print()
