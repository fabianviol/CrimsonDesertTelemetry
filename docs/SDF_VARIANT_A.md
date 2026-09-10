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

Measure the corrected observer's complete read-release tuple
on a supported, progressing game process. Preserve that line with PID/start time,
installed ASI hash, INI, native log and progressing telemetry controls. Layout alone
does not settle enhanced-barrier access/synchronization. This run copies no R16.
In particular, `SyncAfter=NONE` / `AccessAfter=NO_ACCESS` forbids later access or
barriers in the same ExecuteCommandLists scope. If that is the observed release,
copy **before** forwarding it (round-trip the original pre-release state), not
after it merely because GENERIC_READ supports copies. See Microsoft's
[Enhanced Barriers specification](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html).

Then implement one bounded fenced R16 copy at the observed barrier, with exact
resource/tuple/subresource and command-list generation guards, recorded copy time,
and usable GI/camera context. Preserve the existing R8 results. Store completed
R16 evidence immediately, rather than relying on process-detach report writing.

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
