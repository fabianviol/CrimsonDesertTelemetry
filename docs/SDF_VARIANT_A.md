# t233 Variant A — bounded live test, 2026-09-10

Product question: can the fine distance volume alone distinguish the same torch
visible, occluded by a wall, then visible again from the camera? Ambient is parked.
Do not investigate t224 unless A fails on a valid concrete case. Do not acquire a
new PIX capture unless a named mapping or acquisition failure cannot be resolved
from the existing evidence. A failed acquisition is not a negative result for A.

## ESTABLISHED

- The time-accurate captured binding resolves t233/space36 to Resource 191:
  128 x 64 x 1040 R16_TYPELESS, viewed as R16_FLOAT. See
  [GPU_CAPTURE_FORENSICS.md](GPU_CAPTURE_FORENSICS.md), sections 3 and 4b.
- The existing live observer has seen a resource with this shape at a
  SHADER_RESOURCE (6) to GENERIC_READ (1) barrier. This is shape-based live
  discovery, not a new live proof of the t233 descriptor binding.
- No fenced live R16 payload or calibrated camera-to-torch trace is documented.
  The current readback and decoder still accept the R8 sky texture only.
- The pending `sdf-probe.3` ZIP matches its recorded SHA256
  `798FD897D7B2011203275AAE215A897E9A5E385F3BC5FF7FEE5BBA144C06C648`.
  Its ASI SHA256 is
  `820B8B7AD672DC2C8BB31E038D437D52603A05F2E75B6E6580656D295211C9BD`.
- The preserved archive `RaymarchLocalLightsCS` listing supplies a mapping
  candidate without a new capture. At lines 1048-1102 it tests levels **0..7**
  using continuous half-open bounds, radii (63,31,63), GI rows 20+level and
  36+level, and row 19 plus the view-relative ray point. At lines 1111-1135,
  normalized coordinates are `(GI[1].xyz * relativePoint + GI[46].xyz) / 2^level`.
  Lines 1239-1251 form `(130*level + 1 + 128*frac(z))/1040` and sample t233.
  Source: `artifacts/light-research/filtered-count-1322f152-20260906-2111-52c33a4a.ll`.
  This is archive arithmetic, not proof that the current frame dispatched it.

## INFERENCE

The shape-matched live resource is the captured t233 volume. Its shader use as a
step distance supports Variant A, but does not establish the live sign, distance
unit, accuracy, or preservation of a particular wall. Neither the engine's cone
coverage threshold nor the sky decoder's level restrictions are a calibrated
point-to-point hit rule. The R8 `select_clipmap` helper cannot be reused unchanged:
it starts at level 1 and floors the tested cell, unlike the archive raymarcher.

## ESTABLISHED — probe.3 live gate exposed an observer failure

Supported PID15044, start 22:32:58 CEST; verified package ASI; progressing API
controls. The recorded first sighting was `layout 1->3 access 0->10 sync 1->80`
(bitfields hex), resource `12BE75D10`, list `22864FEA0`: entry to UAV writing.
The first-sighting-only observer then discarded subsequent tuples for that same
resource, so this run cannot report the required read release. Evidence:
`artifacts/light-research/sdf-access-pid15044-20260910-223544/`. This invalidates
the planned observer's sufficiency, not Variant A.

## OPEN — one acquisition gate first

The corrected observer measured the complete compute read-release tuple in
supported PID1668: layout 6->1, access 0x80->0x80000000, sync 0x80->0, flags zero,
subresources (UINT_MAX,0,0,0,0,0). The preserved evidence is
`artifacts/light-research/sdf-transitions-pid1668-20260910-224833/`, with hashes,
INI, native log and progressing telemetry controls. That run copied no R16.
In particular, `SyncAfter=NONE` / `AccessAfter=NO_ACCESS` forbids later access or
barriers in the same ExecuteCommandLists scope. If that is the observed release,
copy **before** forwarding it (round-trip the original pre-release state), not
after it merely because GENERIC_READ supports copies. See Microsoft's
[Enhanced Barriers specification](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html).

The bounded R16 copy is now implemented with exact shape/tuple/subresource,
known command-list generation, Close/Submit and queue-fence guards. Its completed
binary and metadata are saved immediately. CPU observations before/after recording
are preserved; their stable mapping to the copied volume remains to be checked.
This is NOT GPU-paired GI and must not be presented as such. The next fact needed
is the first successful live R16 acquisition plus that mapping check. No decoder
or trace has yet been added. See HANDOVER.md for the package and continuation.

## Value semantics of t233, calibrated from the first live payload — 2026-09-10

The first fenced live R16 copy landed at 23:28 in PID 33348, four transactions of
17,039,360 bytes each (128 x 64 x 1040 x 2 exactly), each under its own fence.
Evidence preserved at `artifacts/light-research/sdf-live-pid33348-20260910-2328/`:

```
signed-distance-33348-15545828-1.bin   17039360 bytes  SHA256 BB1396A6E7298A1F...
signed-distance-33348-15545828-1.json     22684 bytes  SHA256 445F316A5529A0EC...
```

The payload's own metadata carries the 768-byte GI constants, so the value semantics
could be settled from the acquisition itself without a further capture.

### Established

**The stored R16_FLOAT is a signed world distance in game units.** Not normalised,
not level-local.

**Negative values exist and concentrate on the fine levels**: 192 in a sparse sample
of level 0, none at all in the two coarsest. That the negative side is the INSIDE is
the standard convention and the obvious reading, but it is NOT yet measured — no
negative texel has been correlated with known geometry. Confirming it is what the
Pfree / Psurface / Pback points are for.

**Cell size per level comes straight from the constants.** `float[0] = 0.25`, and the
per-level scale `w` at float index `80 + 4L` runs 4, 2, 1, 0.5, 0.25, 0.125, 0.0625,
0.03125 — that is `w = 1 / cellSize(L)` with

```
cellSize(L) = 0.25 * 2^L        level 0 finest at 0.25 gu, level 7 coarsest at 32 gu
```

This replaces the archive-derived `cellSize = 0.25 * 2^level` with a reading from the
live data.

**The value is clamped per level, at exactly 1.5*sqrt(2) cell sizes.** Measured maxima
against the constants:

| level | max value | cell size | ratio |
|---|---|---|---|
| 0 | 0.5303 | 0.25 | 2.1213 |
| 1 | 1.0605 | 0.5 | 2.1213 |
| 2 | 2.1211 | 1.0 | 2.1213 |
| 3 | 4.2422 | 2.0 | 2.1213 |
| 4 | 8.4844 | 4.0 | 2.1213 |
| 5 | 16.9688 | 8.0 | 2.1213 |
| 6 | 33.9375 | 16.0 | 2.1213 |
| 7 | 67.8750 | 32.0 | 2.1213 |

`1.5 * sqrt(2) = 2.1213`. The same ratio on all eight levels, so the ladder is the
clamp rule and not a coincidence of this scene.

Every sampled texel of levels 6 and 7 sits at the positive clamp. The tempting reading
is "no geometry within their reach", but in an amortised clipmap that needs freshness
and validity evidence this payload does not carry, so it stays a description of the
values and not a claim about the world.

### What this means for Variant A

**The field is short range.** At the finest level it knows about geometry only within
0.53 gu, and the coarse levels whose cells are 16 to 32 gu across will not resolve a
wall.

The step count is not alarming, and an earlier note calling it "iteration-heavy"
overstated it: a 10 gu ray held at the finest level costs about twenty steps, and
choosing the level along the ray costs fewer. For the few dozen lights Hue cares about
that is cheap. Fix the iteration bound from these numbers rather than a guess, before
looking at the occluded phase.

**Sphere tracing is correct only IF the stored positive value never overestimates the
distance to the nearest surface.** Under a clamp, `min(trueDistance, clamp)` stays
conservative — but the non-overestimating property has not been demonstrated for this
voxelised, propagated field. Treat it as the working hypothesis Variant A rests on,
not as a theorem, and let the calibration points test it.

### Still open

The world-to-voxel mapping including the border texels, verified against a known
surface; that a particular wall is preserved in the copy; and any trace at all. The
sign and the unit are settled; the addressing is not.

## Controlled test and stopping rule

1. Calibrate the copied field at free air and across one known wall, recording
   world positions, raw half values, selected level and interpolation neighbors.
   Confirm world mapping, border handling, sign and distance scale. Missing
   coverage or invalid/stale context must remain unknown.
2. Before examining the occluded phase, fix the sphere-trace hit tolerance,
   distance scale, step safety factor, iteration bound and endpoint policy from
   that calibration. Retain the trace samples so overshoot and endpoint self-hits
   can be distinguished from real wall hits; do not tune against the answer.
3. Use one stationary, individually identifiable torch. Record its world position
   and the camera with each snapshot; do not identify it by rendered sample index.
   The user labels V1 (clear), O (the same wall blocks it), V2 (clear again).
   Hold each pose for at least three completed fresh copies with progressing frame
   controls. Require an unambiguous source association throughout; disappearance
   from ManyLights is not an occluded result.
4. A is demonstrated **for this controlled case** if all accepted V1/V2 traces
   reach the target and all accepted O traces hit the intervening wall using the
   same fixed parameters. This does not establish reliability for all materials,
   thin geometry or moving occluders. Stop without adding t224 or a public API.
5. A is falsified for the concrete case if, after valid mapping and acquisition,
   a repeatable classification contradicts the visual label. Retain the failure
   path and stop. Acquisition, source-identity, freshness, coverage or calibration
   failures leave the product question OPEN; they do not falsify the field.
