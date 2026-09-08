# Local illumination and source visibility — 2026-09-08, Codex/Astra

Required additions to the neutral telemetry product: local environmental lighting
under roofs/in caves, and a separate camera-source visibility stream. Preserve
existing raw/smoothed sources without a new visibility filter. These are different
quantities; neither global sky nor exposure nor ManyLights inclusion proves them.
This checkpoint implements **private diagnostics**, not a new public API or ASI.

## New result: an existing spatial sky-visibility sample is recoverable

Reused the archive index and CrimsonForge readers, not a new heap/GI search.
`shader/postprocessexposure.hlsl` is source hash `fece8b82`. The four compute
entry representatives are `LumaExtractCS` (`0a4a3a56`, empty in that representative),
`ClearHistogramCS` (`24275abd`), `GenerateHistogramCS` (`2900b273`), and
`AdaptExposureCS` (`ac80bf15`). Empty LumaExtract is not evidence that all variants
are empty; it is not the producer selected for the diagnostic.

GenerateHistogramCS actually samples `g_sceneColor` (t12,space36), computes
Rec.709-weighted luminance and bins log2 luminance into 256 bins. Separate RGB
histograms can use `g_sceneColorLightingOnlyForAwb` (t41,space36) where its alpha
is positive. This is **view-dependent scene color**, not incident ambient RGB.

AdaptExposureCS consumes those histograms AND samples
`g_skyVisibilityVoxelsTexturesLikeUav` (t162,space36), with the Voxel GI constants
(b1,space35) and Scene constants (b16,space35). It tests clipmap containment at
the clipmap reference-origin context (no player pointer), samples using `_clipmapUVRelativeOffset`,
and computes `v = saturate(1 - textureSample.x)`. If the selected clipmap is above
3 or none matches, it substitutes **v=1**. Thus v=1 cannot prove texture coverage
or an open sky. This is not visibility along camera-to-lamp rays.
The exact world origin behind these GI constants still needs a native/control
check; do not silently equate it to the player's position or camera position.

The existing large GI consumer independently samples the same-named texture at
t232,space36, lines 5243..5280 of `gi-evaluate-largest.ll`: its `saturate(1-sample)`
multiplies an environment-cube contribution. That supports the spatial-sky
interpretation; it does NOT establish that this scalar alone is complete ambient
illumination, separated sun/moon shadowing, or the player's local irradiance.

### Recovering v without another GPU hook

The known 64-byte engine readback cache is copied from the exposure output.
In ALL THREE indexed AdaptExposure variants, the function body is identical.
Its u8,space39 stores include:

| Byte offset | Verified shader role |
| --- | --- |
| +0 | Final adapted/overridden exposure, NOT local light level |
| +20 | EV intermediate before temporal exposure adaptation (`%348`) |
| +32, +60 | Same clamped histogram luminance L (`%198`) |
| +52 | Literal float 1 (layout control, NOT visibility) |
| +56 | Histogram-derived dispersion term (`%197`) |

L is a percentile-weighted histogram result AFTER configured low/high clamps
and a float32 1e-6 floor. It is not the pre-clamp measurement or lux. In the
doorway control the inside L is pinned to that floor; do not claim actual indoor
brightness from it. L's separate downstream calculation does not depend on v.

For the precise float32 constants printed in the shader, set:

```
x = clamp(L, 0.0001, 7)
u = saturate((x - 0.01) * 0.14306151866912842)
w = saturate((x - 0.0001) * 101.01010131835938)
a = 2*w - 3.5 + (3 - 2*w)*u
b = (3*w - 3)*(1-u)
EV = log2(8*L) - b - (a-b)*fourthRoot(v)
v = ((log2(8*L) - b - EV)/(a-b))^4
```

The script uses exact IR constants rather than the rounded decimals above.
DXIL op23 is log2 and op21 is exp2, per
[Microsoft's DXIL specification](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst).
Out-of-domain/inconsistent data becomes unavailable, not clamped into plausible
visibility. A small numerical tolerance handles FP32 error only; it is NOT a
confidence interval or calibrated physical error. Packed +24 can be NaN as float.

### Evidence and validation

Replayed the already completed no-menu doorway control, PID4208, 120 stable
cache appendices. No new movement experiment was needed to obtain this result:

| Window (s from first sample) | n | Mean inferred v | Mean clamped L |
| --- | --- | --- | --- |
| Outside 0..24 | 48 | 0.402995 | 0.000135708 |
| Inside 30..40 | 20 | 0.0000122074 | float32 1e-6 (floor) |
| Return 52..60 | 16 | 0.418049 | 0.0000776898 |

This is retrospective evidence for a reversible spatial sky-visibility response,
not independent direct-texture validation. GPU cache age is unknown, and camera
view/origin changed during walking. Remaining control: fixed-player, changed-view,
record camera position too because orbiting changes its world position.

New read-only recorder self-locates from Render mapping header owner+104:
filterOwner+10 -> Renderer (+660 backlink), +668 -> Sky (+10 backlink),
+690 -> ExposureOwner (+10 backlink), +C0 -> outer+30 -> inner+168 -> resource;
owner+D0 readback helper; bytes ONLY owner+D8..117. Every link is reread; two
cache reads and identities must match. This detects some races, not ABA/atomicity.
No Map, game function, GPU barrier, memory write, restart or config change.

First live smoke test 20:48:48 CEST, PID22128/sky.1: 20 attempts/1.948s,
19 distinct progressing renderer frames. 18 valid candidates, one changed cache
and one changing bridge safely unavailable. Valid v=0.430869..0.431113,
mean0.431018; camera near(-10500.688,613.6284,-4379.459). This validates access
in the new PID, not a camera-direction or roof experiment. Existing streams kept
running. Failed/missing tests never become v=0. Source GPU age remains unknown.

Final recorder check after adding complete header/process guards,20:52:54:
30 attempts/2.931s,30 progressing frames,29 candidates and1 changing cache;
v0.406743891..0.406744957. No controlled movement instruction, so do not interpret
the difference from the earlier smoke as a motion test. Existing-output refusal
preserved the prior file hash; wrong-process refusal produced no capture.

`tests/scripts/test_exposure_context.py`: independent sequential FP32 forward
calculation vs inverse, 3004 synthetic cases; packed NaN, invalid flags/length/
layout/store controls, wrong-model values, absent layout assumption, missing
data and missing progressing live control. Synthetic tests != game validation.

## Source occlusion: concrete next resource, NOT implemented yet

Existing `filtered-count-exact-20260906-2113-29588660.ll` is ProcessManyLightsCS.
It actually samples **g_hiZMap t15,space36** five times (lines1020..1032), at a
chosen mip, combines depths and uses the result in a light-selection branch.
The tested region has an extent and mip selection: do NOT label an included
ManyLights source as a directly visible source center. The cull has other paths
and thresholds; no pixel visibility fraction was decoded here.

For a separate visibility stream, the next native instrument should resolve this
already-known depth resource at its binding/consumer boundary and verify resource
identity, format, mip meaning, reversed-Z/projection, current/previous frame and
queue/fence provenance. Capture small depth evidence alongside the SAME camera
and light sample. First wall/clear-view A-B-A control before promoting a test.
Do not capture an arbitrary backbuffer or guess a wall factor from global sky.

Initially a depth comparison would establish only **source-center screen-depth
visibility**, not emitter geometry, transparency, volume coverage, light reaching
the player, or the visible illuminated patch. Offscreen and missing depth are
separate from occluded. Avoid near-plane/jitter false results; grouping comes
after per-contribution tests, never mixing differently visible members silently.
Raw feeds stay unchanged. Public stream/API design waits for a measured method.

## Reproduce / continue without rediscovery

Scripts are under product `scripts/`; use PS7 and the configured Python runtime.
Here PATH's python/py are Store aliases; the actual executable is
`C:\Users\fabia\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe`.
No replacement ZIP required for these diagnostics:

```powershell
.\scripts\Capture-ExposureContext.ps1 -ProcessId ACTUAL_PID -Seconds 10 -RateHz 10 -OutFile C:\DEV\CrimsonDesertTelemetry\artifacts\light-research\FRESH.json
python scripts/Decode-ExposureContext.py --input artifacts/light-research/FRESH.json --out artifacts/light-research/FRESH-derived.json --assume-adapt-exposure-layout
python -m unittest discover -s tests/scripts -p test_exposure_context.py
```

Archive index `artifacts/light-research/crimsonforge-shader-index-20260905.json`;
reader/inspector in research/light-source-tests, third-party code in external/.
Exact AdaptExposure paths:
`shadercache__/0d0eaf0a_fece8b82_5_ac80bf15_3_deba1dcd_{33e3354e,8aa92e9a,deba1dcd}.padxil`.
Artifacts prefix `local-illumination-exposure-20260908-` contains 6 inspected
entries (4 entry representatives + 2 additional Adapt variants), PASC/DXBC/IR
and per-artifact hashes. Common LF function-body SHA256:
`D820FE2A890F12569DBB54CD8462715485D4ACDF98D071A73C9934E89FF89269`.
Reference DXBC SHA256:
`F5880944E0DD3E567B96F9C7CABD8179300D8E7C488A35B39512DAEB55295847`.
Not a directly captured live PSO hash; shader-only update risk remains.
Original EXE SHA4D99C15C..., Steam25116796, unchanged from existing evidence.

Derived doorway: `artifacts/light-research/local-illumination-doorway-control-20260908-derived.json`;
source `ambient-live-20260908-pid4208-doorway-control/ambient-readback.json`
and its immutable v2 binary SHA B2FB28F90308807B96443BF0C39B9063640C05A1E7EAE75620ED573576F2B315.
Live raw/derived: `local-illumination-live-20260908-pid22128-check*.json`.
Final smoke: `local-illumination-live-20260908-pid22128-final-check*.json`.
