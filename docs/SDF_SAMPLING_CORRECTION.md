# SDF normalized-sampling correction — 2026-09-12

The production C++ sampler and shared Python decoder omitted the half-texel
offset of normalized linear texture sampling. This correction is established
independently of the current lamp labels. It preserves the trace's level
selection, zero hit tolerance, 0.05 gu minimum step, 400 iterations, 0.6 gu start
offset and 1 gu end margin. **It does not resolve the reported camp occlusion
failure or establish production live acceptance.**

This supersedes the exact-sampling claim in the historical calibration narrative
in [SDF_VARIANT_A.md](SDF_VARIANT_A.md). Those original measurements and all
preserved research/capture files remain unchanged.

## Direct evidence and scope

The preserved archive listing
`artifacts/light-research/filtered-count-1322f152-20260906-2111-52c33a4a.ll`
declares `g_staticVoxelSampler` at s12/space4 (line 245), creates its handle at
line 304 and uses that handle for t233 `SampleLevel` with LOD 0 and zero integer
offsets (lines 1239–1251). Its input coordinates are
`(GI[1].xyz * relativePoint + GI[46].xyz) / 2^level` (lines 1111–1135).
Z is then `(130*level + 1 + 128*frac(z))/1040`. There is no compensating `+.5`
in that coordinate chain or in the reconstructed relative position.

The retained Serkis copy 55 and Warspike copy 12 metadata contain the same
`samplerHex` in their before/after contexts: filter `0x14`
(`MIN_MAG_LINEAR_MIP_POINT`), address U/V/W `1` (WRAP), register 12/space4,
MinLOD 0 and MaxLOD FLT_MAX. The preserved static-sampler resolution and full
52-byte descriptor match are documented in
[LOCAL_ILLUMINATION_RESEARCH.md](LOCAL_ILLUMINATION_RESEARCH.md), around lines
2490–2493 and 2573–2574. The replay independently finds
`GI[46].xyz == capturedCamera.xyz * GI[1].xyz` exactly in every tested context;
there is no hidden half-texel camera bias.

For normalized linear sampling, texel position is `u * dimension - .5`.
Consequently the corrected coordinates are:

| Axis | Continuous texel position | Neighbour addressing |
| --- | --- | --- |
| X | `128*frac(worldX*inverseX/2^L) - .5` | floor, wrap over 128 |
| Y | `64*frac(worldY*inverseY/2^L) - .5` | floor, wrap over 64 |
| Z | `130*L + .5 + 128*frac(worldZ*inverseZ/2^L)` | floor, actual stored slices of the 1040-deep texture |

Using floor matters at negative X/Y texel positions. At fraction Z=0, the
interpolation spans the low guard and first content slice. Near fraction Z=1,
it spans the last content and high guard. The old sampler wrapped only the
128 interior Z slices. All 160 guard/interior slice comparisons across the ten
retained ABA volumes were byte-identical, but the implementation now reads the
actual guards and its synthetic tests deliberately make them different.

This proves a mismatch with the preserved shader/sampler convention. It is not
a new proof of the current game's PSO or descriptor binding, and the CPU's
double-precision interpolation is not claimed bit-exact to GPU hardware.

## Verification

The OFF `CrimsonDesertTelemetrySdfVisibilityTests` target builds and passes.
Nine new analytical cases run through the public `Trace` path: an exactly
representable negative affine R16 field exposes the first sample directly.
They cover texel centres, all three interpolation axes, positive/negative wrap,
deliberately different guard slices and the final clipmap level. Existing
clear/blocked, camera-pairing, freshness and budget checks still pass.

`tests/Test-SignedDistanceSampler.py` separately passes ten stdlib tests for the
Python decoder, including every level, all eight interpolation weights,
negative coordinates and guard boundaries. No new GPU capture was used.

The reproducible offline comparison is saved under
`artifacts/light-research/source-visibility-regression-20260912/`:

- `replay-normalized-sampling.py` freezes the old sampler only for comparison,
  then runs the current shared decoder; it refuses to overwrite an output.
- `normalized-sampling-replay-01.json` contains input/metadata hashes, parameters,
  per-target results, guard comparisons and native executable verdict checks.
  SHA256: `9AA39367D8B1AD604BE8C36FF8AE6B47DB8147E42E691F47CC62BE65643352BB`.

Example, using the Python path documented in [TOOLING.md](TOOLING.md):

```powershell
python -B artifacts/light-research/source-visibility-regression-20260912/replay-normalized-sampling.py --out artifacts/light-research/source-visibility-regression-20260912/normalized-sampling-replay-NEW.json --native build/native-package-release/Release/CrimsonDesertTelemetrySdfVisibilityTests.exe
```

All **ten available retained** ABA payload/metadata pairs keep their individual
verdicts: Serkis 39/55/68 stays CLEAR/BLOCKED/CLEAR; Warspike 9–15 stays
CLEAR/CLEAR/BLOCKED/BLOCKED/BLOCKED/CLEAR/CLEAR. The historical documentation
describes a longer Warspike series; only these seven binaries are retained in
the inspected directory, so no larger replay count is claimed. All 22 native
verdicts match the corrected Python replay (10 ABA + 6 historical camp + 6
current saved CPU-volume targets), despite native float endpoint parsing.

## Remaining camp failure

The first calibration volume, PID33348 copy 1, covers the same camp. Using the
reported present camera/targets with that historical field reproduces the old
far pair CLEAR/CLEAR and near pair CLEAR/BLOCKED. The corrected sampler makes
all six tested historical camp targets CLEAR. That rules out declaring this
sampling correction a solution for the reported boxes; it does not prove that
old and current geometry are identical.

The independently saved current production CPU snapshot (volume 1853, context
53808; `current-sdf-01/sdf.bin`, SHA256
`FC41A9639EBE7C49AC8AD48C25EF390040C01E853099185999A5254EA0875019`) gives:

| Reported target | Old verdict / minimum | Corrected verdict / minimum |
| --- | --- | --- |
| far lower | CLEAR / .053559 | CLEAR / .038723 |
| far upper | CLEAR / .062468 | CLEAR / .038075 |
| near lower | CLEAR / .000330 | BLOCKED / -.003997 |
| near upper | BLOCKED / -.002813 | CLEAR / .001635 |
| left pair | CLEAR / .528655 | CLEAR / .378794 |
| left source | CLEAR / .006540 | CLEAR / .014961 |

This CPU snapshot was already stale when externally read, and its minimal
sceneHex is derived from its stored camera/frame. It is an offline same-scene
comparison, **not a fresh paired live test**. The separate acquisition-stall and
geometry-preservation questions remain open. The frequent `.5302734375` value
is the R16 level-0 positive saturation value, not an exact clearance measurement.
