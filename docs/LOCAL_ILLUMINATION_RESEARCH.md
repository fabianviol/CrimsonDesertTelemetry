# Local illumination and source visibility — 2026-09-08, Codex/Astra

Required additions to the neutral telemetry product: local environmental lighting
under roofs/in caves, and a separate camera-source visibility stream. Preserve
existing raw/smoothed sources without a new visibility filter. These are different
quantities; neither global sky nor exposure nor ManyLights inclusion proves them.
This work implements **private diagnostics**, including an opt-in diagnostic ASI,
not a new public local-illumination or source-visibility API. Read "Why the CPU
exposure shortcut fails" first if you are tempted by the cheap route. The latest
LIVE result is "Occlusion series" after it: the sample responds to local
enclosure across about four orders of magnitude, and its reference is proven to be
the camera position exactly. "First paired GI and texture capture" after it records
the first working transaction, and "Same-submission pairing" explains the design
and its deliberately limited evidence standard.
Older sections preserve prior evidence, not current instructions.

## The engine's ambient light is 1024 bytes of spherical harmonics — 2026-09-09

Read from `GenerateAmbientFromEnvironmentAtmosphericScatteringCS`
(`artifacts/light-research/ambient-check-20260907-sky-ca7a87f5.ll`), a shader that
had been sitting in the artifacts since 2026-09-07 without being opened. It answers
the ambient half of this project without a volume, a clipmap or an interpolation.

**Shape of the shader.** 16x16 threads, one dispatch. It reads
`g_texSkyInscatter` (t38, 2D), `g_texNetDensity` (t37, 2D) and
`g_texCloudVolumeShadow` (t76, 3D) with six `SampleLevel` calls, plus
`SceneConstantBuffer` (b3, space35), `AtmosphereConstantBuffer` (b15, space35,
240 bytes) and `GlobalPushConstants` (b0, space1: `_renderFlags`, `_skyColor`,
`_volumeSize`).

**It integrates the sky into spherical harmonics.** The groupshared variable is
named `g_sharedEnvironmentSH` and typed `SHColor2`. DXC split it into per-channel
arrays whose sizes settle the order: three channels, each with two `[1024 x float]`
vector members and one `[256 x float]` scalar member. 1024/256 = 4, so per element
per channel that is 4 + 4 + 1 = **nine coefficients — L2 spherical harmonics, RGB**,
one per thread over 256 threads, reduced across five `Barrier` calls.

**Output.** `RWStructuredBuffer<float4>` at u2, space39. Seven `float4` at fixed
indices 0..6 — 28 floats, the standard packing of 27 SH coefficients with one spare
lane — then an eighth `float4` at index 7 computed separately, and a second block of
seven at `(_renderFlags.x) * 8 + 8..14`. Stride eight `float4` per set.

**The same bytes are read back as a constant buffer.** Other shaders declare
`PrecomputedAmbientConstantBuffer` as `{ 8 x float4, [56 x float4] }` — 64 float4,
and DXC's own annotation on the handle says `ResourceProperties { 13, 1024 }`.
**1024 bytes, eight sets of eight float4.** A CBV, in space35, the same space as the
768-byte `VoxelGlobalIlluminationConstantBuffer` we already snapshot. Nothing about
reaching it is new work.

**Who consumes it,** across all 79 local listings:

| shader | slots read |
|---|---|
| `csRenderAtmosphericScattering` | 7, 56 |
| `CSRenderAtmosphericScatteringOffscreenSky` | 7, 56 |
| `GenerateAtmosphericScatteringDispatchIndirectArgumentsCS` | 7 |
| `RenderDiffuseCS` | 7 |
| `RenderDiffuseTiledCS` | 7 |
| `EvaluateDiffuseRadianceCS` (two variants) | 7 |

The register number differs per shader (cb16 in the sky path, cb32 in the diffuse
path); the 1024-byte layout does not. In `EvaluateDiffuseRadianceCS` slot 7 is used
two ways: `.y` taken directly on one branch, and on the other
`(0.5 - 0.5 * dot3(dir, v)) * .w` — a hemispherical weight. Its writer built `.w`
from an `exp` extinction dotted with the Rec.601 luminance weights, so slot 7 is a
scalar-plus-colour summary, not part of the SH.

**Why this is the right quantity for the ambient feed.** It is what the engine
itself calls ambient, in the engine's own units, already directional. Evaluating L2
SH against an up vector is a few multiplies. There is no clipmap origin to resolve,
no trilinear sampling, no amortised refresh to wait out, and no 1465-byte payload —
1024 bytes covers the whole sky.

**And it composes cleanly with the refuted occlusion work.** This is the ambient
from the SKY, unoccluded: it says how bright and what colour the environment is,
and nothing at all about standing in a cave. Sky visibility is the term that knows
about the cave. The two multiply. That is a better division of labour than trying
to make one scalar field answer both questions, which is exactly what failed.

**Not established here.** The coefficient order inside the seven float4 is inferred
from the shape, not verified lane by lane. What `_renderFlags.x` selects, and
therefore what the other seven sets are for, is unknown. Whether the values are pre-
or post-exposure is unknown and matters, because `ExposureConstantBuffer` (b*,
space35, 5 float4 = 80 bytes) is read by nearly every lighting shader here including
`ProcessManyLightsCS`, the one behind our own light readback. No shader in the local
sample reads slots 0..6; 79 listings are not the engine.

**Next step:** locate the live 1024-byte CBV, capture it with the machinery that
already captures the 768-byte one, and check a night value against a day value
before trusting any of it.

## Directional profile measured offline — 2026-09-09

No new game capture. The three preserved volumes were re-sampled at offsets around
their recorded camera positions, reusing the shipped decoder's world-to-texture
mapping and its linear-WRAP sampler. Sampling at the camera position itself
reproduced the published value exactly in all three runs (0.386534, 0.117666,
0.000031), which validates the re-sampling before any offset is trusted.

**Geometry.** At the selected clipmap1 the inverse extents are 0.03125/0.0625/0.03125
with scale 1/2, giving **1.00 game unit per voxel on all three axes** and a clipmap
covering 64x32x64 gu. Offsets of 2..10 gu are therefore 2..10 voxels and stay well
inside the clipmap.

**Physical sanity check.** Sampling downward (-Y) at 5 and 10 gu returns exactly
0.000000 in all three runs. Below the camera is ground, and the field says so.

**run3, under the roof, one instant, base 0.000031:**

| offset | +X | -X | +Y | -Y | +Z | -Z |
|---|---|---|---|---|---|---|
| 2 gu | 0.000323 | 0.004531 | **0.000029** | 0.044253 | 0.035764 | 0.098670 |
| 5 gu | 0.311679 | 0.080739 | **0.432942** | 0.000000 | 0.021155 | 0.285750 |
| 10 gu | 0.375227 | 0.260822 | 0.489194 | 0.000000 | 0.205883 | 0.311480 |

The +Y column is the result. Two units straight up is still 0.000029 — the roof is
directly overhead. Five units up is 0.432942, above the roof in open sky. Sideways,
+X at five units reaches 0.311679, out from under the structure. **A factor of more
than a thousand across a few metres, at a single instant, from one stored volume.**

run1 in the open shows the same field behaving sensibly in the other direction:
upward increases (0.399939 at 5 gu against a 0.386534 base) while downward goes to
zero. run2 at the wall shows the horizontal gradient: -Z two units is 0.047271
toward the structure against +Z 0.196598 toward the opening.

**This demonstrates the cave-mouth effect is expressible.** Lamps around the player
can be driven from offsets in their own directions: the side under cover reads near
zero while the side toward the opening reads a few tenths, from the same capture.

**Limits, all real.** This approximates direction by sampling a scalar field at
neighbouring points; it is not sky visibility as a directional function at one
point. The clipmap selection was held fixed for the offsets rather than recomputed
per sample, which is reasonable at 2..10 gu inside a 64 gu clipmap but is an
assumption, not a check. The sampling distance becomes a design parameter: too
small gives no contrast, too large samples through walls into unrelated space.
Horizontal readings are not yet interpretable without the building's actual
geometry — run3 reads brighter toward -Z than +Z, which may mean the structure is
open on that side or may mean something else, and no claim is made here. Nothing
about this changes the pairing standard: still same-submission, still not a
validated illumination measure, still not room brightness or per-source occlusion.

## Why the CPU exposure shortcut fails — 2026-09-09, analysis

Anyone looking for a continuous ambient-occlusion signal will find this shortcut
and should not take it. The analysis below uses only the five captures already
preserved; no new game run was needed.

The shortcut: the same candidate can be recovered algebraically from the CPU
`exposureCacheHex` in every observation via `infer_visibility(lanes[8], lanes[5])`,
with no GPU readback, no fenced copy and no one-shot limit. Compared against the
paired GPU texture value in the same captures:

| capture | GPU texture | CPU median | CPU range over 20 samples |
|---|---|---|---|
| run1 open | 0.386534 | 0.316849 | 0.247303 .. 0.386272 |
| run2 at the wall | 0.117666 | 0.127199 | 0.117770 .. 0.127201 |
| run3 under the roof | 0.000031 | 0.002449 | 0.000000 .. 0.032105 |

It tracks well where the value is large and fails where the answer matters most.

**It is not staleness.** All twenty raw lane pairs in every run are distinct and
vary continuously, so the cache updates. The plateaus visible in the derived series
(the same value repeated ten times) are quantisation inside the decoder inversion,
not held data.

**It is conditioning.** `lanes[8]` is essentially the same in the open (~0.000174)
and under a roof (~0.000176), so it carries no visibility information at all.
`lanes[5]` differs by about5% (-6.12 versus -6.43) across a real difference of more
than a factor100 in the result. Recovering visibility means inverting a
near-exponential exposure relation, so a five-percent input perturbation becomes
orders of magnitude at the output. That is why the spread was WORST under the roof,
where the geometry is most stable — precisely the opposite of a geometric signal.

**It is also the wrong physical quantity.** `lanes[5]` behaves like an exposure/EV
term: it varies more under the roof than in the open, which fits auto-exposure
adaptation rather than occlusion. A lamp signal built on it would respond to what
the camera is pointed at, not only to whether a structure is overhead.

The sound quantity is the direct texture sample: geometric, not mediated by
exposure, and it produced the clean monotonic series below. Making it usable means
lifting the one-shot limit into a bounded periodic read of the small region of
interest. It does NOT mean copying 2MB per frame, and it does not mean substituting
this algebra.

## A signed distance field exists, and it was already in our artifacts — 2026-09-09

The review's advice to look one level upstream turned out to be answerable without
any new capture. The shaders extracted on 2026-09-06 were never mined for names.
They describe the whole structure.

**The distance-field pipeline, from the extracted shader names:**

```
GenerateDistanceFieldsCS
GenerateAxisAlignedDistancePass0_CS / Pass1 / Pass2
PropagateSignedDistanceCS
ClearInvalidSignedDistanceVoxelsCS
InitVoronoiSeedsCS, JumpFloodVoronoiDiagramsCS      (jump-flood SDF construction)
GenerateHiZLevel0FromSDF_CS                          (the hiZ is DERIVED from the SDF)
RaymarchLocalLightsCS, RaymarchDiffuseHitDistanceCS  (the engine raymarches already)
PadClipmapBorderTexelsCS, ClearVoxelsCS, InjectLightsCS, CopyVoxels_*
```

Two of those names matter beyond the SDF itself. `GenerateHiZLevel0FromSDF_CS` says
the depth pyramid that ProcessManyLightsCS samples is built FROM the distance field,
so the deferred hiZ route was always downstream of this. And `RaymarchLocalLightsCS`
says the engine already raymarches local lights, which is the operation we want.

**`PropagateSignedDistanceCS` in detail** (from
`artifacts/light-research/filtered-count-095db9ce-20260906-2111-71fe2870.ll`):

| binding | resource |
|---|---|
| SRV t66, space36 | `Texture3D<float>` |
| UAV u5, space38 | `RWTexture3D<float>` |
| CBV b1, space35 | `VoxelGlobalIlluminationConstantBuffer`, **768 bytes** |
| CBV b0, space1 | `GenerateVoxelConstants`, 80 bytes |

Thread group 8x8x8, body using Sqrt, FAbs and FMin — ordinary distance propagation.

**The decisive detail is that 768-byte constant buffer: it is the same one we
already decode.** The clipmap origins, the common anchor, the inverse extents and
the level selection all carry over unchanged, so a distance-field volume can be
addressed with the mapping already written and verified. Nothing would have to be
re-derived.

**Why this fixes the refuted method rather than patching it.** A signed distance
field stores distance to the nearest geometry. A positive value means free space
with that much clearance, which is exactly the information the sky-visibility field
lacks: there, free air inside an enclosure reads like solid rock, and the whole
failure mode followed from that. On an SDF the correct algorithm is also standard —
sphere tracing along the segment, stepping by the distance value itself — rather
than fixed-step sampling of a smoothed quantity, and it is cheaper as well as more
exact. The format is float, not the R8 of the sky-visibility volume.

**What is not known yet.** The volume's dimensions, its clipmap layout, the sign
convention and the world scale of a unit are all unmeasured; the shader takes them
from constants rather than hardcoding them. Whether the resource is resident every
frame, how it is bound, and whether it can be copied with the existing fenced
machinery are open. None of that is answered by a shader dump.

**Next step, unchanged in method from everything that worked today:** locate the
live `Texture3D<float>` behind t66/space36, verify its descriptor and dimensions,
and copy it with the same release-barrier and fence machinery used for the R8
volume. Then sphere-trace the lantern case again and compare against the validated
ground truth. The PIX capture from 2026-09-05 remains available if the live binding
proves hard to reach, but it is no longer needed to establish that an SDF exists.

## The occlusion test is refuted as a general method — 2026-09-09

An independent review, sought by the user, raised an objection that our own data
confirms. It is recorded here in full because it changes the direction of this
line of work.

**The objection.** The field is SKY VISIBILITY, not occupancy. Below ground, inside
a wall and deep under a roof all read near zero. If "deep under a roof" was a point
in free air, then free space can read the same as solid, and no general line of
sight can be recovered from this single scalar field.

**It was free air, and the refutation holds.** The camera under the barn roof stood
in open space and read 0.047033. Marching outward from it, still entirely in free
air:

```
+Z  0.0470  0.0051  0.0009  0.0020  0.0012  0.0001  0.0094 ...
```

Free air under a roof reaches **0.0001**, which is BELOW the 0.000661 measured
inside a building wall and comparable to the 0.000000 measured below ground. So a
low value does not mean solid; it means "sees no sky". The validated lantern case
worked only because both endpoints were outdoors. Run the same experiment inside a
building or a cave and both ends read near zero, and a naive minimum test would
report "blocked" for everything — precisely where fire occlusion matters most.

Two further points from the review are also correct: a longer segment with more
samples is systematically more likely to find a low value, and 0.25 gu steps on a
1 gu grid are fourfold oversampling of correlated, trilinearly interpolated data
that adds no geometric information.

**What was changed in response.** The marcher now lives once, in
`Decode-SpatialReadback.march_segment`, instead of being copied into each script
where the flaws could diverge. It steps at 0.5 gu, half the finest voxel, so coarse
levels are oversampled rather than undersampled. It excludes the endpoints' own
neighbourhoods. Points that no sampled clipmap covers are COUNTED and force
`unknown-uncovered` instead of being silently dropped, which was the first
recorded flaw. It requires a connected low run of real thickness before saying
blocked, rather than a single low sample. And it applies the relative rule the
review suggested: when both endpoints already read below the free threshold, the
verdict is `unknown-enclosed`, because the field cannot then distinguish a wall
between them from an enclosure around both.

**Effect on the survey.** Re-run against the RENDERED light feed — the earlier
survey wrongly used the authored array — 302 segments now come out as 119 blocked,
78 clear, and 105 honestly unknown (81 marginal, 19 enclosed, 5 too short). A third
of the cases no longer receive a verdict they cannot support. The earlier clean
bimodality was partly an artefact of silent skipping.

The validated lantern case still resolves correctly under the stricter rules:
`clear` with minimum 0.128 from the visible viewpoint, `blocked` with a 4.1 gu low
run from the occluded one, across all eight volumes.

**Revised plan, in priority order.**

1. **Find the resource upstream of this texture.** The R8 volume is the *result* of
   a geometric computation. Whatever feeds it — an opacity, occupancy or distance
   field — would be the right input for a line-of-sight test, and on a true
   occupancy grid the correct algorithm is voxel-exact DDA traversal rather than
   point sampling. This is the same targeted approach that found everything so far,
   in a system we are already inside; it is not a return to broad scanning.
2. **Build a ground-truth labeller from the depth buffer.** The review's best
   suggestion, and better than treating depth as the answer. For lights currently
   on screen we already have camera matrices and light positions, so projecting a
   source and comparing against scene depth yields hundreds of labelled
   visible/occluded examples automatically. Only then can competing statistics —
   minimum, quantile, low-run length, multiple offset rays — be compared on error
   rates instead of on one lantern. The depth resource is already identified as
   `g_hiZMap t15, space36` in ProcessManyLightsCS.
3. **Keep sky visibility for what it is good at.** It remains the right quantity
   for the ambient feed, where "sees no sky" is exactly the question. As an
   occlusion input it should be labelled a confidence, never a visibility answer.

Two further alternatives the review raised are recorded but not adopted now. An
engine-internal physics raycast would be ideal in principle, but calling engine
code with our own arguments is a different risk class from reading and copying, and
it breaks on every game update, which the build-guard rules here explicitly guard
against. Inline DXR would be exact if the game builds acceleration structures at
all — cheap to check, larger to integrate. Per-light shadow maps are exact for
lights that have them, with the difficulty being to locate each light's map and
matrices.

## Occlusion validated against ground truth — 2026-09-09, PID29632

First occlusion result with a known answer. A lantern on the player home wall, the
user stepping out of and back into its line of sight during one capture, and the
user afterwards confirming which position was blocked.

**Setup.** Lantern at (-10403.243, 613.836, -4419.101), identified in the RENDERED
ManyLights feed rather than the authored array. A 5 Hz position track ran alongside
the capture, 127 rows, so every transaction could be placed on the walk by matching
its own camera position. Eight transactions over about 20 seconds.

**The walk was smaller than intended** — 3.7 gu in z, with the third leg falling
outside the capture window — so only one transaction landed at the far position and
seven at the near one. That is too thin on its own, so the volumes were used
cross-wise instead: march from BOTH camera positions through EVERY volume.

| volume | from A, camera z -4420.99 | from B, camera z -4416.38 |
|---|---|---|
| 0 | 0.108369 | 0.000000 |
| 1..5 | 0.110426 | 0.000000 |
| 6..7 | 0.120829 | 0.000000 |

The positions are 4.67 gu apart and both about 17 gu from the lantern. The segment
from B is fully blocked in **all eight** volumes; the segment from A is blocked in
none. Because the volume refreshes in amortised blocks between transactions, the
distinction surviving all eight is evidence that it is geometric rather than a
temporal artefact.

**Ground truth: the user confirmed the middle position, B, was the occluded one.**
So min 0.000000 corresponds to "lantern hidden" and 0.108..0.121 to "lantern
visible", on this case. That is the first occlusion result with an answer rather
than an inference.

**ManyLights inclusion is confirmed useless as a visibility signal.** The lantern
stayed in the rendered feed for 126 of the track's 127 rows, including the whole
period when the user could not see it. The standing warning that an included source
is not a visible source now has a direct measurement behind it, and a consumer must
not treat feed membership as visibility.

**What this does and does not establish.** It establishes that the segment minimum
separated visible from occluded on one real case, consistently across eight
refreshes, with a gap of more than five orders of magnitude between the two states.
It does not establish a threshold, a false-positive or false-negative rate, or
behaviour under partial occlusion, in a doorway, through foliage, past a thin wall,
or at a grazing angle where a visible source might still read low. It covers one
geometry at 16..17 gu, entirely within clipmap 1, so nothing here speaks to the
coarser levels a distant source would use. The two flaws recorded with the survey
— silently skipped uncovered points and resolution changing mid-segment — are still
unfixed.

**Correction to the 100-segment survey.** That survey marched to
`lights.sources`, the authored engine array, not to `lights.rendered.sources`,
which is the filtered ManyLights feed the HUD shows and the one this lantern
appears in. The segments it measured were real, but the light set was the wrong
one; a re-run should use the rendered feed.

Evidence artifacts/light-research/occlusion-aba-20260909/pid29632-lantern-playerhome/
with the raw capture, the 5 Hz track, the tracker used, the native log and the INI.

## Per-point clipmap selection, and a 100-segment survey — 2026-09-09

Two steps toward occlusion, both offline on preserved captures.

**The clipmap can be selected per point, which was the range blocker.** The
sampler previously reused the reference's clipmap, so nothing beyond about 32 gu
could be reached. Reading the constants shows why that limit was avoidable. The
origins are the world position expressed in each level's grid: clip2 origin
(-10408, 612, -4420) against a reference world of (-10411.0, 614.1, -4419.6), with
clip1 exactly twice that and clip3 exactly half. And `wrapped` resolves to the
world position measured from a common anchor: `relative[level] / originScale[level]`
is IDENTICAL for levels 1, 2 and 3, at (-10408, 608, -4424) here, and
`wrapped = world - anchor` reproduces the stored lane exactly.

So for an arbitrary point: subtract that anchor, then run the shader's own cell test
per level. Verified against the known case — the reference selects clipmap 1 and
reproduces 0.158708192 exactly — and it extends the reach: three lights at 71..92 gu
that previously fell outside now select clipmap 3 and return 0.33..0.44, which is
plausible for sources standing in the open.

**A 100-segment survey.** Marching from the camera to every light in every
preserved capture, at 0.5 gu steps, taking the minimum along the way excluding
endpoints:

| band | segments |
|---|---|
| below 0.01 | 37 |
| between | 6 |
| above 0.05 | 57 |

Strongly bimodal, with only 6% ambiguous. Distance does not explain it: in the
Abyss a 77.3 gu segment read 0.000000 while a 71.6 gu one read 0.270112, and at the
player home the 8.2 gu segment read 0.000000 while the 90.3 gu one read 0.082124.
The coarse pattern also fits — the capture under the barn roof produced zeros
almost throughout, the open Abyss capture mostly values above 0.05.

**What the survey does NOT show.** It shows the statistic is bimodal. It does not
show which mode means "occluded", because there is no ground truth in this data:
no capture recorded whether a given light was actually visible. Bimodality is
consistent with a working test and does not establish one. ManyLights inclusion
cannot serve as truth either — the research has said throughout that an included
source is not a proven visible source.

**Two flaws found while running it, stated rather than smoothed over.** First, the
survey silently SKIPS points where no clipmap covers the position, so a segment can
be judged on partial coverage; that must become an explicit "unknown" result rather
than a quiet omission, and one segment did touch a level beyond the three the
shader samples. Second, long segments cross clipmap levels and therefore change
resolution partway — 1 gu voxels near the camera, 4 gu at level 3 — so a thin
obstruction far away can vanish and a coarse voxel can read low without a wall
being there. Both must be fixed before any threshold is proposed.

**Next is ground truth, not more statistics.** The A-B-A control: one place, one
session, a source in clear view, the same source with a wall interposed, then clear
view again, recording the segment profile each time.

## Segment occlusion — 2026-09-09, first offline test

The question was whether per-source occlusion needs a different technique from the
ambient work, or whether the same volume can answer it. This is a first test on
data already captured; it is encouraging and it is one example.

The volume distinguishes solid from open: measured values are 0.000000 below the
surface and 0.000661 inside a building wall, against 0.37 in open air a few metres
away. So marching the segment between two points and watching for near-zero values
is at least plausible as a line-of-sight test.

Tested on the player-home capture. The camera sat at -10411.00/614.15/-4419.57 and
the API reported a spot light 8.2 gu away at -10412.12/613.08/-4411.52, in the +Z
direction that the doorway profile had already shown falling to 0.00066. Marching
in eighteen steps, against two controls of the same length:

| segment | minimum along the way, endpoints excluded |
|---|---|
| camera to the light (+Z) | **0.000000** |
| control into the open (-Z) | 0.285248 |
| control upward (+Y) | 0.139232 |

The path to the light reads 0.000 to 0.004 across almost its whole length while
neither control approaches zero. A criterion as simple as the minimum along the
segment separates the cases by orders of magnitude here.

**Why this could matter more than the hiZ route.** A depth test answers only for
source centres that are on screen. The product case that motivated all of this —
lamps behind the player expressing what is behind the camera — is exactly the case
a screen-space test cannot serve. A volume march is view-independent and works off
screen, and it reuses the readback, the clipmap mapping and the native sampler that
already exist and are verified.

**What this is not.** One light, one geometry, one capture, and the light was only
inferred to be behind a wall rather than independently known to be. Nothing here
establishes a threshold, a false-positive rate or behaviour at a doorway, a window,
a thin wall or a partially open structure. The 1 gu grid is coarse for a point
source: a wall thinner than a voxel may be missed and a narrow opening may read as
closed. Amortised staleness applies as everywhere else.

**A range limit that must be solved first.** The three other lights in that capture
sat 71 to 92 gu away, outside clipmap 1's 64 gu extent, and the sampler currently
reuses the reference's clipmap for every point. Segment tests beyond about 32 gu
therefore need per-point clipmap selection across levels 1..3, whose extents are
64, 128 and 256 gu. That is well defined but not written.

**The controlled experiment this needs**, before any of it is promoted: the A-B-A
that the depth route was always going to require — a source in clear view, the same
source with a wall interposed, then clear view again, at one place in one session,
with the profile recorded each time.

## What the quantity most likely is — 2026-09-09, interpretation

Stated by the user and recorded here as the reading that fits every observation:
it is what its name says. Sky visibility from a point in space, reduced by whatever
stands between that point and the sky — ground, buildings, roofs, trees, clouds,
and in principle anything else that moves through the volume.

The concrete form that fits is **the fraction of the full sphere around a point
from which sky is reachable**, not of the upper hemisphere. That single reading
explains everything measured so far:

| observation | explanation under this reading |
|---|---|
| ~0.53 in the open at ground level | the lower half of the sphere is ground |
| rises with height: 0.159 → 0.562 over 10 gu | the ground subtends less from higher up |
| exactly 0.000000 below the surface | no direction reaches sky |
| 0.000661 pointing into a building | wall and roof |
| 0.000031 deep under a roof | fully enclosed |
| 0.0052 at 2 gu into the house, 0.374 at 2 gu outward | the doorway |

It also retires the earlier puzzlement that the value "does not reach 1 under open
sky". It should not. A point standing on ground cannot see sky in half its
directions, and 0.53 is the correct answer rather than a shortfall.

**Correction to the amortisation section.** That section said the cloud hypothesis
"is not needed and is not supported". That was too strong, and it conflated two
questions. Amortised block refresh explains WHEN a point's value changes — the
plateaus and steps — and that part stands. It does not explain WHY the value
changes at all at a fixed camera position. If the world were static, recomputing a
block would write back the same bytes; instead about 25% of voxels differ between
consecutive transactions with mean deviation 5.4 of 255. The content genuinely
changes, and moving occluders are the natural cause. What remains untested is
whether those byte differences are real scene change or temporal jitter in the
engine's own voxel sampling; the monotonic downward drift of the daylight run
suggests real change, while the non-monotonic night run does not settle it.

**What is interpretation and what is measurement.** The fraction-of-sphere reading
is a hypothesis that fits, not a measured fact: nothing here isolates a cloud, a
tree or a bird, and no controlled geometry has been used to confirm the solid-angle
form. A direct test would need a point far from every surface, where the reading
predicts a value approaching 1 — but the Y axis of the clipmap only spans 32 gu and
wraps, so it cannot be reached by offsetting, only by standing there.

**Hard bound found while testing this.** Offsets wrap toroidally. Y covers 32 game
units, X and Z 64 each; sampling +15 or +20 gu in Y returned exactly 0.0000 while
+30 returned precisely the same value as -2, which is the wrap made visible. The
±2 and ±5 pattern the plugin records is safely inside, but the contract must state
the bound or a later consumer will ask for ±20 and receive unrelated space.

## Loop closed in game, and the doorway effect measured — 2026-09-09, PID4760

Live series with the sampler wired in, standing at the player home door in a part
of the world never sampled before (x about -10405 against the barn's -10530), in
daylight. 8 of 8 transactions completed, values in the mid range 0.1587..0.2693,
which is where a comparison actually tests something rather than confirming zeros.

**Every native value matches the offline recomputation exactly: 104 of 104.**
Thirteen values per transaction — the reference plus twelve offsets — recomputed by
the decoder from the same volume and constants and compared without tolerance,
`agrees: true` in all eight, `disagreements: []` throughout. The native and offline
evaluations are therefore the same computation on real data, which is what allows
the volume to be dropped from a future payload.

**The payload is 1465 bytes against 540672 for the volume**, a factor of about 369.
That is the whole point of the native sampler.

**The doorway profile is exactly the effect the user described.** From the camera
beside the house door, one instant, transaction 0 at reference 0.1587:

| offset | 2 gu | 5 gu |
|---|---|---|
| +X | 0.179158 | 0.175289 |
| -X | 0.162635 | 0.387097 |
| +Y (up) | 0.314600 | 0.465113 |
| -Y (down) | 0.092391 | 0.000000 |
| +Z | 0.005212 | 0.000661 |
| -Z | 0.374170 | 0.317952 |

Into the building (+Z) the field falls to 0.00066 while the opposite direction
reads 0.318 and upward reaches 0.465, with the ground below at exactly zero. A lamp
mapped to +Z would go dark while one mapped to -Z or upward stays lit, from a
single capture, without any new mechanism. This is the cave-mouth behaviour the
product needs, measured rather than argued.

Nothing here changes the quantity's limits: offsets reuse the reference clipmap,
may read voxels of differing age because the volume refreshes in amortised blocks,
and the value remains a candidate engine sky-visibility factor rather than
irradiance, room brightness or per-source occlusion. The spread across the series
was 0.1107, larger than the 0.0156 measured under open sky, which is consistent
with a steeper field beside a building rather than with worse precision.

Evidence artifacts/light-research/series-20260909/run4-playerhome-door-pid4760/.

## Sampler wired into the probe — 2026-09-09, sampler.1 NOT live-tested

Each transaction now records natively sampled values beside the volume, and the
offline decoder recomputes them. This is what a feed would carry instead of
540672 bytes, and it makes the native and offline paths check each other on every
capture from here on.

**In the plugin.** After the fenced copy, the probe locates the 768-byte constant
window inside the whole GPU GI buffer exactly as the decoder does — by matching the
CPU copy of the same constants, at 256-byte alignment, and only when the match is
unique. An ambiguous or absent match is reported as `gi-window-ambiguous` or
`gi-window-absent` and nothing is sampled. With the window found it samples the
reference plus a fixed pattern of twelve offsets: six axes at 2 gu and the same six
at 5 gu. The payload is a few hundred bytes.

**In the decoder.** `verify_native` recomputes every one of those thirteen values
from the same volume and constants and compares them exactly, reporting
`nativeSampler: {checked, agrees, disagreements}`. A disagreement is listed with
the native value, the recomputed value and its offset — never smoothed over. When
the plugin reports `available: false`, no agreement is claimed at all.

**What this does and does not prove.** It proves the native and offline evaluations
agree on real data, which is what allows the volume to be dropped from a future
payload. It does not change the quantity, its meaning or any of its limits: the
offsets still reuse the reference clipmap, still may read voxels of differing age
because the volume refreshes in amortised blocks, and the result remains a
candidate engine sky-visibility factor rather than irradiance, room brightness or
per-source occlusion. The caveat travels inside the JSON so it cannot be lost.

**Verification.** 24/24 native CTests. 46 Python tests, four of them new: agreement
recomputed and confirmed, a broken reference caught, a broken offset caught with
its delta identified, and no agreement claimed when the plugin reports the samples
unavailable. `scripts/Verify-NativeSampler.py` still reports 56 comparisons across
seven captures with zero mismatches. Package 2.0.1-spatial-sampler.1 is built with
spatial defaults off and has not been installed, so the loop is not yet closed in
game.

## Native sampler — 2026-09-09, bit-exact against seven captures

The evaluation now exists in C++ as well as Python. This is the step that lets the
plugin turn a volume into a handful of numbers instead of shipping 540672 bytes,
which is the precondition for any continuous feed.

`src/spatial_sample.cpp` ports the offline model exactly: the same layout offsets,
the same clipmap selection over levels 1..7 with the same +-63/31/63 cell bounds,
the same float32 rounding of every intermediate, the same slab z mapping through
the DXIL constant, and the same linear-WRAP trilinear read accumulated in the same
loop order so the sums round identically. Three entry points: `DecodeReference` for
the position and clipmap, `SampleAtReference` for the exact reference path, and
`SampleAtWorld` for the offsets a directional read needs.

Two limitations are stated in the header rather than hidden. `SampleAtWorld` REUSES
the clipmap selected for the reference instead of recomputing it, which holds for
small offsets well inside the clipmap and not for arbitrary distances. And
neighbouring offsets can fall in different amortised update blocks, so several
offsets from one volume are not necessarily of the same age.

**Verification against real captures is bit-exact.** `scripts/Verify-NativeSampler.py`
extracts the constants and volume from every preserved capture, runs the native
code through its `--vector` mode and compares against the Python decoder that every
published number came from: **56 comparisons across seven captures, zero
mismatches**, agreeing to all 17 significant digits. That includes the reference
values 0.54354636445595861, 0.5968498114236247, 0.45754767087123838,
0.047033219041111129 and 3.1052094153216636e-05, the exact zeros returned when
sampling into the ground, and offsets at +-2, +-5 and +-3 game units on each axis.

The captures stay out of Git, so this verification is run by hand. The 22 synthetic
controls that DO run in CTest cover the algorithm rather than the data: weights
summing to one, a midpoint weighting two texels equally, negative coordinates
wrapping to the far edge while an interior coordinate does not see it, the
fallback branch reporting one without ever sampling, and the guards on inverse
extent, clipmap scale, null constants and a missing volume.

## The drift explained: amortised block updates — 2026-09-09, analysis

Offline analysis of the eight volumes in each series. No new capture. This retires
the drift as a mystery and replaces it with a mechanism that explains every plateau
seen all day.

**The clipmap does not move.** Origin (-21352, 3592, -7376), scale 2.0, relative
(-21360, 3584, -7376) and inverse extent 0.03125 are byte-identical across all
eight transactions in both series. Re-centring is ruled out as a cause.

**The volume is continuously rewritten.** Between consecutive transactions about
1.5 s apart, ~25% of all 540672 voxels differ, with mean absolute deviation ~5.4
out of 255. That rate is the same in every step and, importantly, the same by day
and by night: 24.7..25.8% throughout. Differences against transaction 0 grow
25.7% → 51.1% → 71.7% and then saturate, which is repeated churn, not convergence
after arrival.

**The update is spatially amortised in blocks of 16 z-slices, and it rotates.**
Within clipmap 1's slab (z = 67..130), grouping into four 16-slice blocks:

| pair | z67-82 | z83-98 | z99-114 | z115-130 |
|---|---|---|---|---|
| 0→1 | 0.63 | 0.22 | **0.00** | 0.14 |
| 1→2 | **0.00** | 0.46 | 0.52 | **0.00** |
| 2→3 | 0.06 | **0.00** | 0.32 | 0.72 |
| 3→4 | 0.57 | 0.35 | **0.00** | **0.00** |
| 4→5 | **0.00** | 0.33 | 0.73 | **0.00** |
| 5→6 | 0.22 | **0.00** | 0.11 | 0.72 |
| 6→7 | 0.41 | 0.48 | **0.00** | **0.00** |

In every observed step at least one block is byte-identical while others change by
11..73%, and which block is untouched rotates. Spot-checking a single untouched
slice (z=100 in pair 0→1) shows every one of its 32 y-rows unchanged.

**This is what produced the plateaus.** A given point's value can only change when
the block containing its voxels is refreshed. That is why every series all day
showed stable plateaus with occasional steps, why values repeated to 0.03..0.3%
inside a plateau, and why the "spread" and the "drift" were never scatter. It is
one phenomenon: amortised refresh.

**It also settles the day/night question mechanically.** The churn statistics are
identical by day and night, so the larger daylight range was the same amortisation
sampled across more refresh events, not a time-of-day effect on the quantity. The
earlier cloud hypothesis is not needed to explain the drift and is not supported by
this analysis; dynamic geometry may still be among what gets written INTO the
voxels, but it is not what makes the readings step.

**Hard consequence for any consumer.** A reading at a point can be several seconds
stale, depending on which block it falls in and when that block was last swept.
Sub-second responsiveness at a single point is not available from this field at
all, no matter how fast the readback is made. Smoothing is mandatory, and a
directional design that samples several offsets will mix voxels of differing age,
because neighbouring offsets can lie in different blocks. That last point is new
and must be respected by the feed contract.

## Day versus night, and a drift that dominates — 2026-09-09, PID33700

Third live series, deliberately at the same open-sky Abyss spot as the dusk run, in
full daylight (Day 40 Fri 9:15 AM against Day 58 Tue 8:29 PM). Camera 1.22 gu from
the earlier reference and IDENTICAL across all eight transactions — the player did
not move at all. The sky stream confirmed real daylight, mean radiance
[19.03, 23.90, 38.37]. 8 of 8 completed, no fallback.

Day values in order: 0.5968498114, 0.5972106568, 0.5819191259, 0.5820355312,
0.5818474919, 0.5106919785, 0.5105419812, 0.5105419812.

| | min | median | max | spread |
|---|---|---|---|---|
| dusk 20:29 | 0.527982 | 0.533635 | 0.543546 | 0.015564 |
| day 09:15 | 0.510542 | 0.581883 | 0.597211 | 0.086669 |

**The quantity is not static at a fixed position.** Three plateaus, drifting
monotonically DOWNWARD over twelve seconds — 0.597, then 0.582, then 0.511 — while
the camera stayed at the same coordinates to two decimals. Within each plateau the
values agree to 0.03..0.07%, so this is the instrument reporting a changing world,
not noise. Total drift 0.087, about 14%.

**Position is not the explanation.** Sampling the stored volumes offline at both
camera positions separates the terms over that 1.22 gu:

| sampled at | night volume | day volume |
|---|---|---|
| night camera | 0.543546 | 0.561349 |
| day camera | 0.549273 | 0.596850 |

Pure position costs only 0.0058 on the night volume and 0.0356 on the day volume,
confirming that the field really is flat in open sky, which is why this location
was chosen for the comparison.

**No time-of-day dependence can be claimed — and none can be excluded either.**
At a fixed position the day/night difference is 0.018 to 0.048 depending on which
position is used, while the drift WITHIN the single stationary daylight window is
0.087. The difference we are trying to measure is smaller than the quantity's own
movement over twelve seconds, and the day range 0.5105..0.5972 contains the whole
dusk range 0.5280..0.5436. The test therefore neither detects nor rules out a time
effect of this size, and the model that geometry and sky brightness separate
cleanly remains an assumption.

**What the drift probably is, stated as a hypothesis and not a finding.** The
screenshot shows a moving cloud layer below the platform, and the daylight run
drifts far more than the dusk run. If dynamic sky occluders such as clouds enter
this voxel field, the drift is real occlusion changing rather than instability, and
the user's earlier suggestion that wind-moved geometry could move the value would
be right in kind. This is untested. A capture during visibly still weather, or two
captures minutes apart at one spot, would separate cloud motion from a slow
systematic drift.

**Consequence for a consumer.** An ambient term built on this will move by roughly
ten percent over seconds even standing still under open sky. Smoothing is not
optional. It also means any future day/night claim needs repeated captures at one
position across a longer baseline, not one series per condition.

Evidence artifacts/light-research/series-20260909/run3-abyss-DAY-pid33700/ with
raw JSON, derived.json, pre/post snapshots, the pre-capture ambient sample, native
log, INI and notes.

## Precision, plateaus and a steep field — 2026-09-09, PID31352, series.1

Second live series, eight transactions standing under the barn roof, camera at
-10531.94/611.35/-4424.06, only 1.27 gu from the point run3 measured earlier that
day. 8 of 8 completed and decoded, fences 1..8, no fallback, reference equal to the
API camera position to 0.00 gu for the fifth time. Cadence 1031..1531 ms, so the
configured 1000 ms minimum is reachable when dispatch selection lines up.

Values in order: 0.0470332190, 0.0471239112, 0.0471551429, 0.0471701582,
0.0471641521, then 0.0375554250, 0.0375656267, 0.0375737416.

**Read the shape, not the range.** The naive spread is 0.0096, about 20% relative
at this level, which looks alarming beside the 2.9% measured at 0.53. But the
series is not scatter: it is two stable plateaus with one step between them. Within
the first plateau the values span 0.00014, about **0.3%**; within the second they
span 0.0000183, about **0.05%**. The Abyss series has the same shape. So the
instrument repeats far better than the range suggests, and what moved during the
twelve-second window was the world or the volume refresh, not the measurement.

For a consumer this is the good case: a precise reading that occasionally steps.
Temporal smoothing absorbs steps; it cannot rescue genuine scatter.

**Cross-validation of the offline directional method.** run3's stored volume,
captured roughly six minutes earlier in a different process, was sampled offline at
today's camera position. It predicts **0.0341**, against today's live plateaus of
0.0470 and 0.0376 — and against 0.000031 at run3's own point 1.27 gu away. The
prediction lands within 10% of the second plateau and within a factor 1.4 of the
first, while correctly crossing three orders of magnitude over that 1.27 gu. This
is the first independent check that sampling the stored volume at an offset
predicts what a real capture at that offset actually reads.

**A steep field is a product warning.** Moving 1.27 gu changed the reading by a
factor of about 1500. Under a roof edge the field is extremely steep, so a
lamp mapping that samples fixed offsets will swing hard when the player moves a
metre indoors. That is physically correct behaviour, not instrument error, but a
consumer must expect it and smooth accordingly. It also means the earlier
"repeat spread" question is regime-dependent in a second way: position
reproducibility, not just measurement noise, dominates wherever the field is steep.

Evidence artifacts/light-research/series-20260909/run2-under-roof-floor-pid31352/
holds the 12453931-byte raw JSON, derived.json, pre and post snapshots, the native
log and the INI actually used.

## Spread and the open-sky anchor — 2026-09-09, PID9464, series.1

First live series. Eight transactions in one process, standing still in the Abyss
high above the world under completely open sky, chosen deliberately as an extreme.
8 of 8 completed and decoded, fence values 1..8 in order, no failure, all
`texture-sample` with no fallback, reference equal to the API camera position to
0.00 gu for the fourth time.

Values in order: 0.5435463645, 0.5279821106, 0.5279821106, 0.5279821106,
0.5383259607, 0.5383259607, 0.5383259607, 0.5289446066.

**Repeat spread at one place: 0.0156** (min 0.52798, median 0.53364, max 0.54355),
roughly 2.9% relative at this level. The repeated identical values are the R8
quantisation of the volume showing through; the steps between them are a few byte
levels, so the variation is real rather than numerical mush.

**The open-sky anchor is about 0.53, not 1.0.** This is the single most important
number in this section. With nothing overhead and nothing nearby, the quantity does
not approach one. Every earlier reading has to be re-read against that ceiling: the
0.386 and 0.458 measured beside the barn are about 73% and 86% of the open maximum,
not "half the sky". Do not present this value as a fraction of visible sky, and do
not normalise it to 1 without establishing what the true maximum is across
locations — one Abyss sample is not proof of a global ceiling.

**The occlusion series clears the noise floor.** Against 0.0156: open 0.458 versus
wall 0.118 is 22x the spread; wall versus under-roof 0.000031 is 7.6x; and even the
two open points beside the barn, which differ by 0.071, are 4.6x. Every distinction
drawn earlier that day is comfortably above noise. The caveat is regime: this
spread was measured at ~0.53 and says nothing certain about the floor near
0.00003, where quantisation must dominate and relative spread must be far larger.

**Achieved cadence was ~1500 ms against a configured 1000 ms minimum**, with frames
advancing by exactly 90 per transaction at 60 fps. The interval is a floor, not a
rate: the real limiter is the probe's own 500 ms `lastAttempt` throttle together
with exposure dispatch selection. A live feed wanting more than roughly 0.7 Hz must
revisit that throttle rather than lowering the configured interval.

Evidence artifacts/light-research/series-20260909/run1-abyss-open-sky-pid9464/
holds the 12453950-byte raw JSON, derived.json, pre and post snapshots, the native
log and the INI actually used.

## Repeated measurement — 2026-09-09, series.1 NOT live-tested

The one-shot limit is lifted. This was the pivotal engineering risk: every planned
use of the quantity depends on being able to read it more than once per process.

Configuration is two new keys, `[Research] SpatialReadbackCount` (1..8, default1)
and `SpatialReadbackIntervalMs` (250..10000, default1000). Out-of-range values fall
back to the single transaction that is already proven. **The interval is a minimum,
not a sample rate:** a transaction still only arms on a selected exposure dispatch,
so the achieved cadence is whatever the game offers and must be read from the
recorded ticks rather than assumed.

The copy path is deliberately unchanged: same validated barriers, same whole-buffer
GI and exposure copies, same texture copy at the validated release barrier. What is
new is only the surrounding lifetime.

**Safety properties, each covered by a control.** One readback destination is
reused for the whole series and is re-armed only after the previous fence completed
and its map finished, so the destination is never in flight. Each transaction
signals its own increasing fence value. Any failure ends the series at once and is
reported as the last record, because a series must never continue past a state the
instrument cannot explain. A consumed series cannot restart in the same process.

**Report and decoder.** `private-spatial-readback-v5` carries a `transactions`
array, each entry with its own CPU observation, alongside the requested and
completed counts and the interval. The newest transaction is repeated at the old
`textureReadback` location so v4-era readers keep working. `decode_series` decodes
each entry independently and reports min, median, max and spread. An entry that
cannot be decoded appears as `undecodable` with its reason rather than vanishing,
and identical inputs yield a spread of exactly zero instead of an average.

**Host verification.** 23/23 CTests and 290 WARP/detour checks with zero
debug-layer warnings. The new control runs three transactions on their own queue
with different uploaded contents per pass and asserts that each record holds THAT
pass's texture fill and GI bytes — the point being that a reused destination could
silently return a stale copy, and it does not. It also asserts advancing fence
values, that the series ends exactly at its budget, and that a consumed series
refuses to restart. 42 Python tests. All synthetic; the series has not run in game.

**Why this matters for the open questions.** Eight transactions standing still is
eight measurements at one place, which is exactly the repeat-spread gap left open
by the occlusion series. The spread also sets the bar for the directional work:
a directional difference only means something once it exceeds the spread at a
fixed point.

## Directional ambient — requirement and where the data already is

Stated by the user as the product target, recorded here as a design note, not a
measurement. Lamps distributed around the player follow the camera, but must also
express what is behind it. Reference scenario: standing in a cave mouth looking in,
lamps toward the interior go dark unless a fire bowl stands there, while lamps
toward the opening stay bright if it is day outside.

A single scalar at the camera cannot express this — it dims every lamp equally.
The parts, and what exists today:

- **Directional sky radiance — available.** `/v1/ambient` carries nine SH
  coefficients per RGB channel, so it can be evaluated per direction already.
- **Directional occlusion — latent in data we already copy.** The readback holds
  the whole 64x32x264 volume, and every result so far has read a single texel of it
  at the camera position. Sampling the stored volume at offsets around that
  position yields a spatial profile, and in a cave mouth the field should differ
  over a few metres. Limits to respect: this approximates direction by sampling a
  scalar field at neighbouring points rather than evaluating a directional function
  at one point, and clipmap extent and resolution bound how far out it stays
  meaningful. Entirely unmeasured so far — but testable offline, with no new capture.
- **Local lights such as a fire bowl — already works** through existing ManyLights.
- **Occlusion of those local lights — not built.** Still the separate hiZ route.

Keep this repository vendor-neutral: no Philips Hue naming and no consumer-specific
model in the API. The Hue consumer lives in its own repository and any published
contract here must stay a neutral directional ambient/occlusion feed.

## Occlusion series — 2026-09-09, five captures, readback.4

Five paired captures, one transaction per process, each preceded by a recorded
`/v1/snapshot`. This is the first series rather than a lone number, and it answers
the question the whole spatial line was opened for: does this engine quantity
respond to local enclosure?

All five decoded identically in form: `texture-sample`, no fallback, GI window at
offset0, `cpuGiEqualsGpu` true, `exposure-window-absent`.

| capture | game time | camera yaw | sky-vis candidate | measurement point (= camera) |
|---|---|---|---|---|
| readback.4 open | ~22:32 | — | 0.457548 | -10526.5 / 613.4 / -4408.5 |
| run1 open | 22:32 | 180 | 0.386534 | -10530.6 / 611.3 / -4407.5 |
| run2 at the wall | 20:41 | 0 | 0.117666 | -10530.1 / 610.9 / -4415.2 |
| readback.1 deep in stall | ~22:32 | — | 0.000985 | -10537.7 / 613.1 / -4415.6 |
| run3 under the roof | 20:37 | 0 | **0.000031** | -10532.0 / 611.5 / -4422.8 |

**The ordering is monotonic in enclosure and spans roughly15000x.** Open sky sits
at0.39-0.46, pressed against a wall at0.12, deep inside a partly open stall at
0.00099, and fully under a roof at0.000031.

**The controlled pair.** run2 and run3 were captured at the same game time (20:41
and20:37, both after the same save reload), at the same camera yaw0, in the same
session, differing essentially only in how deep the camera stood under the
structure. They differ by a factor of about3800. The game-time difference against
the earlier night captures therefore cannot be what produces the effect — and it
pointed the wrong way anyway, since run2 and run3 were captured under a brighter
dusk sky than the higher-reading night captures.

**Reference identity, proven three times.** In every run with a recorded snapshot
the decoded `referenceWorldCandidate` equalled the API camera position to **0.00
game units**. The reference is the camera position exactly — not the player root,
and not merely "view context" as the earlier native provenance work could only
establish. This is also an independent corroboration of the assumed GI layout: the
reference is decoded from GI constants under `--assume-adapt-exposure-layout`,
while the camera position is read through a completely separate memory path, and
they agree to two decimals.

**Practical consequence of that identity.** The measured player-to-camera boom was
6.48gu, collapsing to2.16gu when a wall stands behind the camera. A capture must
therefore place the CAMERA under cover, not the player; at yaw180 the boom swings
the sample point out from under a roof while the player still stands beneath it.
Turning the camera in place is a cleaner experimental move than walking, because
it changes the sample point by up to two boom lengths without moving the player.

**Limits.** No point has been captured twice, so repeat spread at a fixed point is
still unmeasured; the two open points, 4.7gu apart, differ by0.07, which is a proxy
and not a substitute. The pairing remains same-submission and is not
binding-proven. The quantity is a candidate sky-visibility term: it is NOT room
brightness, irradiance, lux, a physical roof percentage, or per-source occlusion,
and none of these numbers may be published as a local-illumination field. The
exposure window remained absent in every capture and still wants a separate look.

Evidence artifacts/light-research/roof-control-20260909/ holds run1, run2 and run3,
each with raw JSON, derived.json, pre- and post-capture snapshots, native log, INI
and notes; the two earlier captures keep their own directories.

## First paired GI and texture capture — 2026-09-09, PID25944, readback.4

The mechanism works. This section records the first complete transaction and,
just as importantly, what it does and does not license anyone to say.

Installed ASI verified as 3F93F2568184011A3A7BE1FF1390DFC6A99AF1D87F872AFF31233180C64F508E.
INI saved12:55:40 with SpatialProbe1/SpatialReadback1/AmbientProbe0, native log
opened12:56:16 carrying "Spatial readback v4 IDLE", health playing and supported
build. One stationary capture, user standing OUTSIDE the barn with the four lamps,
at NIGHT — the same area as the earlier runs, but not under the roof.

Transaction: `gpu-complete-texture-and-buffers`. Texture540672 bytes, whole GI
buffer65536, whole exposure buffer65536, one queue, fence value1, exactly one Map,
`giGpuFramePaired` and `exposureGpuFramePaired` true. CPU controls 20/20 at error0,
frames10319..10802 progressing, `giCopiesMatch` true throughout, one selected
exposure at frame10395, reset generation34. Raw report1570839 bytes.

Decode: the GI window was located by content at offset **0**, and `cpuGiEqualsGpu`
is true — the fenced GPU bytes are byte-identical to the CPU read of the same
constants. Shader branch `texture-sample`, no fallback, clipmap selected without
fallback. Sampled R8_UNORM **0.5424523291287616**, candidate sky visibility
**0.4575476708712384**, from a linear-WRAP sample at
(-164.4762725830078, 19.167692184448242, 0.28228759765625). Interpolation neighbors
are x33/34, y4/5, z74/75 with bytes135..207, so this is an ordinary interior sample
of the volume, not a boundary or fallback artefact.

The exposure window was NOT found. The CPU exposure cache does not appear anywhere
in the copied output buffer, so the decoder reports `exposure-window-absent` and
produces no inverse and no direct-vs-inverse difference. That is a missing
corroboration, not a failure of the GI pairing, and it wants a separate look:
plausible causes are a different output layout, a cache written from elsewhere, or
values that changed between the GPU write and the later CPU read.

**What may and may not be said about the number.** readback.1 measured a candidate
of0.000985 inside the roofed stall at night; this run measures0.4576 outside the
barn at night, and the two sample coordinates are close. The direction is what a
sky-visibility quantity should do. It is nevertheless NOT an indoor/outdoor result:
two single samples, different processes, different frames, different game times,
one computed from CPU constants and one from paired GPU constants, with no repeat
and no controlled movement between them. It is also not a roof percentage, not room
brightness, not irradiance and not per-source occlusion. What it does establish is
that the paired path produces a plausible, non-degenerate value at all.

**Defect found in this run.** The report's `rootSetsBeforeExposure`,
`rootSetsInsideExposure`, `tableSets`, `heapSets`, `rootThreadConflict` and
`descriptorHeaps` all read zero here, while the arrays correctly show the same
seven descriptor tables at root indices5,6,7,8,9,12,14 and the unrelated
upload-ring CBV at index1 (0x10CA1F1700, again a new address). The counters lived
in the reported struct and were cleared by the next list Reset. readback.5
snapshots them with the arrays and adds a control proving a later Reset cannot
rewrite a dispatch snapshot. Reports from readback.3 and .4 must be read with the
arrays, never the counters. No copy or fence was affected.

Evidence artifacts/light-research/spatial-readback4-live-20260909-pid25944-outside-barn-night/
holds the raw JSON (SHA256 6C4DF3191EFA18108F136F606D2354F60F0D9C16B121DA3D4A6D0FE79FC54D5E),
derived.json, the native log and the INI actually used.

**Next.** The mechanism is no longer the question, so the next step is a controlled
experiment rather than another lone number: a bounded repeatable pair of captures
under the roof and outside, in one session at the same game time, reported as a
spread and still labelled same-submission. One transaction per process means one
restart per capture, so the count must be agreed with the user first.

## Same-submission pairing — 2026-09-09, readback.4 design

The user chose this route over resolving the descriptor tables. It is deliberately
a weaker evidence standard than readback.2 aimed at, and the report, the decoder
and this section all have to say so wherever a number appears.

**The two claims, separated.** readback.2 tried to prove identity and timing with
one mechanism: a root descriptor pointing into the pinned buffer proved both which
resource the dispatch used and that the copy belonged to it. The live measurement
showed that mechanism cannot exist for this shader. readback.4 therefore proves
them separately:

- *Identity* comes from the validated native producer/consumer path already
  documented in "Native spatial provenance" — the exposure consumer's owner chain
  to the GI and exposure resources, checked every observation, with device, heap
  type, UAV flag, width bounds and nonzero GPU addresses re-validated at arming.
- *Timing* comes from copying inside the same recording and the same submission,
  immediately after the observed original dispatch, under one fence, with no Map
  before that fence completes. This is what the CPU exposure cache never had.

What is genuinely lost is per-dispatch binding proof: nothing in this design shows
that *this* dispatch read *that* buffer. It shows that the buffer identified by the
validated path held those bytes at that point in that submission.

**Consequences in the plugin.**

- The root scan no longer gates anything. Its hits become `giBindingHits` and
  `exposureBindingHits`, alongside the table/heap evidence; in the live game they
  are zero and that is reported rather than hidden.
- Whole pinned buffers are copied from offset0, not 768/128 bytes at a
  binding-derived offset, so the plugin never guesses a window. Each buffer is
  bounded to 65536 bytes by the existing validation, so the readback allocation
  grows by at most 128KiB beyond the texture.
- The GI buffer barrier covers `CONSTANT_BUFFER|SHADER_RESOURCE`, because the read
  path is not observable when the descriptor lives in a table. The exposure buffer
  keeps `UNORDERED_ACCESS`; the resource is validated as ALLOW_UNORDERED_ACCESS.
- Format `private-spatial-readback-v4` carries `pairing:
  same-submission-not-binding-proven` plus a `pairingCaveat` string stating the
  identity source, the table binding and the offline window resolution.
- Unchanged and still failing closed: same list, source, reset generation and
  recording thread; exactly one direct `Dispatch(2,1,1)`; the validated post-exposure
  release barrier for the texture; one queue, one fence, one Map.

**Consequences in the decoder.** It locates the 768-byte CPU GI copy inside the
whole GI buffer at 256-byte alignment and the 64-byte CPU exposure cache inside the
exposure buffer, reporting `giWindowOffset`, `exposureWindowOffset` and
`bindingProven: false`. Absent or ambiguous GI matches raise `gi-window-absent` or
`gi-window-ambiguous` instead of choosing one. A missing exposure window sets
`gpuExposureInverseUnavailable` and produces no inverse. Note what a GI match does
and does not mean: it locates the window, and the fence establishes freshness. It
is not itself proof that the CPU copy and the GPU bytes are from the same frame —
if they were not, the match would simply fail.

**Host verification.** 23/23 native CTests. 239 WARP and native-detour checks with
zero debug-layer warnings, now asserting whole-buffer copies with the GI block at
its real offset256 and the shader output at1024. A new control mirrors the live
game directly: descriptor tables at five root indices, one unrelated upload-ring
CBV at index1, zero root hits into the pinned buffers — and the transaction still
pairs and copies. Dispatch-context negatives still fail closed: wrong thread group,
wrong dimensions, another list, a missing forwardable barrier, and a second
dispatch inside one exposure. 37 Python tests cover the found, absent and ambiguous
windows, the required pairing label, wrong buffer sizes, resource-identity
mismatch and unchanged v1/v2/v3 behaviour. All synthetic. Package identity and the
one next live step are in the current HANDOVER checkpoint.

## Descriptor tables identified — 2026-09-09, PID4340, readback.3

The widened window worked and produced the decisive evidence. This closes the
root-descriptor line of investigation. It is a statement about binding mechanics
only; nothing here measures light, roofs or sky visibility.

Installed ASI verified as 9B35DEC7A0960A0FC94B3146B9104ECA3F930344A1947467CC5FEF7E4ED66352.
INI saved10:19:18 with SpatialProbe1/SpatialReadback1/AmbientProbe0, native log
opened10:20:05 carrying the "Spatial readback v3 IDLE" line, health playing and
supported build. One stationary capture, standing OUTSIDE the barn with the four
lamps — the first outdoor spatial run, though this capture yields no light value.

CPU controls healthy: 20/20 observations, error0, frames10382..10850 progressing,
`giCopiesMatch` true throughout, exactly one `selectedForTextureReadback` at frame
10480, bankFlag0, single GI resource5110197488 and exposure resource5278233376.

Binding state at the selected `Dispatch(2,1,1)`:

| field | value |
|---|---|
| `rootSetsBeforeExposure` | 20 |
| `rootSetsInsideExposure` | 1 |
| `tableSets` | 98 — see the correction below, NOT a dispatch-time value |
| `heapSets` | 6, heaps 0xC92DE6A0 and 0xC92DEE20 — same correction |
| `rootThreadConflict` | false |
| live root CBV | one, index1 = 0x1036631400 |
| live root UAV | none |
| live root SRV | none |
| live descriptor tables | root indices 5, 6, 7, 8, 9, 12, 14 |

**Correction, made after the readback.4 run.** The counter and heap fields above
lived in the reported struct and were cleared by the NEXT list Reset, so what a
report file shows for `tableSets`, `heapSets`, `descriptorHeaps` and the root-set
counts is whatever survived until the file was written, not the state at the
dispatch. The ARRAYS are snapshotted at the dispatch and are authoritative; the
seven live table indices above therefore stand, and the readback.4 run reproduced
exactly the same seven. Do not quote 98 or 6 as dispatch-time counts. readback.5
snapshots the counters alongside the arrays. No copy was ever affected.

The twenty root sets before the exposure are exactly what readback.2 could not
see, so the window fix is confirmed by measurement rather than by argument. The
single surviving root CBV sits ~53GB outside the pinned GI buffer and appeared at
a different address in PID2652 (0x1039FF1700 vs 0x1036631400), which is what an
upload-ring per-dispatch constant looks like. It is not the GI buffer.

**Conclusion: GI and the exposure output reach this shader through descriptor
tables.** `required-native-root-bindings-not-unique` is therefore the correct
final answer for any root-descriptor design, not a defect to tune. The decoder
refuses the capture with `no-completed-gpu-copy`, so this run has no derived
values. readback.1's fenced texture copy is untouched by all of this.

**Where a fourth step could go.** Two honest routes, both bounded, neither started:

1. *Resolve the tables.* Build a heap-slot to resource map from observed descriptor
   creation and descriptor copies on the device, read each heap's GPU handle start
   and increment size, convert a bound table handle into a slot, and match a
   bounded range of following slots against the pinned GI and exposure resources.
   This restores true per-dispatch binding proof, including the offset. It is a
   much larger native surface than anything here, and descriptors created before
   the probe arms are unknown, so it must fail closed rather than guess.
2. *Separate identity from timing.* Resource identity already rests on the
   validated native producer/consumer path documented in "Native spatial
   provenance"; temporal pairing rests on copying within the same recording and
   submission immediately after the selected dispatch under one fence, which the
   current code already does. Dropping the root requirement then costs only the
   offset — recoverable by copying the whole 65536-byte GI buffer and a bounded
   exposure prefix and letting the offline decoder locate the 768-byte window that
   matches the CPU GI copy. The report and decoder would have to label this as
   same-submission pairing and never as binding-proven.

Evidence artifacts/light-research/spatial-readback3-live-20260909-pid4340-outside-barn/
holds the raw JSON (SHA256 AE2A0DB3CB928E26062E17F3546522E8AD9291CFE98E2F9307F4824564958B2E),
the native log and the INI actually used. No source, package or API change was
made during this live-test turn.

## Widened root window — 2026-09-09, readback.3 implementation

Direct answer to the rejection recorded below. No new game experiment, no heap
scan and no change to any public stream, schema or HUD value.

**The mistake being corrected.** A D3D12 root argument persists until it is
overwritten or the root signature changes. readback.2 nevertheless treated the
game's exposure dispatch wrapper as the binding window: `Arm` cleared `cbv_`/`uav_`
on entry, and `SelectedNative` in `spatial_probe.cpp` gated every root observer on
the thread_local `active` observation, which `Dispatch` sets only around
`originalDispatch`. Roots issued earlier in the same recording were therefore
invisible by construction. The live run saw exactly what that predicts: one root
CBV belonging to some other resource, and no root UAV at all.

**What readback.3 changes.**

- Root observation is gated by command-list identity (`SpatialReadback::Observes`)
  instead of the thread_local observation, so it covers the whole recording of the
  pinned list. `Arm` no longer clears anything.
- The recording is cleared where D3D12 actually invalidates root arguments: on the
  observed `Reset` (after a successful reset) and on `SetComputeRootSignature`.
- New observers: `SetComputeRootShaderResourceView` (vtable39),
  `SetComputeRootDescriptorTable` (31) and `SetDescriptorHeaps` (28). The existing
  slots stay CBV37, UAV41, RootSignature29, Dispatch14, and all seven must resolve
  from the same live vtable or the probe refuses to install.
- Descriptor tables and heaps are **recorded only**. A `D3D12_GPU_DESCRIPTOR_HANDLE`
  is never resolved to a resource and never used as a copy source; a table written
  at a root index clears that index's buffer address, exactly as the hardware does.
  Their purpose is to make "bound through a table" distinguishable from "not bound".
- GI may pair through a root CBV **or** a root SRV into the same pinned buffer, since
  both are direct root descriptors with a checkable address, bound offset and size.
  The buffer barrier uses CONSTANT_BUFFER or SHADER_RESOURCE accordingly, and the
  alignment requirement follows the kind (256 for CBV, 4 for SRV). Exactly one GI
  hit across both arrays and exactly one exposure UAV hit are still required.
- Every recorded root argument must come from the recording thread; a conflict is
  reported as `rootThreadConflict` and fails closed.

**Report and decoder.** Format is now `private-spatial-readback-v3`, adding
`nativeSrv`, `nativeTable`, `descriptorHeaps`, `giFromSrv`, `rootThreadConflict`,
`tableSets`, `heapSets`, `rootSetsBeforeExposure` and `rootSetsInsideExposure`.
`Decode-SpatialReadback.py` accepts v3, validates the GI root address against
`nativeSrv` when `giFromSrv` is set and against `nativeCbv` otherwise, and rejects
a thread conflict. v1 and v2 semantics are untouched: re-decoding the preserved
readback.1 capture reproduces its `derived.json` byte-for-byte.

**Host verification.** 23/23 native CTests. The pair test installs all seven
production detours on DIRECT and COMPUTE, then sets the root signature, the GI CBV
and the exposure UAV **before** `Arm` — the live failure order — and still reaches
a fenced, byte-exact GI/output/texture pairing in one submission: 223 WARP and
native-detour checks, zero debug-layer warnings or errors. New negatives cover a
descriptor table replacing the GI root (recorded, no copy), roots left from a
previous recording being cleared by Reset, and a root SRV pairing at offset260.
32 Python tests pass. All of this is synthetic; it does not establish that the
game binds GI or exposure this way. Package identity and the one next live step
are in the current HANDOVER checkpoint.

## Live binding rejection — 2026-09-09, PID2652, readback.2

The implementation described in the next section was installed and run once. It
rejected before copying anything. This section records that measured negative;
the next section's "NOT live-tested" title now describes only its build state.

Installed ASI verified as 941C9DDBC215F56676570376630D6C0A71277513E4B5B1824167DA95342529FD.
An earlier launch (PID29052, 09:38:02) started with the package's default
SpatialProbe=0/SpatialReadback=0 because the INI edit had not been saved; its
native log shows no "Spatial readback v2 IDLE" line and no one-shot was spent.
The INI was then saved at09:41:07, the game restarted09:41:18 as PID2652, and the
log confirmed the armed v2 instrument before the single event was signalled.

CPU side healthy: complete=false only because reason=`sample-limit`; 20/20
observations, error0 throughout, frames12772..13266 progressing, giCopiesMatch
true for all twenty, one `selectedForTextureReadback` at frame12824 (tick4707437),
bankFlag0 and one stable identity each for GI (5279847184) and exposure
(5264622496). enhancedBarriers true. giResourceMetadata unchanged from readback.1:
BUFFER, width65536, DEFAULT heap1, GetHeapProperties S_OK.

The native `Dispatch(2,1,1)` reached `NativeDispatchEnd` on the pinned list and
recording thread — `nativeDispatches` incremented to1 and the context guard passed,
so the failure is NOT dispatch selection. The binding scan then found:

- `nativeCbv`: exactly one nonzero slot, root index1 = `0x1039FF1700`
- `nativeUav`: entirely zero
- `giBase` = `0x2F7ED0000`, `exposureBase` = `0x2F5820000`
- the observed CBV lies ~53GB outside the pinned GI buffer, so cbHits=0, uavHits=0

`spatial_readback.cpp:87` therefore failed closed with
`required-native-root-bindings-not-unique`, HRESULT0x80004005. No barrier group,
no `CopyBufferRegion`, no texture copy, `issued` false, `mapCalls`0, `fenceValue`0,
`queue`0. `Decode-SpatialReadback.py` refuses the file with `no-completed-gpu-copy`,
so no derived value exists and none may be quoted for this run.

**What this rules out and what it does not.** It disproves the readback.2 premise
that the selected exposure dispatch binds GI constants and the exposure output
through compute root descriptors *within the game's dispatch wrapper*. It does not
show absence of light, a broken detour (one root CBV was captured, so the CBV hook
fires), or anything about roof occlusion. readback.1's fenced texture copy and its
0.999014 sample remain valid and untouched.

**Two remaining candidates, both testable in one further capture.** First, the
recording window: `SelectedNative` in `spatial_probe.cpp:88` gates on the
thread_local `active` observation, which `Dispatch` sets only around
`originalDispatch`, and `Arm` clears `cbv_`/`uav_` on entry — bindings issued
earlier on the same command list are invisible. Second, descriptor tables: the
build has no `SetComputeRootDescriptorTable` observer at all, so "bound via table"
and "not bound" are indistinguishable in this data. A root SRV binding for the GI
data would likewise be unseen.

Evidence artifacts/light-research/spatial-readback2-live-20260909-pid2652-bindings-rejected/
holds the raw JSON (SHA256 03C94ED43A2FEA71C14FA6A3BDEB02CA815D740BCFBABBAC281659978F7E2287),
the native log and the INI actually used. No source, package, config or API change
was made during the live-test turn itself.

## GPU pairing implementation — 2026-09-09, readback.2 NOT live-tested

Reused exposure consumer, actual live wrappers and existing byte dumps; no heap
scan or additional stationary experiment. PID8772/readback.1 remained unchanged.
Bounded read-only native-code/resource inspections carry full SHA/process controls.

**CBV route:** consumer143544BFF selects filter+560/+568; wrapper+18 is R8 at
143544CD6, R9d=0. Command vtable+438 ->1437DA550 calls outer.vtable+88 to select
view0; actual outer vtable145BD0960 ->142DEB870. That getter returns
storage+140[index], bounded by +148 count1. 1437DA583 reads the view pointer;
1437DA5A1 queues it via1437DA350 into command+638 (64-byte pending entry).
Within original Dispatch,1437B439B ->1437B5CD0 applies pending bindings through
143759990. Root-binding path1437B5EB7..5F9E reads view+50 GPU VA, passes it in
R8, root indexEDI, to native SetComputeRootConstantBufferView slot37/+128 when
compute flag command+136 nonzero. Equivalent second path1437B6100..618D.
Native root UAV method is slot41/+148. SDK C-interface offsetof independently
asserts CBV37,UAV41,RootSignature29,Dispatch14 (plus existing Barrier80/Reset10).
Descriptor-table paths exist: new diagnostic must reject missing direct roots,
not pretend every binding uses the root descriptor branch.

Live GI0 resource132625460, view21D3674F780, GPU VA2F80D0000;
GI1 resource131E0F7D0, view21D3674F5A0, GPU VA2F80E0000;
exposure resource132CCBAF0, view21D3664B4C0, GPU VA2F5A20000.
In all three instances view+50 equaled resource base VA (offset0). Independently
inspected actual D3D12Core GetGPUVirtualAddress getter7FFDE4498760:
48 8B 81 10 01 00 00 C3 -> mov rax,[rcx+110];ret. This read-only driver-field
cross-check is NOT a product anchor; native code calls official GetGPUVirtualAddress
and derives actual offsets from observed native bindings. Never carry these heap
addresses or D3D12Core+110 across restarts/updates as application assumptions.

**Exposure copy provenance:** consumer1435450CC takes ExposureOwner+C0. After
Dispatch it invokes1437DD810 at14354510D, with source offset0/size from helper+48.
Non-type5 helper branch1437DDDA2 -> command.vtable+650=1437B6A40. This helper
requests copy source/dest engine states and applies barriers via+1B0; native
CopyBufferRegion at1437B6C82 uses outer+30/storage+168 and actual passed offsets.
Existing engine cache is still read later at14354511E and copied into owner+D8;
it does not guarantee current GPU completion. New diagnostic avoids that cache:
captures source output immediately after the observed original native Dispatch.

**Implementation readback.2:** selected GI and exposure resources pinned beside
the known texture. Worker verifies DEFAULT buffers, width768..65536, same canonical
device, output UAV flag and nonzero GPU VAs. Arming requires same selected resource
bank as preparation (another bank is skipped, not silently substituted).
While the selected exact exposure invocation is active, observe native root CBV,
root UAV, root signature and Dispatch. Signature invalidates saved roots. Require
one matching root per resource, correct offsets/bounds/alignment, same list/thread
and one direct Dispatch(2,1,1). Actual native Dispatch is forwarded FIRST; then
buffer barriers synchronize COMPUTE/CONSTANT_BUFFER and COMPUTE/UAV to COPY_SOURCE,
copy768 GI +128 output bytes to separate readback tails, restore original access.
Whole-buffer enhanced barriers use Offset0/SizeUINT64_MAX per SDK contract:
https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_buffer_barrier
Texture copy still occurs at validated post-exposure release packet. If any later
guard fails, already-issued work retains COM objects, never maps on guessed time.
One actual list/queue/fence covers ALL copies, not merely adjacent CPU observations.

New private-spatial-readback-v2 stores requested/copied/nativeDispatch counts,
all64 observed CBV/UAV root slots, source base VAs/offsets/root indices, GPU GI and
output bytes, plus prior texture/control evidence. Paired flags require successful
fence and readback; CPU scene/frame/cache remain unpaired. No public API change.
Decoder v1 retains CPU-only semantics. v2 uses explicit paired-GPU model input,
compares GPU-derived direct visibility with inverse from GPU output stores and
reports CPU GI equality. Does not fabricate flags31 or double CPU cache reads.
Logical meaning/coverage still requires controlled live roof/outside evidence.

23/23 CTests, including new165 checks using actual WARP shader + all four native
COM detours on DIRECT and COMPUTE. GI offset256 and output1024 are deliberately
different/nonzero. Shader writes output from that same CB; readback byte-exact,
texture byte-exact, gated GPU proves no early Map/paired flag. Missing roots,
root-signature invalidation and duplicate source binding reject without copies.
187 existing texture/lifecycle/fence controls also pass. Initial test setup used
legacy COPY_DEST creation; corrected to COMMON + explicit enhanced transition
after debug-layer warning. Final zero debug warnings/errors.14+13 Python tests
validate old/new modes, independent GPU reference, inverse/store/binding guards.
No new native hooks were injected into current game; new ZIP installation pending.

Evidence artifacts/light-research/rawpages/ (all .bin plus .meta.json):
spatial-cb-pairing-code-pid8772 SHA510EE049236B93C2DEE34BA5CB9E06E6DA1BBE3AC4F4480FCBAEC7F0088821F1;
spatial-cb-root-binding-pid8772 SHA73D023F382AF07B639F7D93F6538A3676D973FCA09610B6E2205EB19D0FC5367;
spatial-cb-address-proof-pid8772 SHAD66AED1A99E75740ED054A7C4A3356F3D67B80DA65799B30E2DBBA5400A0B78F;
also spatial-cb-view-code-pid8772,spatial-cb-copy-helper-pid8772,
spatial-native-bind-emitter-pid8772 (additional bounded intermediate call paths).
Package/hash/config and ONE next live test in current HANDOVER checkpoint.

## First direct live texture readback — 2026-09-09, PID8772

Installed2.0.1-spatial-readback.1 ASI verified against immutable package:
SHA256686BFA08BF49F9AFD3F78936B9BCB7966445B0372AB2DECD955307A5852FE666.
PID8772 started08:42:40.8526107 CEST; native log opened08:42:42, IDLE instrument
and normal ManyLights/global sky active. INI SpatialProbe1/SpatialReadback1/
AmbientProbe0, Lights and ManyLights and Ambient enabled. Exact supported EXE
hash4D99C15C.../build25116796; health playing, no error. Git initially clean.

User stood between the four lamps at NIGHT and reports indoors brighter than
outside. Prior location is the partly open roofed stall. This run is ONE stationary
location, not a doorway/light-toggle comparison and not an untouched baseline.
No need to darken lamps: candidate sky visibility is distinct from total lighting.
Started once through Start-SpatialProbe.ps1; released user immediately afterward.

Native report complete=true,20/20 valid CPU observations, frame17704..18195;
all GI before/after copies equal, exactly one selected exposure at frame17730.
Texture source5140177008, native list/list75135413632, Reset generation9.
Recording thread4212, submitting thread27240, actual queue5156726752.
Matched full release Sync128->0,Access128->NO_ACCESS0x80000000,Layout6->1,
flags0, ALL subresources (FFFFFFFF/0/0/0/0/0). Issued one copy, Close/Execute
guards accepted, own fence value1. Release/submit tick1289546; worker completion
1289562,16ms later. HRESULT0, Map calls1. No GPU layout/completion inferred from
passive history. Health sequence12556 before ->13858 after, playing/error=null.

Readback allocation2162496 bytes; footprint0,Texture3D64x32x264,R8_TYPELESS60,
rowPitch256. Padding stripped ->540672 bytes, range0..255,33 distinct values.
Texture bytes SHA256
04c89b9be9a120646e246b161af7c222df951d5992c331d937dcce6d342e8726.
This is the first actual fenced game-texture readback for this spatial route.

Offline decoder accepted build, control, texture/fence, sampler and view settings.
CPU GI model selects clipmap1 (NO fallback), world reference
(-10537.6552734375,613.0991821289062,-4415.5927734375), normalized pre-wrap UV
(-164.65086364746094,19.15934944152832,0.2553304135799408).
Eight sampled neighbors are x21/22,y4/5,z66/67; seven255, one247 at(22,4,66)
with weight0.031410481889722064. R8_UNORM linear-WRAP gives0.9990145731171852,
thus candidate saturate(1-sample)=0.0009854268828147772.

Independent sanity check, explicitly NOT decode_cache validation: unpack each
single exposureCacheHex with struct.unpack('<16f',...) and pass lanes[8],lanes[5]
to the existing infer_visibility(). At selected frame17730 its algebraic value
is0.000976572812038453: absolute difference0.0000088540707763242. Across20
single CPU reads, candidate range0.00024414009722536798..0.010666289564135754.
Never fabricate flags31 or double-read stability for those single reads. GPU
cache age remains unknown; close agreement is encouraging, not proven same-frame
pairing or a physical percentage of sky/roof coverage. Earlier inverse results
are NOT invalidated by a different night/place/time value.

**Important next-binding evidence:** selected GI resource5140272224, and all20
GI metadata entries, report BUFFER dimension1,width65536,heapType1 DEFAULT,
CPUPageProperty0/MemoryPool0,GetHeapProperties S_OK. CPU source is768bytes, so
do not assume it starts at resource offset0 or is a directly mappable UPLOAD
resource. Resolve actual CBV GPU address/offset from the known binding path
(consumer143544CD6, selected wrapper+18; producer1432A3150), then legal buffer
copy state/boundary and paired exposure output. No fresh broad scan needed.
Reference CPU constants are NOT a GPU CB snapshot. Nested cpuReference metadata
in derived.json describes the reused CPU-only model; top-level GPU texture
completion describes the separate actual copy, not a contradiction.

Preserved raw JSON/native log/INI plus derived.json in
artifacts/light-research/spatial-readback-live-20260909-pid8772-stall-night/.
Raw spatial-binding-8772-1289031-1.json size1303461, saved08:48:41 CEST, SHA256
A58BC2AE2CBB7B2E30B9B44829243A5C88C75AE52E2C2AD462FD7AF14AF6460E.
Derived command: Decode-SpatialReadback.py --input RAW --out FRESH-derived.json
--assume-adapt-exposure-layout. No plugin/config/API edits or second capture;
one-shot remains consumed in PID8772. Next step is binding/output pairing, then
bounded repeatable direct control. Separate per-source hiZ route still pending.

## Direct texture readback implementation — 2026-09-08, NOT game-tested

PRIVATE2.0.1-spatial-readback.1, package/hash and restart instructions in HANDOVER.
Separate Research/SpatialReadback=1 opt-in requires SpatialProbe=1, AmbientProbe=0
and active normal capture. Passive mode remains unchanged. No API/schema change.
User ended today's work after building; DO NOT claim installed or validated live.

`spatial_readback.cpp/.h` owns a bounded single transaction independently of the
lossy interval tracer. Discover pins the live texture/list inside its actual
consumer call; worker prepares matching-device READBACK/fence/placed footprint.
Require one-node device, enhanced barriers, DIRECT/COMPUTE list and exact texture
shape/format. Do not allocate or Map on a recording thread. A successful observed
Reset begin/end establishes a private generation; unknown/reset/closed list never
arms. Capture20 CPU controls as before; select at most ONE exposure invocation.

Before that invocation, arm its list/source/generation/frame/thread. After it,
require the two inline GI copies to match. The NEXT matching target texture
barrier on the SAME list/thread must arrive within250ms with the exact live
release tuple, one target entry only, no flags, ALL subresources. A different
target transition, release inside the dispatch, duplicate entry, overflow,
intervening Reset or Close without release rejects the transaction.
This uses the current native packet as evidence, never a lossy trace history.

At that boundary record enhanced SHADER_RESOURCE->COPY_SOURCE, whole-volume
CopyTextureRegion to a device-derived footprint, COPY_SOURCE->SHADER_RESOURCE,
then forward the untouched original engine packet ONCE. The original final
GENERIC_READ layout, all other groups/resources and release flags survive.
The inserted barriers use COMPUTE/SRV -> COPY/COPY_SOURCE -> COMPUTE/SRV scopes.
Enhanced access/layout compatibility reference:
https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html

Known successful Close required. Existing shared Execute hook invokes the private
observer before/after its ORIGINAL call. Match one list occurrence, generation,
actual queue's canonical device/type and submission thread. Signal OWN fence on
that actual queue after Execute; only worker GetCompletedValue permits Map.
Return from Execute is not completion. Post-submit list reset does not cancel
already queued work. Failed/ambiguous issued work keeps process-lived COM refs;
5s timeout never frees GPU destinations. One request per process also bounds
failed allocations. Broad global tracing is disabled in this mode.

JSON private-spatial-readback-v1 keeps20 CPU controls plus textureReadback with
packed540672 bytes (X fastest, padding removed), footprint, chosen CPU context,
release packet, reset generation, queue/thread/fence/ticks and errors. No paired
GI GPU-CB or exposure-output readback added yet. `giResourceMetadata` records the
selected CB's GetDesc/GetHeapProperties to guide that next step without guessing
its state or offset. Exposure-cache bytes are still ONE CPU read of unknown age.
The copy changes command recording; it is NOT an untouched baseline.

Offline `Decode-SpatialReadback.py` validates build, completion, progressing
control, sample context and sampler. Reuses `Decode-ExposureContext.py`'s CPU GI
reference calculation and uses eight R8_UNORM neighbors, normalized texel-center
coordinates, WRAP XYZ, linear spatial filtering at LOD0. Derives candidate
saturate(1-sample) or an explicit fallback-one branch. Always labels GI and cache
GPU pairing FALSE; not ambient RGB/lux, sun shadow or per-source visibility.
The byte histogram/raw texture remain useful even when the CPU reference fails.

Host validation:22/22 CTests,187 direct WARP/debug-layer assertions, zero warnings
or errors. Known nonconstant full volume copied byte-exact on DIRECT and COMPUTE,
including last slice/padded rows; deliberately blocked GPU proves no early Map.
Tests preserve a multi-group engine packet, validate one shot and reject wrong
source/list, missing/failed Reset, unstable GI, foreign thread, duplicate/changed
release, missing/failed Close and wrong queue type. Failed submissions never Map.
These are synthetic command-list tests, not an actual game shader/frame proof.
14 existing +7 new Python tests cover wrap seams/negative coordinates, centers,
constant volume and mandatory no-pairing/fence/context guards. Package validated.

Tomorrow: install through DMM only after game shutdown, enable config, one new
event-triggered live run; preserve raw JSON/INI/log and verify progressing health.
Then decode with `--assume-adapt-exposure-layout` to a NEW product artifacts file.
Do not repeat the broad trace or begin a generic doorway test first. Paired GPU
GI/cache and the independent per-light hiZ route remain separate pending work.

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

## First passive live capture — 2026-09-08, PID23516

User installed private2.0.1-spatial-probe.1 through their mod workflow and reported
standing among the four fire lamps in a partly open **roofed stall**: two closed
walls, one fully open side, one doorway side. No indoor/outdoor transition was
requested or inferred. This is NOT the earlier open-outdoor camera control.
Installed ASI SHA256 matched the immutable expanded package:
`A5F8F82C6F28E3A61C09BC3518DAF35D7CD5DEF04A509286186A7416A02C6167`.
SpatialProbe=1, AmbientProbe=0; native log showed IDLE before the single request.

One run via Start-SpatialProbe.ps1:20 observations over9594ms, all error=0,
complete=true, controlProgressed=true, frames21107..21567. All20 GI before/after
copies matched. Actual in-process GetDesc confirmed Texture3D64x32x264,1mip,
R8_TYPELESS(60),flags4/UAV. One resource identity throughout, two command-list
identities; nativeList7 equals nativeList in these observations. Enhanced flag
true. The52-byte sampler description matches the previously resolved slot12:
MIN_MAG_LINEAR_MIP_POINT, U/V/W WRAP, register12/space4.

Reused decode_spatial on the matching CPU copies:20 candidate contexts, all
clipmap1/texture-sample branch. Reference world position first
(-10537.091797,612.622864,-4421.095703), last
(-10537.091797,612.622620,-4421.095703); this is the camera-linked reference,
not a player-position measurement. First unwrapped sampler coordinates
(-164.642059,19.144464,.476910233). This is NOT a sampled texel or GPU-frame pair.
The exposure cache in this format is only ONE raw copy: do not fabricate the
double-copy/flags31 contract of decode_cache to make an inverse look validated.

**Coverage limit:** first observation installed the passive Barrier detour;
19 subsequent observations reported it installed. Zero matching target texture
barriers, no overflow. The observation scope includes only one exposure Dispatch
CPU invocation. No global Barrier-call count/control is present, so the result
cannot distinguish transitions elsewhere from an interception/identity gap.
It does NOT prove no barriers, a particular current layout, or copy safety.
Do not issue a transition from a guessed SHADER_RESOURCE state. Static Dispatch
bytes were rechecked from existing rawpages:1437B4388 calls1437E1930,
1437B439B conditionally calls1437B5CD0,1437B43AB calls virtual+1B0 before the
native Dispatch at1437B4468. Their presence alone does not establish where this
resource's transition is actually emitted. Follow that coverage question rather
than repeat stationary captures with the identical instrument.

The captured raw SRV-view object starts with engine vtable0x145BCC460, not a
DXGI format field. It must not be reinterpreted as a native SRV descriptor.
Typed view format remains to be resolved through the actual descriptor producer.
R8_TYPELESS resource format alone is insufficient to choose UNORM interpretation.

Raw JSON, log and INI preserved unchanged under
`artifacts/light-research/spatial-live-20260908-pid23516-stall/`.
Raw `spatial-binding-23516-8839093-1.json` SHA256
`D04CB79201035FA26D38CE27AD9B9F53028A6B9DF168CA57FD1F0BD7401E15D6`.
After capture health remained playing/error=null, sequence22597. No new GPU
command/copy, API modification, installed file change or second recording.
User released from standing still once the capture finished.

**Next step:** same texture, bounded passive coverage beyond the Dispatch window,
with actual list/reset-generation/submission provenance and a control that proves
the barrier path was intercepted; resolve the typed SRV. Only then select a safe
copy/fence boundary for paired direct texels, GI constants and exposure comparison.
No more generic walking tests to solve an instrumentation gap. This measurement
does not yet provide stall ambient intensity, sunlight shadowing or lamp occlusion.

## Typed view and interval trace — 2026-09-08,22:16

Followed the SAME texture's native SRV builder; no new heap scan or alternate GI
route. In PID23516 storage0x46367DD4C40 has vtable145C444B0; view0 object
0x4636A75DF80 has vtable145BCC460. The exposure binding at143545087 explicitly
passes view index0 (xor r9d,r9d at14354507B).

**Typed format provenance:**143D7AB90 constructs the texture SRV. It passes
storage+B0 and the selected view settings to143D78BF0 at143D7AF05, then submits
the40-byte result to native device CreateShaderResourceView at143D7AF3E
(vtable+90), using resource storage+100 and descriptor CPU handle viewObject+40.
143D78BF0 selects a format table from143D78650. Internal format15 branches at
143D7881D: resource format60/R8_TYPELESS, linear view format61/R8_UNORM.
The actual live view0 settings have override15, plane0, no sRGB preference;
the storage descriptor also has format15. Thus the native builder selects
**R8_UNORM61**, not UINT/SNORM or typeless for shader reads. Texture flags/depth
select SRV dimension8/Texture3D and component mapping0x1688. This is native-code
provenance with matching live settings, NOT a captured CreateSRV call or sampled
texels. Future spatial v2 snapshots include the48-byte storage descriptor too.

**Actual enhanced barrier emission:** command virtual+1B0 ->1437D97C0,
enhanced path virtual+1C0 ->1437B2AF0 ->1437B2B00. The latter converts engine
Sync/Access/Layout through143D02460/143D02560/143D02660, builds native texture
barriers from command+6B0/count+6B8 and outer+30/storage+100. At1437B3057 it
calls nativeList vtable+280, the verified Barrier slot80. It then clears counts
at+698/+6A8/+6B8. This proves the static emission path, NOT that this target
transition occurs inside our old Dispatch window. No guessed engine-enum cast.
Native barrier fields follow the [SDK texture-barrier contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_texture_barrier).

Evidence: artifacts/light-research/rawpages/spatial-srv-barrier-provenance-pid23516
(.bin/.meta.json), six fully read8192-byte ranges, live telemetry control present.
Binary SHA256498FC6753794AF4281DA66F04BAD07B0E2F7E0492880FC7F1E9A5B9ED1E8F81B.
Heap addresses are evidence only; new runs still resolve via the exposure owner.

**Private instrument v2:** existing exact-build/signature-gated Dispatch observer
now starts an interval trace after discovering the actual Barrier, Reset and
Close implementations from that live list. Original COM arguments/HRESULTs are
forwarded. Target texture gets AddRef while alive in the consuming call and is
released after trace shutdown. Resource/implementation changes mark incomplete.
The trace has global Barrier-call and texture-entry counts, exact-resource
barriers across lists/threads, Reset/Close begin/end with HRESULT, exposure
begin/end and Execute begin/end. Queue observation reuses the existing render
capture hook through an optional atomic callback; no duplicate Execute detour.

Fixed8192 events,512 list identities; callback try-locks never wait. Drops,
oversized inputs and capacity overflow are explicit. Unknown initial generation
stays unknown until an observed successful Reset begin/end. Reset generations
are NOT allocation identities. Event order is CPU interception order; submissions
on different queues or lists do not acquire a proven GPU order from that number.
No Signal/fence, new GPU command or GPU completion/state assertion is added.
Coverage is only the discovered implementations; zero counts/lossy trace must
not become an absence claim. First and last partially observed list generations
need special care before a future copy transaction is designed.

Host controls cover out-of-Dispatch/other-list target hits, foreign resources,
missing/failed Reset, failed Close records, submission identity, contention,
3200 calls across8 threads (accepted+dropped must balance),8192-event/512-list
overflow, disabled trace and real WARP/MinHook Barrier/Reset/Close interception.
The existing WARP light/ambient/shared-sky tests also verify the actual shared
queue observer receives balanced callbacks without changing copied payloads.
21 native CTests and14 Python tests; host tests are not live evidence for v2.

Package2.0.1-spatial-probe.2 created immutably, package validation/payload equality
passed. ZIP SHA2567A26996CE09E26A888C092C2BEBC27AC167124C9EA11E1CAFED9641A05540E00.
Default SpatialProbe=0. User must close game, install whole ZIP via DMM, set
SpatialProbe=1/AmbientProbe=0, load, then request one run. No new generic movement
test. Current installed ASI/spatial-probe.1 unchanged. Actual safe copy boundary,
GPU pairing/direct texture validation and independent source visibility remain
open; raw/smoothed/global-sky streams and their APIs are unchanged.

## Live interval result — 2026-09-08, PID32956

Verified newly installed spatial-probe.2 ASI hash and SpatialProbe=1/AmbientProbe=0,
native IDLE log and supported playing health. ONE event-requested capture:
20/20 samples error0, matching GI CPU copies, frames7186..7679. Actual resource
descriptor/sampler/view settings remain consistent with the prior findings;
reference position(-10537.131836,613.093445,-4415.895508), clipmap1 in first
CPU-model observation. Same camp region, not an instructed indoor/outdoor test.
Existing telemetry continued (sequence2790 before,5248 after,error=null).

**The bounded interval trace overflowed; preserve its negative quality result.**
During9.375s it counted376447 Barrier calls,1197253 texture entries and7673
matching target barriers,7272 Reset,7266 Close,5829 Execute calls.1136 callbacks
were dropped by try-lock contention.8192 events filled after2.734s; only2258
target barriers are stored, across16 lists.26 of those have unknown reset
generation. Two submitting queues are represented. Top-level complete=true is
the20-sample limit, NOT a lossless trace. Do not promote a partial trace into
safe current-state inference or CPU order into inter-queue GPU order.

All stored target barriers cover ALL subresources (indexFFFFFFFF,numMips0),flags0:

| Stored count | Sync before/after | Access before/after | Layout before/after |
| --- | --- | --- | --- |
| 703 | 1 / 128 | 0 / 128 | GENERIC_READ1 / SHADER_RESOURCE6 |
| 282 | 1 / 128 | 0 / 16 | GENERIC_READ1 / UNORDERED_ACCESS3 |
| 282 | 128 / 128 | 16 / 128 | UNORDERED_ACCESS3 / SHADER_RESOURCE6 |
| 991 | 128 / 0 | 128 / 0x80000000 | SHADER_RESOURCE6 / GENERIC_READ1 |

0x80000000 is NO_ACCESS (JSON serializes the enum as signed -2147483648), NOT
a corrupt negative access flag. These are actual native API values, not engine
state enums. Keep unsigned bit patterns when decoding.

**All five stored exposure contexts show the same useful sequence**, on
list0xC91C86F0, known reset generations7/20/33/46/59:
GENERIC_READ->UAV->SHADER_RESOURCE, exposure-begin/end, SHADER_RESOURCE->GENERIC_READ,
Close S_OK, Execute on queue0x135F1E820. First example event orders772/776,
777/778,779,780/781,782/783. Later exposure-begin orders2286,3794,5303,6805.
Thus the observed transitions occur BEFORE/AFTER, not inside, Dispatch; the old
per-Dispatch zero is consistent with these positive out-of-scope events. The
global losses still preclude certifying an exhaustive per-list history from v2.

Next candidate is not another large tracing run: intercept the actual release
barrier associated with a just-observed exposure invocation. Its incoming
LayoutBefore/AccessBefore/SyncBefore provide a live contract to validate before
planning a bounded copy/restore/forward transaction. Preserve all other group
entries and the engine's final state; abort on mismatched resource/list/generation,
unexpected transitions, multiple conflicting target entries or missing context.
Use WARP validation and the actual submitting queue/fence, never a timer or an
arbitrary queue. The [native barrier contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_texture_barrier)
and [CopyTextureRegion requirements](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-copytextureregion)
still apply; this paragraph is a design direction, NOT an implemented safe copy.
Paired GPU GI constants/exposure output remain separate from the current CPU
snapshots. No directly sampled sky value, player irradiance or light occlusion yet.

Raw JSON/log/INI copied unchanged into
artifacts/light-research/spatial-live-20260908-pid32956-stall/.
Raw spatial-binding-32956-10539062-1.json SHA256
181CFBFB5A7F1559BEA340E618D3720468CCAEBD6B8D57BF0A5CFE2197D501AF.
User released once recording finished. No second run, ASI/config/package/source
change or additional GPU command in this turn. Existing feeds remain unchanged.

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

## RaymarchLocalLightsCS — first look, 2026-09-09

Read offline from `artifacts/light-research/filtered-count-1322f152-20260906-2111-52c33a4a.ll`,
no capture needed. Bindings:

| binding | resource |
|---|---|
| SRV t233, space36 | `Texture3D<float>` |
| SRV t224, space36 | `Texture3D<uint4>` |
| UAV u1, space38 | `RWTexture2D<uint2>` |
| UAV u4, space38 | `RWTexture2D<uint>` |
| samplers s3, s12 | |

Thread group 8x8x1, so this is a screen-space pass writing 2D targets, not a
volume pass. It marches a `Texture3D<float>` — plausibly the finished distance
field — alongside a `Texture3D<uint4>` that is likely a voxel light index.

Note the register differs from `PropagateSignedDistanceCS`, which used t66/space36
for its input and u5/space38 for its output. Both are `Texture3D<float>` in space36.
Whether t66 and t233 are the same resource at different bind points, different
clipmap levels, or different fields entirely is UNKNOWN and must not be assumed.

Not yet read: the step logic, hit threshold and how visibility is derived. That is
the part worth having, since it would remove the guesswork from sphere-tracing
parameters. The file is 1850 lines and available offline.

## The raymarch volume is a different, finer clipmap — 2026-09-09

Read offline from `RaymarchLocalLightsCS`. Its 3D `SampleLevel` builds the z
coordinate like this:

```
z < 0 ? z+1 : z          wrap into [0,1)
* 128.0                  128 usable slices per level
+ level * 130            stride 130 = 128 + 2 border texels
+ 1.0                    skip the leading border texel
* 0x3F4F81F820000000     = 1/1040 exactly = 8 levels * 130
```

So this volume holds **8 clipmap levels of 130 z-slices**, 128 usable plus two
border texels, which is what `PadClipmapBorderTexelsCS` maintains.

Our sky-visibility texture uses the same construction with different numbers:
64 usable slices, stride 66, scale 1/264 = 4 levels * 66.

| volume | levels | usable z per level | format |
|---|---|---|---|
| sky visibility (already copied) | 4 | 64 | R8 |
| raymarch target | 8 | 128 | float |

The raymarch volume is finer on both axes, which fits a field meant for precise
tracing while the R8 texture is a coarser derived product. It also means the
mapping we verified for the R8 texture does NOT transfer unchanged: the slab
stride, the level count and the scale constant all differ, even though both take
their origins from the same 768-byte constant buffer. The x and y coordinates are
built from a shared scale factor that is not yet resolved.

Two consequences. The coarse-level worry recorded earlier is smaller here than
feared, because level 0 of this volume is finer than anything we have sampled so
far. And the reach is larger: eight levels rather than four.

Still unknown: which resource this is (t233/space36 at the bind site, against
t66/space36 for PropagateSignedDistanceCS), its x and y dimensions, the sign
convention, the world scale of a unit, and whether it can be copied with the
existing fenced machinery.

## RaymarchLocalLightsCS step logic — 2026-09-09

Read offline. The marching loop confirms sphere tracing and gives the engine's own
step conventions, but it does NOT give a hit threshold, because this pass gathers
light rather than answering visibility.

Loop termination:

```
iteration + 1 < 256          hard cap of 256 steps
t + step < maxDistance       accumulated distance bound
```

Step selection each iteration, where the sampled distance is the volume value:

```
if (sample <= cellSize && param <= 64)
        step = FMin(analyticStep, sample)     sphere tracing proper
else    step = analyticStep                    coarse step when far from geometry
step = FMax(step, minimumStep)                 floor, so it cannot stall at a surface
```

So the engine steps by the distance to the nearest surface, clamped below by a
minimum, and falls back to a coarser analytic step when the sample exceeds the
current cell size. That is the classic sphere-tracing arrangement and it is
directly reusable for a line-of-sight trace.

**What is NOT here.** No hit test and no visibility output. The loop accumulates
lighting and uses the distance field only to choose step sizes, so nothing in it
defines when a surface counts as struck. The earlier framing that "the engine
already raymarches local lights against this structure" is therefore too strong: it
marches through the structure, it does not answer our query.

The threshold question points at `RaymarchDiffuseHitDistanceCS`, whose name states
it returns a hit distance. That shader has not been read.

Also unresolved and needed before any of this can be used: the sign convention of
the field, its x and y mapping, and the identity of the live resource.

## The engine's hit criterion is integrated, not a distance test — 2026-09-09

Read offline from `RaymarchDiffuseHitDistanceCS`
(`artifacts/light-research/gi-entry-864eec9d.ll`). It binds the SAME volume as
RaymarchLocalLightsCS — `Texture3D<float>` t233/space36 and `Texture3D<uint4>`
t224/space36 — and writes `RWTexture2D<float4>` u42/space38.

The loop terminates on this test:

```
value < 0.5   ->  keep marching
value >= 0.5  ->  stop, this is the hit
```

and the continuation branch caps the march at a dynamic iteration limit and an
accumulated distance below 10000 units, stepping by FMax(candidate, minimum).

**The tested value is an accumulator, not a sample.** It enters the loop as
`phi float [ previous, continue ], [ 0.000000e+00, entry ]`, so it starts at zero
and carries forward, and the values feeding it are `Saturate` results clamped to
[0,1]. The engine therefore accumulates saturated coverage along the ray and calls
a hit when the accumulation reaches **0.5**.

**This contradicts the assumption we were working under.** Both the outside review
and this analysis had expected a distance field traced until the distance falls to
near zero. What the engine actually does is integrate occlusion along the ray and
threshold the integral. That is much closer in spirit to the minimum-along-the-
segment heuristic we refuted, except done correctly: an integral rather than an
extremum, which is exactly the more robust statistic the review suggested when it
proposed an integral or a run length over a bare minimum.

**Consequences.** A line-of-sight test built here should accumulate rather than
look for a single low sample, and 0.5 is the engine's own threshold rather than one
we would have to calibrate. It also means the field at t233 may be an opacity or
coverage volume rather than a signed distance field; the SDF names elsewhere in the
pipeline describe other resources, and which one t233 actually is remains open.

**Not established.** The chain from the 3D `SampleLevel` at line 1214 to the
accumulated value was not traced instruction by instruction, so how a sample
contributes to the accumulator — weighting, step-length scaling, any density
factor — is unread. The sign convention question is now differently shaped: if this
is coverage rather than distance, there may be no sign at all.

## The full sample-to-hit chain, closed — 2026-09-09

Traced instruction by instruction in `gi-entry-864eec9d.ll`, lines 1206..1290. This
answers the questions the outside review posed and CORRECTS two claims made here
one commit earlier.

**The recurrence.**

```
d        = SampleLevel(Texture3D<float> t233, sampler, u, v, w)   trilinear, LOD 0, channel 0
coverage = saturate(1 - ((d + bias) / width(t))^2)
A        = saturate(A + coverage * gate)                          gate is 0 or 1
hit      when A >= 0.5
step     = FMax(FMin(stepCandidate, d), minStep)                  only while d <= width and level <= 2
```

**CORRECTION 1: t233 behaves as a DISTANCE field, not an opacity volume.** The
previous entry suggested it might be coverage or opacity. The chain refutes that.
The sample is divided by a width, squared and subtracted from one, so a large
sample yields zero coverage and a small sample yields full coverage — the algebra
of a distance. The same sample also limits the next step through
`FMin(stepCandidate, d)`, which is sphere tracing and only makes sense for a
distance. The SDF reading is therefore supported, not undermined.

**CORRECTION 2: "integral" was the wrong word.** The accumulation weight `%831` is
`uitofp i1`, a boolean widened to float, so it is 0 or 1 — a gate, not a step
length. Each qualifying sample adds its FULL coverage; non-qualifying samples add
nothing. It is a gated sum of per-sample coverages, not an integral over path
length, and it is not alpha compositing or a maximum either. The review was right
to withhold the word until the chain was read.

**The 0.5 threshold is footprint-relative and must NOT be transplanted.** The width
in the coverage term is built from the accumulated distance `t`: the code forms
`saturate(t*0.5 + 0.5)`, scales the cell size by ~1.0605, and combines these so the
normalising width GROWS with distance travelled. That is a cone footprint widening
along the ray, exactly the concern the review raised. A thin camera-to-light ray
has no such footprint, so 0.5 means something different there and would have to be
re-derived rather than copied.

**Why this is still the better basis than our refuted heuristic.** A single low
sample cannot decide anything: it contributes one coverage term that must compete
with the 0.5 budget. A grazing wall contributes a little, a thick wall contributes
repeatedly and crosses the threshold quickly, and a thin occluder lands in between
instead of being treated like a solid mountain. That is the graded behaviour the
minimum test could never produce.

**Still open.** What the gate `%830` tests, the bias `%661` (a small constant scale
of another value), the exact composition of the width, whether `t233` is the same
resource as the `t66` that PropagateSignedDistanceCS writes, and the field's world
scale and sign. None of these blocks designing our own accumulation, but all of
them block copying the engine's constants verbatim.

## Gate, bias and width resolved — 2026-09-09

Three of the four remaining unknowns are answered offline; the fourth needs the
live game.

**Gate.** `%830 = fcmp ogt float %773, 0.0`, widened to 0.0 or 1.0. A sample
contributes its coverage only when some per-iteration quantity is strictly
positive. What `%773` is has not been traced, so whether this rejects invalid
clipmap cells, samples outside the volume, back faces or exhausted distance is
unknown. For our own implementation the gate matters: it is likely the difference
between "usually works" and "robust".

**Bias.** `bias = 0.0002 * x^2`, the constant being float32 0.0002 and `x` a value
squared before scaling. It is POSITIVE, so `(d + bias)` makes the sampled distance
slightly larger, which thins geometry rather than inflating it — the direction the
outside review flagged as significant. The magnitude is small and grows
quadratically with whatever `x` is, which is untraced.

**Width, and it is definitively a cone.**

```
coneTerm = t * (k + 0.01)                              grows linearly with distance
cellTerm = cellSize * 1.0606600 * saturate(t*0.5 + 0.5)
width    = lerp(coneTerm, FMax(cellTerm, coneTerm), blend)
cellSize = level * (constant buffer value)
```

1.0606600 is 1.5 * sqrt(2)/2, a conservative cell radius. The width therefore never
falls below a cell-sized floor and otherwise opens linearly with distance
travelled. This settles the question raised earlier: the 0.5 threshold is defined
against a widening footprint and cannot be carried into a thin point-to-point ray
without re-derivation.

**t233 provenance is NOT resolvable offline.** Whether it is the resource
`PropagateSignedDistanceCS` writes at u5/space38, a different clipmap level, or a
separate field requires observing the live bindings or a frame capture. The shader
dumps give register slots, not resource identity.

**Recommendation: stop reverse-engineering here and start testing.** Enough is
known to build an offline prototype and calibrate it against ground truth rather
than against assumptions. The engine's own constants should NOT be copied: the
bias, the width and the 0.5 all belong to a widening screen-space cone, while our
query is a thin segment between two points. What transfers is the SHAPE of the
method — distance to coverage, gated accumulation, threshold, with the step limited
by the distance itself — and that shape is what should be calibrated.

Two variants are worth comparing on the same ground truth: a plain sphere-traced
hit test on `d < epsilon`, which is geometrically cleaner, against coverage
accumulation, which should degrade more gracefully at voxel resolution and on
grazing or thin geometry. The lantern case already provides one labelled example,
and the depth-buffer labeller would provide many.

## The gate resolves to a second volume, and three corrections — 2026-09-09

**The gate is a data dependency, not a geometric guard.**

```
u    = textureLoad(Texture3D<uint4> t224, space36)
gate = saturate((u.w >> 2) / 63) > 0
```

The scale constant is exactly 1/63, so the top six bits of the `w` component are
normalised to [0,1] and any non-zero value opens the gate. The other components are
unpacked as 4-bit nibbles (`and 15`, `lshr 4 and 15`), so `t224` is a packed volume
carrying several small fields per texel.

This changes the shape of the task: the engine's occlusion march depends on **two**
resources, the `Texture3D<float>` at t233 and this packed companion at t224. Our
own implementation would need the companion too, or a defensible substitute for
whatever presence it encodes. That was not visible before tracing the gate, and it
is the reason the outside review was right to insist on tracing it.

**Three corrections to the previous entries, all conceded.**

1. The bias reading — that a positive bias thins geometry — holds only if `d` is
   positive outside geometry. If the field is genuinely signed and goes negative
   inside, the statement is not uniformly true. The sign convention remains open
   and the earlier phrasing was too flat.

2. The constant 1.0606600 was described here as a conservative cell radius. More
   precisely it is 1.5 * sqrt(2)/2, and sqrt(2)/2 is the half-diagonal of a SQUARE,
   not of a cube, whose half-diagonal would be sqrt(3)/2 ~ 0.866. In a screen-space
   cone pass a 2D footprint is the natural reading, so this is probably pixel
   geometry rather than voxel geometry.

3. The claim that a single sample decides everything under plain sphere tracing was
   unfair to that method. Only a sample very close to the reconstructed surface
   triggers a hit. Sphere tracing's real risk is different: whether a discretised
   field represents thin geometry conservatively enough to be hit at all.

**A third variant is worth benchmarking.** Unweighted coverage summation couples to
the step strategy, because ten short steps near a surface contribute ten terms
where three long ones contribute three. The engine can afford that because its step
rule, cone width and 0.5 threshold were designed together; a reimplementation
cannot. So the comparison should be three-way:

```
A   sphere-traced hit,        d < epsilon
B   engine-shaped sum,        sum(coverage) >= T
C   length-normalised sum,    sum(coverage * stepLength) >= T
```

C is not what the engine does, but it is invariant to step strategy, which for an
arbitrary point-to-point ray is likely to matter more than fidelity to the engine.

**And t233 should not be assumed to be the destination.** If the resource history
shows it is produced by `GenerateHiZLevel0FromSDF_CS` rather than by
`PropagateSignedDistanceCS`, then it is a hierarchy built for cone tracing, and the
raw field behind it may suit a thin segment better. Both should be secured if both
exist.

## The gate is a bare presence flag, and t224 splits in two — 2026-09-09

A correction and a sharpening, both checkable and both checked.

**The gate carries no magnitude.** `saturate(x/63) > 0` is equivalent to
`(u.w >> 2) != 0` for a non-negative integer, so the division and the saturate are
dead weight for the boolean. Verified: the normalised value is referenced exactly
once in the whole shader, at the comparison itself. The previous entry's phrasing —
"normalised to [0,1]" — read a 0..63 occupancy scale into something that is only
ever tested against zero. Nothing here supports a graded occupancy in that field.

**But t224 splits into two roles.** While the `w` component is used only as a
presence flag, the nibbles unpacked from the other components ARE converted to
float and feed the computation, with twelve uses across the shader. So:

```
t224.w      presence flag only, tested != 0
t224.xyz    4-bit fields, decoded and used as data
```

What those nibbles mean is entirely open. That they are used numerically rather
than as flags is the strongest available hint that `t224` carries real per-voxel
attributes rather than a bitmask.

**Two further variants to benchmark**, both from the outside review and both
plausible enough to record before any is built:

```
D   traverse t224 directly, 3D-DDA over the crossed cells,
    a relevant cell on the segment means blocked
E   hybrid: t233 for large safe steps, t224 for the actual hit decision
```

D is attractive because it is independent of step size, needs no epsilon, no
coverage budget and no cone, and because whatever occupancy t224 encodes should
distinguish free cave air from a cave wall — precisely the distinction the
sky-visibility field could not make, which is what refuted the first method.

E takes from the engine only the parts that suit a point-to-point ray: the distance
field as an accelerator for traversal, the voxel attributes for the decision, and
leaves cone width, the 0.5 budget and the pixel footprint behind. On present
evidence E is the most likely final shape, but nothing here decides it.

**Both resources must be traced in PIX, not just t233**, each back to its producer.
If t224's producer carries a name suggesting voxelisation or occupancy injection,
it may sit upstream of the distance field, which would make it closer to the
original scene geometry than the SDF is.

## Validation protocol before any algorithm — 2026-09-09

A semantic caution and a protocol, recorded so the next step measures instead of
theorising.

**The caution.** `t224.w != 0` establishes that a cell has content relevant to this
raymarch. It does NOT establish that the cell contains blocking geometry. Plausible
meanings still include cell validity, presence of injected scene data, presence of
material information, or relevance to this GI path. The previous entry treated
presence and occupancy as interchangeable; they are not, and the difference decides
whether variants D and E are viable at all.

**The protocol, for t224 first.** Sample `w` at four world points of known
character before drawing any conclusion:

```
free air outdoors            expect 0 if w means occupancy
free air deep in an enclosure expect 0
inside a wall                 expect != 0
just outside a wall           expect 0
```

Only if that pattern holds may the field be described as occupancy or geometry
presence. Then decode the `xyz` nibbles at the same four points; if they turn out to
carry direction, normal, material or coverage information, that could matter for
grazing edges.

**The same for t233, by measurement rather than argument.** Read the value at
free air, one metre from a wall, ten centimetres from a wall, at the surface, and
inside the wall. That single table yields the world scale, the zero point, the sign
convention, whether the field is conservative, and its quantisation — all of which
have been open questions here for hours.

**The four world points already exist.** They do not need a new capture, only the
ability to read the new resources, because preserved captures with confirmed
ground truth already supply them: the visible lantern viewpoint is free air
outdoors, the under-roof camera is free air inside an enclosure, and the interior
and end of the occluded lantern segment give a point inside a wall and a point just
outside one. The user confirmed the occlusion in that case, so these are labelled
rather than assumed.

**Variant matrix, renamed.**

```
A  SDF surface hit
B  engine-inspired unweighted coverage      reference only
C  length-weighted coverage                 experimental LOS score
D  t224 voxel traversal
E  SDF-accelerated t224 traversal
```

B is now reference only, since the engine's arrangement is tuned to a screen-space
cone. If t224 does carry geometry occupancy, D and E answer the actual question —
is there geometry between these two world points — rather than the question the GI
raymarcher answers, which is how a widening pixel cone would be shaded.

**In a capture, do provenance only.** For both t233 and t224: resource, format,
dimensions, mips, last writer before the dispatch, that writer's name and its
inputs. Then one step further upstream if needed. The goal is the dependency graph
between them, not a pretty resource name, and specifically whether one is derived
from the other or they come from separate branches.

## Correction: two of the four validation points are not ground truth — 2026-09-09

The previous entry listed four world points of "known character" for validating a
new resource. Two of them do not survive scrutiny and the labels are withdrawn.

A blocked segment establishes that a blocker lies SOMEWHERE along it. It does not
establish that any particular interior sample is inside geometry. Worse, the only
instrument available for locating the blocker on that segment is the sky-visibility
field, which this document already refuted for exactly that question: near zero
there means "sees no sky", not "is solid". There is therefore no independent
confirmation of where the wall stood.

Revised labels:

```
free air outdoors             SOUND    the visible lantern viewpoint
free air inside an enclosure  SOUND    the under-roof camera
interior of the blocked ray   NOT ground truth, relabel as
                              "blocked-ray interior sample"
just outside a wall           NOT ground truth, same reason
```

So the validation table starts with two labelled points, both of them free air,
and no confirmed solid point at all. A new capture, or a visualisation that shows
where geometry actually sits, is needed before any "inside a wall" expectation can
be checked. Until then a candidate occupancy field can be tested for the two free
cases and for internal consistency, but not for its behaviour inside geometry.

## Capture checklist, provenance only — 2026-09-09

For the `RaymarchLocalLightsCS` dispatch, record for BOTH t233 and t224:

- the exact SRV view, not just the resource: format, dimensions, mip, first slice
  and array range, and any format reinterpretation. A `Texture3D<float>` in the
  shader may be a view onto a larger or differently organised resource, which
  matters given the 8 levels of 130 slices seen in the addressing.
- the last writer BEFORE this dispatch, not merely any historical writer. Clipmaps
  and temporally amortised updates have several writers, and only the one that
  produced the content this dispatch reads is informative.
- that writer's own inputs, then one step further upstream if needed.
- resource and heap identity rather than descriptor slot numbers, since aliasing
  and heap reuse make slots unreliable.

The objective is the relationship between the two resources. The valuable outcome
is a chain such as scene voxelisation to t224 or a precursor, then distance
generation to t233, because that would give the SDF-accelerated voxel traversal a
structural justification instead of merely an appealing shape. Finding that they
come from separate branches is equally informative and would weaken it.

No variant should be implemented before this provenance exists.

## Ground truth without a new session, and surface before interior — 2026-09-09

Two additions that close the gap left by withdrawing the two labels.

**A confirmed solid point can come from the capture itself.** Reconstructing a
world position from the depth buffer and the inverse view-projection depends on
neither t224, nor t233, nor the refuted sky-visibility field, so it breaks the
circularity that made the withdrawn labels unusable:

```
pick a pixel on a visible wall
read scene depth there
reconstruct the world position through InvViewProjection
optionally take the GBuffer normal
```

Better still, if the capture exposes the wall's actual geometry at the draw, take a
point from the triangle directly. Either way the label comes from the render, not
from the field under test.

**Prefer a SURFACE point over a deep interior one.** Whether the engine voxelises
the inside of a solid at all is unknown; a surface voxelisation would leave a point
deep in a wall empty, and reading that as a failure would condemn a correct field.
The informative set is therefore:

```
free air outdoors
free air indoors or under a roof
confirmed wall surface
just short of that surface
```

For a distance field that ordering should show a clear approach toward zero. For a
packed attribute volume it shows whether the surface voxel carries content at all.
Neither requires any claim about solid interiors.

**And the eventual bulk ground truth stays the depth buffer.** For sources on
screen, projecting a light position and comparing against scene depth yields
hundreds of visible/occluded labels without ever deriving truth from the volume
being tested. That remains the right way to compare variants A through E on error
rates rather than on single cases.

## Do not mix ground-truth coordinates across captures — 2026-09-09

This withdraws the remaining claim that validation points already exist, and it is
the most important caution recorded before opening a capture.

**The volumes are camera-centred and amortised, so a world point is only a valid
test point for the capture it came from.** A coordinate taken from today's lantern
session is not a valid probe into t224 or t233 as they appear in the 2026-09-05
capture: the clipmap is centred on that frame's camera, the point may fall outside
it entirely, and the stored content belongs to a different spatial and temporal
state. Both points previously kept as sound — the outdoor viewpoint and the
under-roof camera — are therefore unusable against that capture. They remain valid
for a LIVE readback taken in a session where the camera is actually there.

Rule: every validation point must come from the SAME frame whose volume is being
decoded. Mixing sessions during a careful validation is exactly how a wrong
conclusion gets manufactured.

For the capture at hand this means obtaining, from that frame only: free air, a
confirmed surface, a point just short of it, and an enclosed free-air point if the
frame happens to contain one.

**"Last writer" is too coarse for an amortised clipmap.** If writer A updates
z-slices 0..15, B updates 16..31 and C updates 32..47 before the dispatch, C is
formally the last writer while A and B produced parts of the volume actually read.
The question to ask is which writers modified the SUBRESOURCES OR REGIONS visible
through the SRV since the last full initialisation, so mip and slice-range updates
matter, not just the final line of a resource history.

**Shader LOD 0 is not necessarily resource mip 0.** `SampleLevel(..., 0)` addresses
mip 0 relative to the VIEW. With `MostDetailedMip = 2` it reads resource mip 2. So
record, for both resources: resource identity, SRV format, MostDetailedMip,
MipLevels, and FirstWSlice/WSize where they apply.

**Depth reconstruction has its own traps.** Reverse-Z, D3D NDC with z in 0..1, TAA
jitter in the projection, dynamic resolution and viewport scaling, and which depth
buffer that particular pass used. Taking a point from the draw's geometry directly
is cleaner where the capture exposes it.

## Same frame is not the same as fresh, and one frame is not the whole history

Two final precisions before opening a capture.

**Spatial validity and temporal freshness are different properties.** "From the
same frame" makes a point spatially valid for that frame's clipmap. It does not
make the sampled content current, because the volume is amortised. This is not
speculation here: it was measured. About a quarter of the voxels change between
reads 1.5 s apart, updates rotate through blocks of 16 z-slices, and in every
observed step at least one block was byte-identical. A validation point can
therefore be correctly placed and still hold content several cycles old.

```
spatially valid    the point lies inside this frame's volume
temporally fresh   its region was updated for the current clipmap state
```

Where the capture reveals which slabs were written in the frame, a ground-truth
point inside a freshly updated region is worth considerably more than one outside.

**A single frame may not contain the whole writer history.** For a persistent,
amortised resource, part of the content can have been written before the capture
began and merely carried forward. So the goal of "all writers since the last full
initialisation" may be unreachable from one capture. Record what is visible and do
not overstate it:

```
record:  every writer visible in this frame, the regions each touches,
         and the last visible writer before the dispatch
do not:  conclude that the provenance of the current content is fully known
```

Completing the chain may need the shader graph plus a later multi-frame or live
observation.

**Take three points from one confirmed surface, not one.** Given a surface point P
with a reliably oriented normal:

```
Pfree    = P + normal * eps      confirmed free air immediately in front
Psurface = P                     the confirmed surface
Pback    = P - normal * eps      just on the other side
```

`Pback` must be labelled "behind surface" and never "solid interior", because how
the voxelisation treats the inside of a volume is still unknown. Three points from
the same real wall in the same frame say far more than a single wall point: for a
distance field one expects a clear minimum at the surface, and for the packed
volume the interesting question is simply which of the three carries content.

## Resources identified from the capture export — 2026-09-09

Codex ran `pixtool open-capture <wpix> export-to-cpp`, producing a full C++
reconstruction under `artifacts/light-research/pix-provenance-20260909/cpp`. The
resource identities fall straight out of it.

| ApiObjectId | dimensions | resource format | SRV format | role |
|---|---|---|---|---|
| 190 | 64 x 32 x 264 | R8_TYPELESS | R8_UNORM | the sky-visibility volume already copied |
| **191** | **128 x 64 x 1040** | R16_TYPELESS | **R16_FLOAT** | the raymarch volume, t233 |
| **211** | **64 x 32 x 512** | R8G8B8A8_TYPELESS | **R8G8B8A8_UINT** | the packed companion, t224 |

All three are `TEXTURE3D` with `ALLOW_UNORDERED_ACCESS`, and all three SRVs are
created with MostDetailedMip 0 and MipLevels 1, so shader LOD 0 is resource mip 0
and the MostDetailedMip trap does not apply anywhere here.

**The 1040 depth confirms the shader derivation independently.** The addressing in
`RaymarchLocalLightsCS` was read as 8 levels of 130 slices, giving 1040, before any
resource was inspected. The actual resource is 1040 deep. Likewise 190's 264 is the
4 x 66 already established for the sky-visibility volume.

**`Texture3D<uint4>` is confirmed as four 8-bit channels**, which is what the
nibble unpacking in the shader operates on.

**A structural observation that matters for the variant choice.** The two volumes
are NOT the same shape:

```
191  128 x 64 x 1040     8 levels x 130 slices, 128 usable plus 2 border
211   64 x 32 x  512     8 levels x  64 slices, no border
```

The companion is half the resolution of the distance field on every axis. So if
t224 does carry geometry occupancy, traversing it directly — variant D — would be
COARSER than the distance field it accompanies, not finer. That weakens D relative
to the hybrid E, where the fine field drives traversal and the coarse one only
qualifies the decision. It also means a thin wall is more likely to be missed by
t224 than by t233.

**Incidental but significant: the game builds raytracing acceleration structures.**
The export contains `AccelStructureRecreation` files, so DXR is in use. Inline
`RayQuery` therefore remains technically available as an exact alternative, which
had been listed earlier as unknown.

**Still open.** Which shader writes 191 and 211. Both are written through UAVs, and
identifying the producers requires correlating descriptor heap slots with the root
tables set before each dispatch in the recorded command lists. That is the next
step and it is mechanical rather than uncertain.
