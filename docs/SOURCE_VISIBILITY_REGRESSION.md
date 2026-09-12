# Live source-visibility regressions — 2026-09-12

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
