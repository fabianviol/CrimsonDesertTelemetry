# Live source-visibility regressions — 2026-09-12

## Current `.4` room test: fresh paired field still reports four clear sources

User installed `.4` via DMM, PID 34380, started 16:12:57 CEST. The ASI, code DLLs
and INI match the candidate; DMM retained older deps.cfg version labels with the
same runtime target and DLL asset names. Full package equality is not claimed.

The user reports two candles and two fires in the room screenshot, two visible
and two blocked, while all four labels say SOURCE VISIBLE. The user's subsequent
identification is the first and third physical markers from the left: the top
11.9-gu source and bottom 3.6-gu source are blocked. The hanging point light at
3.0 gu and right spot at 4.2 gu are visible controls. Neither GPU slots nor
point/spot kind provides a candle/fire classification.

Evidence is under
`artifacts/light-research/source-visibility-focused-20260912/room-four-lights-01/`.
`user-view.png` preserves the screenshot. The stationary 12-second raw stream has
720/720 available messages, 168 distinct light captures and 24 SDF volumes
(1268–1291); raw age is 0–110 ms. Player position is unchanged. Each of the four
nearest matching source positions reports CLEAR in every message. This measured
window has no shared light-feed dropout, but does not establish long-run continuity.

`current-sdf-02/` retains a bounded read-only copy of the exact ASI's immutable
published CPU snapshot: volume 1970, context 55044, SHA256
`A752E85C95E2F24F3E0EC4ABEC42427F388073D44BFBDE6BA6A7C46C46E739C6`.
The owner/header/vector are unchanged around the copy. `api-matched.json` references
that exact volume and frame in light capture 15359, API sequence 58612: raw age
78 ms, SDF age at capture 109 ms. The source reference camera is
`(-10536.7568359375,612.9241333007812,-4417.41748046875)`.
Actual GI row 46 equals stored camera times inverse extents exactly. The derived
scene fixture contains only stored frame/camera, not original GPU scene bytes.

The first retained CPU copy, volume 1264, is NOT paired to its API before/after
(volume 1263), and the subsequently started stream begins at 1268. Do not use it
as an exact live trace comparison. The second copy explicitly waits for matching
API metadata and is the one used below.

| Screenshot label | Target XYZ in the matched capture | Live closest / offline closest | Complete interpolated path minimum |
| --- | --- | --- | --- |
| Top, 11.9 gu | -10520.249, 610.1467, -4423.202 | .012111214 / .012111214 | .005258648 |
| Left, 3.0 gu | -10529.752, 611.3594, -4420.2944 | .530273438 / .530273438 | .160335741 |
| Right, 4.2 gu | -10531.063, 610.22504, -4424.199 | .517017484 / .517017470 | .124548416 |
| Bottom, 3.6 gu | -10528.658, 610.2924, -4421.731 | .019646818 / .019646817 | .016471236 |

HUD distances are from the player; traces originate at the paired camera. Source
positions, converted back to float32, reproduce the native trace's published float
closest values. `four-target-profile.json` also retains dense 0.005-gu profiles.
`piecewise-cubic-minima.json` goes further: it splits the entire camera-to-source
segment at texel centres, clipmap bounds and Z wraps, then evaluates each cubic
interpolant's endpoint and derivative-root minima. Four additional evaluations per
interval differ from the polynomial by at most 2.01e-12. These complete paths,
including the normally omitted start/end margins, have no nonpositive crossing.

Thus this current field reproduces the incorrect clear classification upstream of
the HUD. The previously proven forced-step skip is still real, but does not explain
this particular fresh four-source snapshot. This is a statement about the sampled
field, not proof that the world is clear or that a particular wall is missing.
The next controlled comparison follows the first/top blocked source. No hit
threshold, endpoint or production implementation was changed to fit
these labels. The user's later sky-HUD wording/orientation complaint is queued after
the source test at their explicit request. Existing `.4` AV results are unchanged;
no new binary was built or installed for these read-only measurements.

### Same first source exposed by the user

The user moved until the first/top source was visible and confirmed `frei`.
`room-first-source-visible-01/` retains three fresh, immutable CPU volumes with
matching API metadata and an eight-second stream (481 raw/480 smoothed messages).
The tracked source stays near `(-10520.24,610.15,-4423.20)`. Camera X/Z is fixed
at `(-10536.15625,-4415.99169921875)`, Y 612.6862–612.6961 in the matching samples;
camera forward is `(0.81005996,-0.20203367,-0.550441)`.

| Volume / context frame | Live closest | Offline closest | Verdict |
| --- | --- | --- | --- |
| 3096 / 19932 | .489167005 | .489167016 | CLEAR |
| 3098 / 19988 | .492633104 | .492633097 | CLEAR |
| 3100 / 20044 | .493894756 | .493894747 | CLEAR |

`first-source-comparison.json` preserves camera, target, light capture, freshness,
complete marcher samples and dense minima alongside the original blocked pose.
This establishes a clear difference in the field at the two labelled poses, while
the production zero-crossing rule incorrectly gives the same verdict. It does not
justify fitting a threshold between their values. The user has been asked to return
to a blocked pose of the same source; confirmation and the return capture are pending.

The first blocked ray crosses a cell whose centre is negative (-.0181884766), but
its trilinear path stays positive. The local 5x5x5 neighbourhood also contains
negative samples. Thus there is actual negative field content near the failure;
these data do not establish that the entire crossed cell is solid. A diagnostic
nearest-cell control separates this first blocked pose from its three free poses,
but misses the second/bottom blocked source. It is not promoted to production:
cell occupancy and grazing behaviour cannot be inferred from a single centre.
The old uncorrected sampler also leaves both initially blocked sources CLEAR,
so reverting the half-texel correction is not supported by this case.

The user explicitly requires the camera position and orientation as reference.
Each replay verifies that source metadata's reference position is the paired raw
camera position. Camera orientation governs projection; the geometric ray points
from that camera to the source, including sources outside the view. HUD distances
in the original screenshot are still player distances, not ray lengths.

## Previous `.3` camp failure

The user's camp test of production `v2.1.10-source-visibility.3` failed.
The screenshot shows opaque boxes/structures between the camera and several
lamp markers labelled SOURCE VISIBLE; the user excludes the visibly open fire
from that report. F11 cannot correctly hide a source incorrectly classified as
clear. This is not accepted per-light geometry behavior, regardless of the earlier
fireplace research or synthetic tests. Ambient's previous separate pass stands.

## Simultaneous light-view disappearance

Read-only 20-second raw/smoothed/ambient capture, PID 5604, 15:27:17–15:27:37 CEST:

- 1,197 messages, all `game.state=playing`, sequence 34,172–35,369.
- 1,179 available raw samples, 309 distinct light captures, each 84 source records,
  raw age 0–109 ms. Camera X/Z remained fixed; Y varied only 0.01423 game units.
- **18 raw and 18 smoothed messages were unavailable with `bridge-changing`.**
  Their neighboring samples were healthy. Both light views share the requirement
  for an available raw capture, explaining the simultaneous marker/radar dropouts
  while the rest of the HUD remained visible.

The native producer held its inter-process write sequence odd throughout up to
256 SDF traces. The managed reader's three immediate attempts could all overlap
that computation. Metadata preparation now runs before the short publication
window, under the existing writer lock. One bounded temporary metadata array is
copied together with its matching raw capture. There is no retained source history,
new freshness threshold, or visibility-only filtering of the API.

The managed reader also reuses its already-existing single complete byte-buffer
cache if all three read attempts race a writer. It decodes that unchanged capture
at the current clock; the original capture timestamp and 500-ms age limit still
apply. A cold reader remains unavailable, and a completed native fault replaces
the cache immediately. This covers the remaining brief fixed-size copy window
without adding source history or treating partial data as valid.

A deterministic native regression pauses geometry preparation while another
thread reads the complete previous publication three times; it then verifies the
new raw/metadata pair and an allocation-failure path. The test callback compiles
only into its test target. Live verification of the rebuilt ASI remains required.
Managed race controls additionally cover partial changed RGB/geometry, cold reads,
completed updates/faults and expiry during a prolonged write. All 69 managed tests
pass. An initial fixture incorrectly paired a clear code with a negative distance;
the validator correctly rejected it, and the fixture was corrected.

## Geometric false clears

The saved screenshot and stationary capture identify distinct target positions,
not durable GPU slot identities. The far box pair near
`(-10514.04,610.42,-4387.03)` consistently reported clear, with small positive
sampled minima, across multiple progressing volume captures. The nearby pair
near `(-10514.71,610.94,-4374.30)` gave conflicting and changing verdicts despite
being only centimeters apart. Left-hand lamp contributions also remained clear.

[Normalized sampling correction](SDF_SAMPLING_CORRECTION.md) establishes a separate
half-texel addressing defect from the preserved shader and sampler descriptor.
Correcting it preserves all ten retained historical ABA verdicts but **does not
resolve all reported camp lamps**. No hit tolerance or endpoint was tuned to the
screenshot labels.

An external read of the exact running ASI's immutable CPU snapshot saved volume
1853, context frame 53808, at camera
`(-10515.209961,613.077454,-4358.502930)`. The read inspected only 20,480 bytes of
the ASI's writable sections and validated its own snapshot/vector layout and
unchanged owner/header/vector across the bounded copy. It added no hooks or game
writes. This volume was already stale at read time: it is usable for offline
analysis but **not fresh geometric live acceptance**. Its derived scene bytes
contain only stored camera/frame, not an original GPU scene capture.

A 0.01-gu offline profile through this field confirms two distinct limitations:
the corrected near-pair trace can skip a narrow negative interval because of its
forced 0.05-gu step; several far/left target paths remain positive even under dense
sampling. Increasing a hit threshold cannot establish a geometric solution.

## Acquisition stopped updating

At 15:38:32 CEST the source API still referenced volume 1853, already 228,141 ms
old. At 15:43:02–04, three read-only inspections of the ASI's own acquisition state
found `Ready`, no failure, and progressing valid GI frames/timestamps. Its copy
counter, old command-list generation, last completion and release timestamps were
unchanged. Thus this was not a recorded GPU timeout or `Failed` state. The current
implementation pins its initial resource and command list indefinitely; which
exact match stopped occurring in this run is not stored and is not proven.

The corrected acquisition can select a replacement only after the last copy is
stale and its GPU fence has completed, in Ready with no pending CPU/GPU work.
The candidate must be a unique exact R16 release on the same verified device.
The existing preparation path replaces completed resources; a newly observed
successful Reset is mandatory before copying from the replacement list. Pending
and Failed transactions remain protected. The normal known binding still works
when the bounded discovery budget is exhausted; duplicate/malformed barrier
packets remain rejected. No new hooks, history, logging or general retry system
were added.

The production-acquisition WARP target passes 691 controls, including actual
list/source replacement copies and incomplete GPU work, with no D3D12 debug
warnings/errors. Independent review caught and corrected two candidate-scan
regressions before packaging: the known binding must not consume the replacement
budget, and every occurrence of a candidate resource must be checked. A test
fixture initially assumed two WARP device creations have different identities;
the runtime reuses that identity, so the test now explicitly supplies a mismatched
verified identity. None of these synthetic controls proves the original live
match change or a live recovery yet.

The user subsequently confirmed there was no interruption or menu operation;
they had simply left the PC. The stall therefore occurred during unattended
normal play. Neither `playing` nor ongoing GI updates becomes a validated general
menu indicator because of this confirmation.

## Evidence and next check

All raw files stay under
`artifacts/light-research/source-visibility-regression-20260912/`:
`stationary-01/`, `reported-occluded-lamps.png`, `current-sdf-01/`,
`current-sdf-state-01.json`, and the explicitly bounded read/analysis scripts.
The installed ASI hash matched `76B0D0B1DAFA15F233D94DAD92BE11FE4FD01DE2DDB20162423F314932E9605D`.

The `.4` OFF test package is built and scanned: ASI 5/71, ZIP 0/67, with the same
five detecting engines as `.3`. It must be installed by the user through DMM
and checked for continuous raw publication and recurring
SDF updates before the same lamp is tested visible/blocked/visible. All-functions
acceptance, per-light acceptance, and final AV clearance remain open.
