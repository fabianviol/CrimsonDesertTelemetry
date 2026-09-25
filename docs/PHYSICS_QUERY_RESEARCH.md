# Physics-first occlusion investigation — 2026-09-25, Codex

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
