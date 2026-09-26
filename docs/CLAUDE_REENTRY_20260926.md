# Claude re-entry — 2026-09-26, Codex

Read this first. It summarizes the intervening work without requiring the whole
chronological HANDOVER. Evidence paths below are relative to this repository,
`C:\DEV\CrimsonDesertTelemetry`. Baseline before this documentation commit:
`26b25eb`. No new release, install, game query or code change in this handoff.

## 1. Product and priorities

- One ASI plus managed host provides player/render-camera data, raw rendered
  light contributions, separate grouped/smoothed lights, ambient/sky, local
  HTTP/WebSocket APIs, fullscreen markers and a 3D radar. The merged console
  remains research tooling. `C:\DEV\CrimsonHue` is now ONLY the separate consumer.
- Public baseline is tagged `v2.1.14`; current PRIVATE experiments are
  `2.1.15-physics.*` and `2.1.15-manylights.1`. Do not overwrite/publish their ZIPs
  or use the public README as the complete private-development status.
- Working game target: patch **2.03.02**, EXE **1.0.0.2976**, Steam **25477059**.
  Exact saved EXE: `artifacts/recovery/20260923-191056-build-25477059/CrimsonDesert.exe`;
  SHA256 `57DA440D72F4DB974F25FEF047CF84C4DADD999A88CB2A3C5AF4C9BD67FDE1E7`.
- The owner wants CURRENT nearby lights all around, including behind the camera.
  Distinguish engine view-selection, geometric camera-to-source line-of-sight,
  screen membership, and gameplay ON/OFF. They are not interchangeable.
- Light switching/music-show is deferred. Immediate issue: recover sources the
  renderer filters out BEFORE our existing raw API. Physics cannot classify a
  source missing from its input. Do not restart the old sound/heap/224-byte-pool hunt.

## 2. What changed while you were away

**Camera / World Builder:** owner merged Moon-yungg's PR #2 (local merge
`e09cc60`). It relocates the direct camera via the RIP-relative load plus two
vtable-slot fingerprints, NOT RTTI names: this camera lacks the usual MSVC RTTI
locator. Offline checks reproduced three saved EXEs; unknown-build render hooks
remain gated. This did not change the ManyLights input/output selection.
Broader reuse findings are in [WORLD_BUILDER_RESEARCH.md](WORLD_BUILDER_RESEARCH.md).

**Occlusion:** the old SDF route produced false clears through walls; don't
resume threshold tuning as if solved. World Builder's native physics knowledge
led to validated natural-query/replay controls, then actual thin rays and a
nine-ray stencil. A visible lantern's steel cage CAN block the center ray;
neighbor rays remain clear. A wall blocked all nine. An open bowl supplied a
clear-path control. Contact points are collision intersections along our rays,
not an engine inventory of surfaces illuminated by each light.

This is now integrated experimentally into API/HUD. Early movement/confirmation
logic failed during camera turns; the owner explicitly requested its removal.
Current policy: latest complete nine-ray result, any clear = sampled visible,
all blocked = sampled blocked, invalid/incomplete/older than 500 ms = unknown.
No movement invalidation or two-result confirmation. Preserve actual measurement
pose/time; a hit fraction is NOT optical attenuation. Limits: 256 targets,
20 rounds/sec maximum, shared 2 ms issuing budget, NOT guaranteed 20 Hz per light.
`[LightOverlay] Radius` controls player-centered selection (1..500 gu), while
rays start at the paired CAMERA. Current private configuration is 100 gu.
Owner reported near-live behavior and a successful unchanged restart; broad
geometry/performance coverage is not proven. See [SOURCE_VISIBILITY.md](SOURCE_VISIBILITY.md)
for current behavior and [PHYSICS_QUERY_RESEARCH.md](PHYSICS_QUERY_RESEARCH.md)
for the measured controls. Do not follow their older diagnostic-only next steps.

**Ambient:** global sky RGB is separate and working in earlier tests. Local
camera sky visibility/derived local RGB previously succeeded but were unavailable
for a whole 2.03.02 session. They remain nullable, not newly solved by physics.
Ambient/legacy spatial diagnostics are disabled in the present private test
configuration; absent fields there are not a new regression. See README and
[AMBIENT_STREAM.md](AMBIENT_STREAM.md). Do not mix this into the current task.

**Startup history:** GPU hangs/sound continuing and a lingering process have been
recorded, not causally pinned on the latest change. After reboot the owner ran
physics.10 unchanged successfully. NVIDIA's counter showed 9999 FPS during part
of loading, then normal FPS; owner asked to NOTE it, not deeply investigate unless
strongly relevant. Evidence/checkpoints remain in HANDOVER; don't erase them.

## 3. Current ManyLights finding — the actual breakthrough

The radar is not applying a hidden frustum filter. A raw-copy audit accounted
for every valid output record; 100 gu settings reached API, HUD and physics.
The current raw API is raw **POST-ProcessManyLights** output, not raw engine input.
Known camp sources remain upstream when turning away but disappear downstream.
Historical off-screen support was real as a feature claim, not proof of complete
360-degree coverage; never dismiss the owner's old screenshots or invent that proof.

We added a bounded paired diagnostic at the EXISTING post-filter hook (no extra
hook), copying input + output + counters + scene with one completed GPU fence.
Current input route: captured owner `+0x628` -> outer `+0x30` -> inner;
inner stride `+0xC0`, count `+0xC4`, resource `+0x168`. Layout 32768 x 48 bytes.
Verified load VA `0x143DA9406`, named SRV binder `0x143DA94C5`, existing hook
`0x143DA97DA`; build-specific evidence, not future-update constants.

Input positions are WORLD coordinates; output positions require the PAIRED
camera addition. Initial double-camera input decode was wrong and is superseded
by `decoded-world.json`. More important: full capacity is NOT current content.
The ProcessManyLights structure counter's DWORD0 bounds current INPUT work;
DWORD1 is OUTPUT count. This is NOT the separate producer allocator counter.

Group headers have signed int32 `+40 = -2`, `+44 = member count`. The following
negative-color.w members contribute to the group, not individual physical lamps.
Standalone member skipping also tests packed half at +46; negative w alone is
NOT OFF. Summed ordinary RGB, shader's 5% luminance channel floor and exact 3x3
matrix reproduce known output colors to float-rounding precision. Therefore
"80 hits at one lantern" was **16 current members + 64 stale tail records**, not
80 lights. A grouped fire can have two real render contributions. No arbitrary
position deduplication and no stale-history substitution for current state.

**Completed physical AN/AUS/AN control:** specific rear bowl at
`(-10492.520996, 606.882263, -4446.792969)` — NOT the shrine bowl.

| Measurement | AN A1 | AUS B | AN A2 |
|---|---:|---:|---:|
| Input dispatch bound | 1770 | 1736 | 1770 |
| Current nearby groups / members | 2 / 32 | 0 / 0 | 2 / 32 |
| Nearby paired output contributions | 1 | 0 | 1 |
| Out-of-bound stale position matches | 89 | 89 | 89 |

34 disappearing slots = two headers + 32 members. A1 headers1462/1615 become
1360/1513 in A2: slots are transient, not stable identities. A2 group1360's RGB
matches output slot4 within 4.452e-8; the other group is absent downstream in that
pose. This validates fresh upstream state for this source, NOT a complete world
registry. No more owner toggles or rotation tests are needed for these findings.

Evidence: `artifacts/light-research/manylights-aba-2252-rear-bowl-20260926/`,
subdirectories `A1-on`, `B-off`, `A2-on`: original binary, log, INI and groups.json.
Earlier front/away pairs live in `manylights-pair-2252-20260926-camp-facing-1356/`
and `manylights-pair-2252-20260926-camp-away-1359/` under the same research root.
Analyzer `scripts/Analyze-ManyLightsInputGroups.py`; raw pair decoder
`scripts/Decode-ManyLightsPair.py --input-space world` (also requires capture path).
Both have six focused tests, 12 passed. Full interpretation and exact provenance:
[LIGHT_CONTROL_RESEARCH.md](LIGHT_CONTROL_RESEARCH.md), sections
"ManyLights input for off-screen coverage" through "Bounded producer check".

**Still open before integration:** exact group render-position uses constants/
blue noise; special negative-RGB color cases require exposure. Three such slots
were found in the front sample. A member centroid can only be an explicitly
derived position, not falsely called the exact shader output. No new upstream
API/HUD stream exists yet; the installed HUD STILL uses filtered output.

## 4. PIX: useful progress, exact stop point

The existing September5 capture and full C++ export already suffice for bounded
shader/provenance work. Read [TOOLING.md](TOOLING.md) and
[GPU_CAPTURE_FORENSICS.md](GPU_CAPTURE_FORENSICS.md), not a new tool hunt.

- Capture: `artifacts/light-research/pix-captures/CrimsonDesert_lantern_2026-09-05_2346.wpix`.
- C++ export: `artifacts/light-research/pix-provenance-20260909/cpp`.
- Actual shader map: `artifacts/light-research/cbv-moment/pso-shader-map.csv`.
- ProcessManyLightsCS, PSO475, event93: t18/space37 = INPUT213;
  u13/space39 = OUTPUT217; u2/space39 = structure counter230.
- GPUSpawnPointUpdateCS, PSO21575, events771/775: u38/space39 ->213,
  allocator u19/space39 ->234. Writes literal -2/count header at +40 and
  zero-initializes members. This is an initializer, NOT final member RGB writer
  or a proven join to our live bowl. Packed allocation saved at particle block+80.
- InjectLightGroupsCS, PSO21582, event759: t16/space37 ->14963,
  u12/space39 ->213. Copies 48-byte LightGroupInstanceData from a dynamically
  indexed `g_lightDataBuffers[]` SRV array; its backing resources unresolved.
  Do not conflate this named branch with the 16-member particle groups.
- Existing LOD branch: PSO21562, event779; 80-byte LOD list15397 fed by
  event685 CopyBufferRegion from UPLOAD107. Not proven to be the nearby bowl path.

Actual extracted .ll files: `artifacts/light-research/light-control-pix-20260924/`
(21575 etc.) and `artifacts/light-research/manylights-group-producer-20260926/`
(21582). Use Resolve-PixExportBindings.py for time-accurate bindings, not table
bases. Global event numbers alone do NOT establish cross-queue execution order.
Read-PixExportResource.py gives initial serialized bytes, NOT arbitrary post-event
GPU state; old rounded CSVs are context, not live constants.

**UI task stopped by owner:** PIX was open, analysis active. Computer Use became
available, but we only opened the queue menu and highlighted Compute Queue0;
no resource213 history was inspected/exported and no new UI finding established.
Do not resume clicking automatically. The unanswered bounded question is the
write/dependency history of213 relative to event93, then the final group-member
writer if needed. This may clarify provenance; it must not become another broad
GPU project delaying a useful upstream feed.

**Update, Claude, same day:** both answered offline from the existing export, without
PIX UI: GPUParticleUpdateCS is the member writer, and event93 consumes the previous
frame's producers. See HANDOVER's current checkpoint and LIGHT_CONTROL_RESEARCH.md,
"Resource 213 access history, resolved offline".

## 5. Resume safely / one next step

First review the existing paired-data/group decoder against captured PSO475 and
choose the smallest honest treatment of group position and special RGB needed
for a current upstream stream. Retain the current renderer-output stream as the
reference and keep physics visibility independent. If exact provenance is still
needed, answer only the resource213 writer/dependency question using the existing
exports; don't repeat completed controls or assume PIX UI is required.

Last VERIFIED runtime: PID2252, private **2.1.15-manylights.1**, 7/8 diagnostic
transactions consumed, lamp returned AN after ABA. This is historical process
state, not a current liveness check. ZIP:
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.15-manylights.1-ModManagers.zip`.
SHA256 `9B19C6EFE06D417A39622AD9089D66DF014154745A031C81F112CC91E63B5052`;
ASI `27FD7CF81876AC2D9BC89D0EA7610AA9D9263451759D237325C7C3C644BB0556`.
Last verified INI: Research.ManyLightsPair=1, PhysicsVisibility=1, Radius=100,
HideOccluded=0, legacy SourceVisibility=0. Start-ManyLightsPair.ps1 requests ONCE
and returns; do not launch a ninth request or poll/wait for the owner.

Code entry points: `native/CrimsonDesertTelemetry.Asi/src/` contains
`spatial_readback.cpp`, `manylights_pair.h`, `physics_probe.cpp`,
`physics_visibility_bridge.h`, `filter_thunk.asm`; managed readers in
`src/CrimsonDesertTelemetry.Core/RenderLightReader.cs` and `PhysicsVisibilityClient.cs`.

Git tracked work was clean at takeover; unrelated UNTRACKED aba_test_results*,
capture_aba*, parse_aba*, vt_*, debug_test.cpp, test.cpp, test_ambient.py, set_ini.py,
temp_zip/ and spatial_readback_stub.cpp are preserved, ownership not assumed.
Do not commit them indiscriminately. Research is a separate nested repository;
raw artifacts are local/ignored, not automatically available from GitHub.

Owner constraints: practical bounded work, no endless scene experiments; never
wait/sleep/poll for user input—END and resume on their next message. Require game
shutdown for ASI replacement; keep whole DMM package companions together. Preserve
all immutable packages/evidence; no publication authorized. Documentation is for
assistants, not homework for the owner. Current entry overrides historical "next"
instructions; retain the history but do not execute it as a checklist.
