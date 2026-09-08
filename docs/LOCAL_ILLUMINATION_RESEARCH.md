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
Native producer and CPU camera comparison are now resolved below: this is a
view-context position matching the camera, NOT the player. Pairing the actual
bound GPU constants/texture with the inverse cache remains unverified.

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

## Native spatial provenance resolved at21:22 — 2026-09-08

Bounded inspection followed the existing exposure and filter owners. No broad
heap scan, additional camera/doorway experiment, hook or plugin replacement.
Exact EXE hash remains4D99C15C...; addresses below are build25116796 anchors,
NOT a new update-stable public discovery mechanism.

**Producer `0x143C533A0`:** RCX/filterOwner is saved in RBX, R9/view context in
RBP. The position is `[RBP+8F8/8FC/900]`. Stores at143C53A85/3A8D/3A95 write
`filterOwner+300/304/308 = viewPosition.xyz * [filterOwner+30/34/38]`.
The latter is `_invClipmapExtent`. Thus `_clipmapUVRelativeOffset / invExtent`
recovers the world-space view reference. Stores around143C538C6 also derive
`_wrappedViewPos` from that position. Do not confuse viewContext+8F8 with an
unrelated field at filterOwner+8F8.

The **whole768-byte CPU GI constant block is inline at filterOwner+20**.
Calls143C53B45/3B88 ->1432A3150 upload precisely that pointer/size to wrappers
at filterOwner+568/+560, selected by byte+705 (nonzero/zero). No heap search
or Map is needed to inspect this CPU upload source.

**Consumer `0x1435429F0` (exposure pass):** R13 is renderer+660/filterOwner.
143C-series producer above is NOT this consumer. At143544BFF the consumer
selects the SAME +560/+568 wrappers; wrapper+18 is bound as Voxel GI CB at
143544CD6. It takes the sky texture from filterOwner+4B8 at143544FD2, binds
it at143545087 and dispatches(2,1,1) at14354509E (return1435450A4).
This establishes a native source-to-binding chain, not a paired GPU capture.
Producer has five statically validated callers (143C5BC12,143C611D7,
143C612CA,143C61EB9,143C6E40C); possible auxiliary views mean caller selection
must still be checked if taking a future at-dispatch snapshot.

Resource chains, re-resolved from the existing Render bridge, never saved PID
addresses as restart anchors:

- Selected CB wrapper+18 -> outer+30 -> storage; storage+10 backlink to outer,
  +C0 stride768, +C4 count1, +168 resource pointer.
- Sky texture filterOwner+4B8 -> outer+30 -> storage; storage+10 backlink,
  +100 resource pointer, CPU descriptor at+D0/D4/D8 reports64x32x264.
- These are CPU wrapper observations: no in-process GetDesc, format query,
  resource-state confirmation, sampler query, Map or texture readback yet.
  The storage+68/+20 objects are metadata, NOT mapped CB/texture contents.

`Capture-ExposureContext.ps1 -IncludeSpatialContext` now records the two
matching bounded CPU copies, bank, validated links/shape and surrounding bridge
camera observations. It rejects changing data independently of the exposure
cache. Decoder follows AdaptExposure SSA199..284 (clipmaps1..7, lower-inclusive,
upper-exclusive bounds, fallback for none/>3) and returns unwrapped sample
coordinates, NOT a texture value. Sequential FP32 is diagnostic; fast-math near
cell boundaries and unverified sampler addressing/filtering remain caveats.

**Live access check21:22:01 CEST, same PID22128/sky.1:**50 attempts/4.923s,
50 progressing renderer frames.45 stable spatial contexts;2 changing-link and3
changing-copy/bank probes unavailable. All45 CPU shader-model evaluations select
clipmap1 (texture-sample branch), texture pointer0x139A2FF40. Inferred reference
vs bridge camera distance0.000319..0.000970gu (includes float JSON precision and
sequential timing), camera near(-10537.138,612.818,-4414.379). This supports camera
reference, not player-local irradiance or current-GPU-frame pairing. Player has
moved since the preceding outdoor recording; no new environment transition was
instructed or inferred.44 independent valid exposure inverses v.007111..016297;
6 unavailable. Do not attribute that change to an unreported roof transition.

Artifacts `local-illumination-spatial-context-20260908-pid22128-check1.json`
and `-derived.json` under artifacts/light-research. Raw SHA256:
`F34EB56EBC5CDCC58A58CF38DD7363B7A7A8270C0A5E4997539FE8481A6BF832`.
Native bytes/meta under artifacts/light-research/rawpages:
`local-sky-exposure-code-pid22128`, `local-sky-cb-producer-pid22128`
(producer SHA711BF82F04E1538036499D6137BE8FEEB866DC572458EAEE62D7334E5EBF29AA),
`local-sky-filter-resources-pid22128`, `local-sky-resource-storage-pid22128`.
The earlier `local-sky-resource-inners-pid22128` WITHOUT `-corrected` is an invalid
zero-byte failed-address dump (PowerShell numeric-string conversion), not absence
evidence. Corrected dump preserved separately; quote large hex addresses.

Tests:14/14 Python tests, including previous3004 independent FP32 inverse cases
and new synthetic spatial origin/negative coordinates/bounds/fallback/malformed
data/failure independence. PS7 parse clean, live progressing-control check above.

**One next step:** resolve this identified texture's actual format, sampler and
legal copy state at the existing exposure binding/dispatch boundary, then implement
a bounded paired direct readback with its768-byte CB. Do not guess a barrier or
assume R8 from the dimensions. Compare direct `saturate(1-sample.x)` to inverse
with measured cache age; only then assess its outdoor fluctuations. No more
generic walking tests yet. Raw/smoothed/sky APIs and ASI unchanged; source
occlusion remains independently required via the depth route below.

## Direct readback preflight and passive instrument — 2026-09-08,21:46

Native texture initialization is in143C4F170:143C4FEC9 selects filterOwner+4B8,
143C4FF1D constructs its descriptor (internal format0x0F),143C4FF7C calls texture
creation1437F0280,143C4FF94 stores the outer pointer. Internal format0x0F is NOT
itself a verified DXGI SRV format. Preserve this distinction.

Resolved the SAME live resource0x139A2FF40 in PID22128. Its vtable GetDesc
(slot10) points to D3D12Core.dll0x7FFE744800A0. Read-only inspection of that
implementation and its instance fields predicts the actual resource descriptor:
Texture3D,64x32x264,1 mip,DXGI_FORMAT_R8_TYPELESS(60),flags4/UAV,64KiB alignment.
No COM call was made in the game during this inspection. The new instrument
queries GetDesc in-process and checks it, rather than adopting undocumented
D3D12Core offsets as a product dependency. **Typed SRV format still needs check**;
do not silently decode typeless bytes as R8_UNORM yet.

Sampler name g_staticVoxelSampler is registered at14380988E/1438098A9 via
1437F15F0 for slot12; device vtable+178 ->143D0ECB0 builds52-byte static sampler
descriptors at device+970, count+978. Live slot12 (array0x5CB04175100) reports:
filter0x14=MIN_MAG_LINEAR_MIP_POINT, AddressU/V/W=1/WRAP, bias0, anisotropy1,
comparison8/ALWAYS, MinLOD0, MaxLOD FLT_MAX, register12, space4, visibilityALL.
That is linear interpolation within the 3D mip, point mip selection; our shader
uses LOD0. This resolves the stored sampler description, not a live root-signature
hash. The previous CPU decoder deliberately leaves X/Y unwrapped; it has not yet
sampled a texture. Keep raw coordinates for the eventual sampler comparison.

**Critical state finding:** device+43=1 in this process selects the enhanced
barrier branch in1437DD140. Engine command vtable145BD0BC8 has Dispatch+328=
1437B4360, SRV texture bind+450=1437DAD80, state request+588=1437DD140.
Dispatch applies queued bindings/barriers before native Dispatch. Static engine
state enums are NOT D3D12 enhanced-layout enums; do not substitute their numbers.
The original buffer-copy implementation's UAV legacy transition is not a safe
template for this shader-read texture. Microsoft documents the required copy
state in [CopyTextureRegion](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-copytextureregion)
and [resource barriers](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12).

Implemented private `spatial_probe.cpp/.h` + `spatial_thunk.asm`:

- Hooks normal Dispatch function ENTRY1437B4360 after exact-byte check and the
  existing exact-EXE gate. Only caller return1435450A4 with dimensions2,1,1 is
  observed. ABI thunk passes the caller's actual R13/filterOwner and return
  address, preserves normal ABI/stack and original RAX return.
- Explicit named event starts20 samples, at most2Hz,30s deadline. Default OFF;
  when enabled it starts IDLE. No startup/loading recording or movement request.
- Validates owner/renderer and texture wrapper backlinks,64x32x264 CPU shape,
  actual resource GetDesc and sampler register/space. Captures inline GI block
  before/after, selected-bank CB resource, scene bytes, raw exposure cache and
  raw SRV-view metadata. CPU copies/cache are NOT paired GPU reads.
- After discovering the actual list7 Barrier function, worker installs a passive
  detour (slot80 checked using SDK offsetof). Thread-local scope ONLY the actual
  exposure Dispatch, same native list7 and exact target resource. Captures up to8
  matching enhanced texture barriers, with actual Sync/Access/Layout/subresources;
  bounded group/entry processing, overflow explicit. Every engine call is forwarded
  unchanged. No GPU copy/barrier/dispatch is added by the observer.
- No observed barrier is UNKNOWN, not evidence of a read/copy state; transitions
  outside the invocation are not covered. This instrument intentionally does NOT
  arm a copy based on incomplete observations. ManyLights/sky code and APIs remain
  independent. It never claims source visibility or a calibrated sky percentage.
- MinHook patch operations run outside the capture lock; trampolines remain in
  the pinned module through shutdown. JSON writing is on worker, never render
  thread. Output uses CREATE_NEW; old evidence is untouched.

Host verification:21/21 native CTests; new observer32 controls use synthetic
wrappers plus real WARP resource/list7/MinHook Barrier interception. Separate ABI
fixture80000 parallel calls verifies argument/owner/caller/R13/stack extraction.
These tests do NOT demonstrate live producer frequency, complete barrier coverage
or actual GPU texels.14 existing Python tests continue passing.

Private package2.0.1-spatial-probe.1 validated and payload-matched; SHA256
F085947BAB4D1BDFDB14B6D7295DE8E3458BC5E02104B5C1D87EB63A2D141E33.
Install only after shutdown via DMM. Set Research/SpatialProbe=1, AmbientProbe=0;
leave existing Lights/ManyLights and Ambient enabled. After loading, agent calls
`scripts/Start-SpatialProbe.ps1 -ProcessId ACTUAL_PID`. Check new
`spatial-binding-PID-TICK-RUN.json` and progressing control. Not installed or
game-tested in this turn, no publish/push. The inherited ZIP README still
describes the public release; the private switch is explained here and in INI.

Evidence under artifacts/light-research/rawpages (binary+meta):
`local-sky-d3d12-resource-pid22128`, `local-sky-texture-create-pid22128`,
`local-sky-sampler-state-pid22128`, `local-sky-sampler-producer-pid22128`.
All fully read, matching live PID/executable/scene control. Keep alongside prior
exposure/native GI evidence. Next step is the passive game capture, then a
legal direct copy with paired GI constants; no new generic camera/doorway test.

## Previous control and separate source occlusion

### View-control recording completed at20:57 — 2026-09-08

User confirmed ready. One30s read-only capture, PID22128/sky.1 unchanged,
20:57:18.660..20:57:48.565 CEST, 300 attempts/29.957s.297 distinct render frames
56279..57769; one cache-chain identity.291 candidates,6 changed caches and3
changing bridges unavailable. Recorded camera pose; player was instructed to
remain fixed but its pose was NOT independently recorded in this script.
**User clarification after analysis:** this test was in the open, not near a
door. There was no reported indoor/outdoor transition. Keep it distinct from
the earlier PID4208 doorway recording. Its variation cannot be attributed to
a doorway crossing on this evidence. v~0.4 is not a calibrated "40% visible sky"
measurement; the remaining reference-position/texture interpretation matters
even outdoors. Nearby occluders or weather are not established explanations.

Camera direction rotated up to72.294 degrees from start; camera itself moved
up to7.0333 game units from its starting position. End direction differs by
13.4404 degrees from start, so this is NOT an exact camera-pose A-B-A return.
Histogram L2.92115..4.55835; exposure.0142187...0190443;
inferred v.355885...482627. Crucially v changes during stationary camera windows:

| Window | Camera evidence | Candidate v |
| --- | --- | --- |
| 0..5s | X/Z and direction constant; Y range.00137gu | .356251...424018 |
| 15..30s | X/Z and direction constant; Y range.0138gu | .369481...468300 |

Consequently a pure change-of-look-direction explanation is insufficient, and
this inverse is not established as a constant geometric indoor/outdoor scalar.
Dynamics alone do not prove decoder failure: voxel/temporal/environment state,
reference-origin interpretation and cache coherence/age remain unmeasured.
Do not discard the preceding doorway result or pretend this control resolves
the reference origin. No fixed-time/weather or direct-texture comparison here.

**Next bounded validation:** inspect the producer of the Voxel GI constants used
by AdaptExposureCS (`_wrappedViewPos` and `_clipmapUVRelativeOffset`) and compare
one direct sky-visibility texture sample with the inverse at the same reference
position. Reuse this concrete binding, not a broad GI search. No additional
generic camera/doorway run until this question is instrumented. Source occlusion
remains its separate required depth-resource route below.

Evidence artifacts/light-research/local-illumination-view-control-20260908-pid22128
`.json`, `-derived.json`, `-analysis.json`. Raw SHA256
`2A5F0782238850E54C5C5510DCEF60AFF2564EA6986AA91A1589E19316268A4B`.
No plugin/config/API edits, no install/publish/push; user released after recording.

### Separate depth-resource route

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
