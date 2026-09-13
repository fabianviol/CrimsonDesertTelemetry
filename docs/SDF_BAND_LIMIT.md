# The distance field is a narrow band, not a solid interior — 2026-09-14

This explains the per-light false clears in both failing locations, and it rules
out an entire class of attempted fixes. Everything below is measured from the two
preserved CPU volumes; no game was running and no capture was taken.

## What was measured

Probing straight down from the capture camera, through ground the player was
standing on, at clipmap level 0 throughout (verified: the clamp stays 0.5303 the
whole way, and only changes to 1.0607 at 12 gu when the point leaves level 0's
window):

| volume | negative band | deepest value | immediately below |
|---|---|---|---|
| room 1970 | drop 3.80 – 4.40, about 0.6 gu | **−0.18086** | **+0.53027** |
| camp 1853 | drop 2.70 – 3.00, about 0.3 gu | **−0.09241** | **+0.53027** |

`+0.53027` is exactly `1.5 * sqrt(2) * 0.25`, the positive clamp at level 0. From
0.2 to 3.6 gu *below the floor surface* the field reports the maximum possible
distance from any surface.

## What that means

**"Inside" is not a persistent state in this field.** It is a thin signed band
around surfaces, roughly 0.3–0.6 gu wide here — one to two cells. Beyond the band,
interior and exterior are indistinguishable: both saturate at `+clamp`.

A segment therefore registers a negative value only if it crosses that band in a
way trilinear interpolation preserves. It frequently does not:

- In the room, exhaustive piecewise-cubic evaluation of the complete camera-to-source
  path found **no nonpositive crossing at all**, while texels marked solid
  (−0.020, −0.036) sat within one cell of the path. The ray threaded between them.
- In the camp, `far-box-a/b` and `left-source` stay positive (+0.038, +0.037,
  +0.015) even under 0.01-gu dense sampling.

**Consequence: "is the interpolated value <= 0 somewhere along this segment" cannot
be made into a reliable occlusion test on this field.** Not by smaller steps, not by
denser sampling, not by a larger hit tolerance. The information is not in the data
along that ray.

This is a limitation of the query, not a defect in the tracing, the addressing or
the acquisition. All three were verified independently: offline traces reproduce the
native published `closestApproach` values exactly, and the geometry is present in
the field as negative texels.

## What is still not distinguishable

Whether the band is a property of the **field** (a narrow-band SDF built for cone
tracing, which is the usual design) or of the **world geometry** (thin shells rather
than solids) cannot be told from these probes. Both produce this signature. The
operational consequence is identical either way, which is why this document does not
depend on the answer.

## What this does and does not license

**Still correct and still needed:** the forced 0.05-gu minimum step is a real defect.
It skipped a genuine negative interval on `near-box-b`, where dense sampling finds
−0.0073 at t = 13.25 while the traced verdict was clear. A sphere trace is only valid
when it steps by at most the sampled distance; a floor breaks that guarantee exactly
where it matters. Fixing it recovers that case. It does **not** recover the others.

**Ruled out:** raising the hit tolerance. Across 13 labelled paths in two locations
the suspected-blocked minima are ≤ 0.039 and the believed-clear minima are ≥ 0.1245,
so a cut between them would appear to work. Do not install it. It is fitted to the
labels of one room and one camp, it has no basis in the field's semantics, and a
genuinely clear path passing close to a wall would be misclassified. `SOURCE_VISIBILITY
_REGRESSION.md` already reached this conclusion; this document explains why it is
right.

**Ruled out for the same reason:** counting solid texels near the path. Tested and
refuted on the controlled same-source A/B — the blocked pose touched 4 solid texels
and the three exposed poses touched 3. In a room with walls and a floor there is
always something nearby. What differed was the distance to it (0.125 versus 0.41),
which is the path minimum again, and therefore the tolerance idea again.

## The one query this field can answer

A distance field answers *"how far is the nearest surface"* reliably inside its band.
So a **swept** test is well posed where a sign test is not: "does any geometry come
within radius r of this segment" is answerable, because `value < r` means exactly
that, with no reliance on the sign surviving interpolation.

That is a different question from "is the segment blocked", and choosing r is a
physical decision — source size, penumbra, how much grazing contact should count as
occlusion — not a fit to labels. It is also inherently conservative: it will call a
light blocked when its path merely grazes geometry. For a lighting product that bias
is arguably correct, but it must be stated rather than discovered by a user.

This is a proposal, not a result. It has not been implemented or tested live.

## Reproducing this

Read-only, from preserved evidence:

- `artifacts/light-research/source-visibility-focused-20260912/room-four-lights-01/current-sdf-02`
- `artifacts/light-research/source-visibility-regression-20260912/current-sdf-01`

with `scripts/Decode-SignedDistance.py` for sampling and
`scripts/Trace-SignedDistance.py` for level selection. Walk down from
`sdf.json`'s `camera`, print value and level together, and watch for the return to
`+clamp` below the surface.
