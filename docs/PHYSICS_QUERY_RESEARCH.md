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
