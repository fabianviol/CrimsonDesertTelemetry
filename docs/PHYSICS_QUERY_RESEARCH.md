# Physics-first occlusion investigation — 2026-09-25, Codex

**Current instrument: physics.2**, described in the final section. physics.1
below is the preserved observation-only baseline, not the new replay behavior.

Owner priority: resolve geometric light occlusion before the new light-control
idea. Reuse World Builder's native query knowledge, not another SDF threshold.
The SDF false-clear limitation remains measured in `SDF_BAND_LIMIT.md`.

## What is established

World Builder `asi/cdmodkit/cdmodkit.cpp`, pinned
`ee1f05a3ad1a61cd4aee66946155d0315fdc14e7`, captures a natural player-near
`hknpSphereShape` query and replays a copy for downward ground placement. Its
selection and layout are reused under MIT; notice in `licenses/WorldBuilder-MIT.txt`.
See `WORLD_BUILDER_RESEARCH.md` for the broader audit. This is NOT an existing
optical-visibility API. Collision masks, sphere radius, player self-hits, glass,
foliage and render-only geometry can all differ from optical visibility.

Offline anchors: patch **2.03.02**, EXE **1.0.0.2976**, build 25477059,
SHA256 `57DA440D72F4DB974F25FEF047CF84C4DADD999A88CB2A3C5AF4C9BD67FDE1E7`.
Saved EXE: `artifacts/recovery/20260923-191056-build-25477059/CrimsonDesert.exe`.

- WorldCastShape RVA `0x42B0C50`; verified five pointer/integer arguments:
  world, query, transform, collector, extra. Hook forwards them and preserves RAX.
- Collector vtable `0x5D13528`; slot 5 `0x39DBE20`, 12-slot shape also guarded.
- Shape RTTI `.?AVhknpSphereShape@@`, independently found at file offset `0x6A90040`.
- Query: shape pointer +0x28, start float3 +0x30; collector: inline buffer +0x20
  points to collector+0x30, count +0x0C, double fraction +0x10, normal float3 +0x80.
- Query/template sizes come from WB, not independently proven object extents.
  All snapshots are bounded safe reads; failed reads reject the candidate.
- Player-near selection uses truncation toward zero of X/Z /1000, a horizontal
  radius of 3 and vertical difference <=3. The live observation must confirm
  this convention for the current context; a timeout does not prove no query.

Evidence: `artifacts/light-research/worldbuilder-anchors-25477059-20260924-v2.json`
and `worldbuilder-profiler-25477059-20260924.json`.

## First instrument: observation only

`physics_probe.cpp` is compiled ONLY in research builds. `[Research] PhysicsProbe=0`
by default. The private test package enables it while disabling Ambient,
SourceVisibility and spatial readback so no unrelated spatial instrument runs.
Player/camera, ManyLights and both light views remain available.

After the shared exact-EXE check, an additional fixed diagnostic hash, entry bytes
and collector-vtable guard must match before installing one MinHook detour.
There is NO movement-tick hook, cloned replay, additional physics call or
visibility API mutation in this version. A previously drafted replay was removed
before build: its lifetime/stack-pointer assumptions need live evidence first.

Run `scripts/Start-PhysicsProbe.ps1` with the player in a loaded world. It sends
one atomic file request, bound to PID/start time and fresh telemetry, then returns
immediately. No waiting for owner input. The observer examines at most 512 calls
over 2 seconds, claims one matching natural query, copies its inputs and collector
output in the original call, and writes the report off-thread. In idle state it
only forwards the original call. No allocation or logging in the detour.

Output beside the ASI: `physics-probe-PID-REQUESTID.json`. Contains original
addresses, caller, thread/stack limits, world context, before/after bytes, raw
count/fraction/normal, and exact build/hash. Original calls and arguments are
unchanged. Timeout/rejection is unknown, never a clear-path verdict. Raw result
fields have NOT yet been promoted to validated collision semantics.

Host tests cover negative-tile selection, bounds, exact argument/return
forwarding, snapshots, invalid candidates, timeout and concurrent one-shot claim.
They do not validate the actual game's ABI, collision layers or optical behavior.

## Next steps, strictly in order

1. Read one successful natural query report. If there is no match, use recorded
   selection counters, not a new heap scan. No interpretation of a failed read.
2. Validate retained pointers/lifetimes, native execution context and collector
   behavior; then implement ONE replay control of that query. Do not call from
   the telemetry worker. WB's service is on the movement/game tick despite a
   misleading source comment saying it runs inside the cast.
3. Only after matching the natural control, test bounded start-to-end queries
   across known clear and walled paths. Targets and camera must come from the
   proven paired ManyLights capture, not the authored vector. Keep collision
   outcome separate from visible/blocked/unknown until empirically validated.
4. Consider an explicit ray/filter or calibrated small sphere and endpoint policy
   based on evidence, not a guessed universal wall/visibility flag.

No game run or public visibility acceptance is implied by building this package.
Do not implement light switching or replace the working telemetry pipeline here.

## Built checkpoint

`2.1.15-physics.1` private DMM ZIP is ready. 25 synthetic observer checks, five
focused native regression suites, exact saved-EXE hash verification and ZIP/INI
validation passed. Package validator now recognizes the new research-only switch
while accepting older INIs where it is absent (default OFF). Nothing installed
or published. See HANDOVER for artifact hash and the single next live action.

## Live observation — PID 12228, 2026-09-25

Owner at the known camp. Installed ASI hash matches physics.1. Native log confirms
the exact-build hook and playable-world ManyLights capture. Two explicit observer
requests succeeded, 65.407 seconds apart; no added physics calls or game writes
apart from the instrumentation itself. Telemetry was progressing, with 25 rendered
ManyLights sources at the sampled control. No collision/visibility acceptance yet.

Reports under `artifacts/light-research/`:

- `physics-natural-12228-33035429dbee48b28667237c04a41772.json`
- `physics-natural-12228-07a16c59e57244c0a684b10bd83effa9.json`
- `physics-natural-12228-20260925.native.log`
- `physics-natural-callers-12228-20260925.json` (saved-EXE disassembly, not live code)

Both have thread 6164, caller `module+0x32554FA`, fifth argument = collector, valid
before/after snapshots. First matched the second candidate, next the first. Count
was zero in both; fraction changed from ~1.84467e19 to 1.0, normal remained zero.
This is a natural no-hit result, NOT evidence that any lamp is unoccluded. These
short, diagonal player-near casts are not established to be ground probes.

Important new layout evidence (two independent samples):

- `query+0x40.xyz` = (0.1697388, 0.2169189, -0.376709) in sample 1.
- `query+0x50.xyz` = (5.891406, 4.610017, -2.654569). Componentwise products
  with +0x40 are 1 within 4.2e-8. Sample 2 reproduces the reciprocal relationship.
- `query+0x5C` matches the Euclidean length of +0x40 (0.46666342 / 0.46540228).
  Thus +0x50 is observed inverse-displacement data, NOT a duplicate delta vector.
  Do NOT blindly port WB's ground replay writes of -length into both Y fields.
  A generic segment needs correct preparation, including zero-component handling.
- All four objects are on the active thread stack: transform 0x13FE330,
  query 0x13FE430, collector 0x13FE4D0, shape 0x13FE610. Broad WB-sized captured
  ranges OVERLAP: query+0xA0 starts the collector, collector+0x140 starts the shape.
  They are readback windows, not proven object extents. Other pointer-looking
  words can be neighboring locals/stale hit payload, NOT established fields.
  Never generically remap all qwords across these overlapping windows or retain
  these stack addresses for a later asynchronous call.

The immediate caller is a forwarding/profiling wrapper at RVA 0x3255210, dispatching
five arguments through vtable+0x1F0; it does not build the query. Saved disassembly
again confirms WorldCastShape's five-argument forwarding and world+0xB70/+0xBC0.

Next private revision: controlled replay design with a valid execution context,
owned/accurately rebased data, and an identical-query control BEFORE arbitrary
segments. Observe-only physics.1 cannot replay; requires a closed-game package
change. Preserve this successful capture instead of restarting anchor discovery.

## physics.2: bounded control and segment requests

Owner closed the game after the two successful natural observations. This revision
keeps the default `observe` command and adds explicit `replay` / `segment` modes.
No automatic extra query at startup, no change to public visibility or raw lights.

Execution deliberately differs from WB's deferred movement-tick service: the
extra call runs **after the original WorldCastShape returns, before ShapeHook
returns to the still-active caller**. Same thread, fresh known caller, same world
and live source stack. This avoids dangling captured pointers and a second hook.
It is a new experimental context, NOT yet proven safe in this game. No locks from
our observer are held by the original engine call; the extra query uses the
trampoline and private buffers. A native deadlock cannot be canceled safely by
this instrument; the request/attempt caps are not a timeout for a stuck engine.

All four inputs are copied into guarded private buffers. Only the established
query-shape pointer and collector-inline-buffer pointer are rebased. No blanket
rebasing of the overlapping readback windows. Remaining references are usable
only while the captured caller is alive. Copies never escape for later execution.
Captured sizes still exceed proven extents; canaries are detection, not a proof
against all native bugs. Original query/transform/shape prefixes and the original
collector result are compared after calls; detected changes or exceptions disable
further replay in the process. SEH does not guarantee a faulted engine is healthy:
any such failure requires a restart, no retry in that process.

Each request makes at most one identical-query control and (segment mode only)
one modified segment. The latter is skipped if count/fraction/normal disagree
with the original, input context changes, result is implausible, or guards fail.
Maximum 12 replay transactions per process. A matching no-hit control alone is
weak evidence: require a positive ground control before interpreting wall tests.

Segment +0x50 is populated with reciprocal displacement, +0x5C with length.
Unverified zero/near-zero component convention is refused, not guessed. Limits:
length 0.05..50 gu, origin within 20 gu of player. Sphere radius and collision
filter remain inherited; contact/penetration is NOT automatically optical blocking.

Commands (one-shot, return immediately; no owner-waiting session):

```powershell
./scripts/Start-PhysicsProbe.ps1 -Mode replay
./scripts/Start-PhysicsProbe.ps1 -Mode segment -GroundControl
./scripts/Start-PhysicsProbe.ps1 -Mode segment -LightSampleIndex <current-index>
```

Run these in order only after inspecting the preceding report. Ground control
uses a slightly diagonal downward path near the player to keep all delta
components nonzero. Light mode takes start AND source from the same fresh,
filtered ManyLights snapshot and uses its paired camera. The sample index is
frame-local, not stable lamp identity. Reports retain exact world endpoints.
Do not automatically classify all lights, raycast every frame, or touch the SDF
pipeline. First assess ground, known open path, known wall, then limitations.

Built `CrimsonDesertTelemetry-v2.1.15-physics.2-ModManagers.zip`; SHA256
`CC84F118B1E92064B0389F89399CBC19F731D1ABCE0C42CA6334AF401A0E661F`.
44 synthetic physics checks and five focused native regression suites pass.
Package/INI/payload equality checks pass. Request script parsed and its actual
ground-coordinate assignments were exercised with synthetic camp coordinates.
Not installed automatically; all native replay/segment behavior still needs a
live game test. Default command remains observation-only.

## Live control and segment results — PID 5468

Installed physics.2 hash verified; five explicit transactions completed. All
controls matched, all guards intact, checked original ranges unchanged, exception
zero. ManyLights frame/telemetry sequence progressed throughout. No ASI replacement
or public visibility change. These are instrumented observations, not untouched
baseline evidence.

Reports in `artifacts/light-research/`:

| Report suffix (all .json) | Result |
|---|---|
| physics-control-5468-bc4e1385ec534e858d254af16f41dec8 | Natural no-hit reproduced exactly |
| physics-ground-5468-b9b4206d67304b81941f4ab749950993 | Hit, fraction 0.3111352026, normal (0.000044, 1, 0.000031) |
| physics-lantern-5468-a760c73276e242db958266b8cbb58e94 | First known lantern hit, fraction 0.9068721533 |
| physics-lantern-5468-a32ced963ae34a4cb2e258af9d07a4b7 | Other known lantern hit, fraction 0.9109512568 |
| physics-lantern-short-5468-ea5d1c1d0add4cb79c8f928e205972d3 | First path shortened by 1 gu: no hit, fraction 1 |

Log: `physics-controls-5468-20260925.native.log`.
Ground starts at player+(.6,1.5,.6), ends player+(.7,-3,.67); hit center Y about
609.2566 vs player feet Y 609.1567. This is consistent with a small sphere, not
a measured universal sphere radius. Natural inherited mask/radius were unchanged.

First light full segment: camera (-10535.5684,613.0045,-4420.8491) to paired source
(-10529.7480,611.4641,-4420.3008). Contact is about 0.56 gu before endpoint.
Second target (-10536.1660,611.4647,-4413.7915), contact about 0.65 gu before end.
Shortened first path misses. This localizes its collision to the endpoint vicinity;
it does NOT identify the contacted object (fixture, character or nearby geometry).
No visually confirmed wall/clear pair yet. Do not mark the near-light contact as
an occluded lamp or solve it with a universal 1-gu cutoff.

Script-only improvements need no new plugin: `-NearLightPosition x,y,z` picks the
nearest actual ManyLights contribution within 0.5 gu IN THE SAME fresh paired
snapshot as its camera. `-StopBeforeLight 1` shortens that measured ray only for
endpoint-isolation experiments. A frame-local sample index from an earlier HTTP
request was not reused. Report endpoints are authoritative, not guessed names.

Next: owner outside the shed's closed wall, looking toward the four lanterns.
One wall-crossing segment, then compare with the retained camp results. Seven
transactions remain in PID 5468; no active request and no waiting session.

## Outside the plank wall — PID 5468, 2026-09-25

Owner stood outside the known shed, supplied a screenshot, and judged the four
lanterns covered. Plank gaps are visible, so this is not a solid optical barrier.
Owner also clarifies the lanterns are wall-mounted: the earlier near-source
contacts may be mounting-wall geometry, not necessarily the fixture itself.

Same installed physics.2, no native change. Player (-10534.5635,608.9869,-4425.0371),
paired camera (-10535.7783,611.6642,-4430.9141). Two successful light segments:

| Request ID | Actual light endpoint | Fraction | Sweep-center contact | Before light |
|---|---|---|---|---|
| 132cf90ed3ec42d3aafbca0a277ab7fe | (-10529.7500,611.4661,-4420.3008) | 0.5939830244 | (-10532.1976,611.5465,-4424.6100) | 4.9564 gu |
| 4e67fd9b9c0d43bc89667423319c5a0f | (-10536.1807,611.4673,-4413.7881) | 0.4216989279 | (-10535.9480,611.5812,-4423.6921) | 9.9074 gu |

Contact distances from camera: 7.2510 / 7.2245 gu. Normals respectively
(-0.182516,-0.001820,-0.983201) and (-0.247063,0.000624,-0.968999).
Positions are start + fraction * displacement: sphere-center locations at
contact, not decoded mesh contact points. Both controls matched, canaries intact,
checked original ranges unchanged, exception=0, segment result plausible.
Telemetry progressed from sequence 50975 / light frame 49093 to 56966 / 54459.

An intervening request `201efeb922fe47128efbca267f5170f2` was refused as
`unknown-call-context`: the captured natural query's fifth argument was NOT its
collector. `controlCalled=false`, `segmentCalled=false`; false guard/preservation
booleans are unexecuted defaults, NOT observed corruption. No replay budget used.
One explicit retry supplied the accepted second segment above. Preserve this
rejection as evidence; do not interpret its absent segment result as clear.

Inference: two paths now hit intervening geometry consistent with the visible
wall, well before either lamp. This supports native queries detecting objects /
walls, not just terrain. It does not yet prove optical visibility classification:
inherited sphere radius/mask, plank gaps and near-source collision remain limits.
Next: a visually unobstructed view from the open side/doorway to one of these same
sources, with paired endpoints again. No blanket endpoint cutoff or API change.

Artifacts in `artifacts/light-research/`: `physics-wall-5468-<request ID>.json`
for all three requests above, `physics-wall-5468-20260925.native.log`, and the
owner screenshot `physics-wall-5468-20260925.png`. Seven replay transactions used,
five remain in this process. No active request; no new plugin/install required.
