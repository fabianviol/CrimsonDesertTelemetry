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
