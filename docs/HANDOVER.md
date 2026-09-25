# Current: physics queries FIRST — 2026-09-25, Codex

Owner explicitly deferred light switching in favor of unresolved occlusion.
Read `docs/PHYSICS_QUERY_RESEARCH.md`; use World Builder's verified native-query
route, not more SDF tolerance tuning. Working paired ManyLights remains the source.

**physics.1 baseline:** research-only `PhysicsProbe` observes ONE natural player-near sphere cast,
including input bytes, collector output, thread/caller and exact EXE. No replay,
no extra physics call, no public visibility change. It is gated to patch 2.03.02 /
EXE 1.0.0.2976 by hash, instruction bytes and collector vtable; other builds refuse.
First draft replay was deliberately removed before build. Host tests pass; now
TWO live natural-query observations succeeded in PID 12228 at the known camp.
Installed ASI matches physics.1; ManyLights/telemetry remained live. Both raw
results are no-hit, NOT a visibility result. Details/evidence in the physics doc.

Critical live findings: +0x50.xyz holds reciprocals of +0x40.xyz, not a duplicate
delta (confirmed twice). All query objects are stack-local and broad captured
ranges overlap. Do not blindly copy WB's ground-replay vector writes or remap all
qwords in overlapping windows. Caller is a forwarding wrapper, not the builder.

Earlier package: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.15-physics.1-ModManagers.zip`.
Built and validated; SHA256 `0D5B720185DA6C628EB2D113586467F01E39AE5C42C1C18EFAE2708C2C7B4002`.
25 synthetic physics checks and 5 focused native CTest suites pass, including
build guard, render bridge/filter and HUD model. Saved current EXE passes actual
hash verification. ZIP/expanded payload equality and INI/package self-tests pass.
PhysicsProbe=1; Ambient and SourceVisibility/spatial diagnostics OFF for this run.
Normal player/camera + ManyLights + HUD stay ON. User installs the WHOLE ZIP via
DMM with game closed. Nothing was installed automatically; no public release/push.

**Current package: physics.2.** Owner closed the game. New opt-in `replay`
and `segment` requests run after the original call returns, on the same thread
while its caller stack is alive; guarded private copies and matching-result
control, no queued stack pointers. This execution context is NOT yet live-tested.
Read the final section in the physics doc before use; native hangs cannot be
timed out safely. Segment inversion corrected; zero-axis convention refused.
No result is published as optical visibility; max 12 replay transactions/process.
ZIP: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.15-physics.2-ModManagers.zip`.
SHA256 `CC84F118B1E92064B0389F89399CBC19F731D1ABCE0C42CA6334AF401A0E661F`.
Same ready-made INI as physics.1. Package content/INI validation passed. Native
tests include copy isolation, control mismatch, fault latch, canaries and bounds.
Live replay and segment controls now succeeded in PID 5468 (see below).
No public API/published release was changed.

**Latest live result (PID 5468, installed physics.2 verified):** replay matched;
ground control hit at fraction 0.311135 with upward normal. Two camp-lantern
segments hit late (~0.907 / 0.911); stopping the first path 1 gu before its light
gave no hit. Thus collision queries distinguish hit/no-hit, but endpoint/fixture
contact is not established optical occlusion. All 5 transactions passed control,
guards and original-data checks; no exception; ManyLights continued advancing.
7 of the 12 replay transactions remain. Raw reports/log are archived; details in
the physics doc. No further native change or reinstall needed for the wall test.

**Next:** owner moves outside a closed wall of the camp shed, facing the lanterns
through the wall, and returns. Request ONE current paired-frame light segment via
`Start-PhysicsProbe.ps1 -Mode segment -NearLightPosition @(-10529.755,611.292,-4420.3)`
(or another of the four present sources). Script now resolves the target within
the same frame instead of reusing a transient index. Optional `-StopBeforeLight 1`
is endpoint-isolation research ONLY, not a production occlusion threshold.
No active request remains; no waiting for owner input. Restart on replay faults.
Collision outcome is not yet optical visibility. Current SDF classifications remain
experimental/default-off; do not declare the open occlusion problem solved.

## Earlier: camera compatibility PR merged; research preserved

Owner merged GitHub PR #2 (Moon-yungg, head `40f4d0d`, remote merge `9ce0b67`).
Local merge `e09cc60` incorporates it without losing our five local research
commits. Combined managed Release build: zero warnings/errors; 73/73 tests pass.
No installed ASI, package or published release was replaced. No push performed.

This is NOT an RTTI-name camera resolver: the camera vtable lacks the usual
MSVC locator (independently checked). It enables automatic direct-camera
relocation using the existing RIP load and two vtable-slot fingerprints.
Offline review reproduced the exact current profile and also resolved preserved
EXEs 1.0.0.2850 and 1.0.0.2949. Unknown builds still lose NativeCapture and
EngineLights; layout/freshness validation remains. No live-game validation of
the new automatic path. Minor review follow-up: the new NativeCapture-null
assertion starts with null; seed a valid native contract to test its removal.

The later physics-first checkpoint above supersedes the object observation below;
merging this contribution does not authorize a new release or broad discovery.

## World Builder reuse / update recovery — 2026-09-24, Codex

Owner broadened the investigation beyond switching lights: examine World Builder
for unresolved occlusion/sky and alternative recovery of working telemetry too.
Read **docs/WORLD_BUILDER_RESEARCH.md** first. Pinned full source clone is under
`external/crimson-desert-world-builder`, commit `ee1f05a3ad1a61cd4aee66946155d0315fdc14e7`.

Concrete findings: native scene create/enable and server gimmick paths, plus
physics sphere sweeps for ground placement. The latter is a new route around
our SDF false clears, NOT an already validated optical visibility solution.
Public C/HTTP APIs only manage WB-owned objects; no ready-made lamp switch/RGB
or all-world object API. Our exact known fire-lamp prefab occurs in its tables.
Keep camera-paired ManyLights; WB's camera pose/heap ranking is not a replacement.

Offline current patch 2.03.02 / EXE 1.0.0.2976 / build 25477059: 17 literal
patterns checked, 15 unique, 1 absent on disk, and 15 references agreeing on one
world global. Named create/ray/shape entry points also recovered; CastShape has
two candidates. See the report for exact RVAs, limits and private evidence paths.
New `scripts/Inspect-WorldBuilderAnchors.py` is read-only; 10 targeted tests pass.
No third-party binary built/installed, live write, product build or release change.

**Next bounded step:** prepare default-off observation-only capture at the native
scene create/enable boundary for our known lamp; validate instructions/context
before arming. Recover current object/child lifetime before a reversible write.
Physics query validation is a separate next avenue, not silently enabled now.
Do not ask for another broad scan or install a second ASI. The earlier preset
read and post-filter modulation remain alternatives, not mandatory next actions.

# Earlier light-control / preset checkpoint — 2026-09-24, Codex

Owner now wants to investigate switching/dimming game lights for a music light
show. The earlier release STOP below was superseded by the owner's explicit
publication instruction; preserve the released package and all historical crash
evidence. Do not resume release/crash work implicitly.

Read `docs/LIGHT_CONTROL_RESEARCH.md`. Old PIX export was reused to prove the
filter-output -> light-tree binding (resources 213 -> 217 -> tree 248). It gives
a precise candidate for a per-lamp RGB intervention; no control experiment or
live write has run. Physical Source evidence survives in the old fire handover.
Current RTTI lookup was repaired for renamed `.xdata`, with offline tests.
Two bounded current-process position scans were incomplete, NOT negative.
The owner closed PID 13528. No hook was armed and no live write ran.
Owner clarification: use PIX as a source of captured data, NOT a scene-replay
project. Stop replay work; the separate C++ build was terminated, its files and
the original capture/export preserved. Baseline replay did run but both its
output and the capture's stored original have HUD over a black scene (already
noted in the historical handover). No visual light-control result follows.
Owner then recalled the `Let There Be Light` preset route; inspect that upstream
route before implementing persistent render modulation. Current loader and
owner paths are relocated OFFLINE: owner `module+0x6C8E9D0`, independent parent
`module+0x6C8E978` -> **+0x778** (old +0x760 is obsolete). Details/evidence in
`LIGHT_CONTROL_RESEARCH.md`. Next owner action: start/load the game. Then one
bounded read-only comparison of both paths and named preset tables; no toggles,
heap scan or writes. Existing direct consumers are Nit/material, NOT a proven
Lumen/lamp path. The two paths are not yet live validated on this build.
No production plugin/package changes; post-filter modulation remains fallback.

Targeted PIX re-read for control (no replay): event 779 `InjectEmitterLodLightCS`
really multiplies RGB by `_lodLightScale`, reads named 80-byte `LodLightInfo`
entries, and writes unfiltered resource 213. Event 685 uploads 14 such entries
from UPLOAD resource 107 +86944 to 15397. Useful earlier alternative, NOT proof
of controlling the nearby fire or every light. Details/caveats and the other
named candidate fields are in `LIGHT_CONTROL_RESEARCH.md`; post-filter remains
the bounded first proof. No new build, hook or live write in this check.

# Historical 2.1.14 crash A/B and deployment correction — 2026-09-23, Codex

The owner disabled only `CrimsonDesertTelemetry` in DMM, launched the same save,
and reported that it loaded and ran. DMM activity records the ASI disabled at
22:20:33 and the game launched at 22:20:39; the ASI file was absent from
`bin64` when checked. The owner then closed the game. This strengthens the ASI
association but a single A/B run does not isolate the native GPU path from a
mixed installation or a transient driver failure.

The owner correctly notes that earlier private builds ran before the Error-6
bootstrap change. Keep that temporal association in the differential diagnosis.
The exact diff only made the host log and NUL stdin inheritable for `CreateProcessW`;
it did not edit D3D12 command generation, and `bInheritHandles` was already true
before the change. An indirect startup/timing effect is possible, not established.
If a clean same-build retry still hangs, compare an otherwise identical build
with only this bootstrap change reverted before opening broad GPU research.

DMM's library held the correct 2.1.14 `crimson-desert-telemetry.deps.cfg` but
`bin64` retained the 2.1.11-rc.2 copy. With the game closed, the exact old file
was backed up to
`artifacts/deployment-backups/20260923-2230-stale-deps/crimson-desert-telemetry.deps.cfg`
and only `bin64/crimson-desert-telemetry.deps.cfg` was replaced with the exact
2.1.14 package file. The target hash now matches the package
(`AD78244EFB69E454E4709C395E74E0B38924DFC3AE4ACC8D2A2C40E17307BCA7`).
Do not infer that this fixes the GPU hang: the .NET host started with the old
CFG. **Next action:** owner re-enables the same 2.1.14 ASI in DMM but does not
start the game yet. Verify all six deployed runtime hashes against the immutable
ZIP, then ask for one same-save retest. If it hangs again, stop repeating and
diagnose the native D3D12 path offline. No public release until resolved.

# Earlier 2.1.14 live crash — release STOPPED, 2026-09-23, Codex

The owner installed the exact 2.1.14 ZIP via DMM and the game crashed while
loading a save at 22:09:51. DMM reported minidump exception `0x887A0006` in
`kernelbase.dll+0xC41CA`; Microsoft identifies that HRESULT as
`DXGI_ERROR_DEVICE_HUNG`, which can arise from invalid GPU commands and does
not by itself prove bad hardware. The owner confirmed that game audio kept
running. Windows System log has an `nvlddmkm` event 153 at 22:09:46. DMM's
headline "not a mod" is not an ASI-plugin exoneration; its own panel listed
`CrimsonDesertTelemetry` as injected. Native log shows the exact ManyLights
detour installed, first playable-world signal received, and recurring capture
armed. Overlay reached D3D12/SDR ready. There was no fatal detail in host,
native, overlay or bootstrap logs. The game's crash log is
`C:\Users\fabia\AppData\Local\Pearl Abyss\log\Launcher_2026_09_23_22_08_14_8272.log`;
dump is `C:\Users\fabia\AppData\Local\Pearl Abyss\DumpCache\reports\39e5f532-15dc-49f4-9b1e-b7d1c443b31c.dmp`.

Five of the six deployed runtime files matched the exact ZIP. DMM left an old
`crimson-desert-telemetry.deps.cfg` from 2.1.11-rc.2 instead of the 2.1.14
companion; the host did start, so this is a real packaging/deployment defect
but not established as the GPU-hang cause. Do not publish 2.1.14. Keep its ZIP
immutable. Before attributing blame to game, driver or plugin, run the same
save with the Telemetry **ASI plugin disabled in DMM** (data mods are a separate
category). Ask the owner for the result, then compare one controlled plugin-on
run only if justified. Do not wait/poll between owner actions.

DMM called Telemetry "no log" even though four component logs existed beside
the ASI. Its search rule is unproven; the local DMM binary was inspected but
source was unavailable. New un-packaged source mirrors bootstrap messages to
the conventional `CrimsonDesertTelemetry.log` while preserving
`.bootstrap.log`. Targeted native build and no-parent-stdin bootstrap smoke
passed; the smoke checks that the primary log is actually written. This change
is **not** in immutable 2.1.14 and is not evidence about the GPU hang. Only
package it in a later version after the crash investigation.

# Earlier 2.1.14 candidate checkpoint — superseded by live crash

**Use only the latest candidate**:
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.14-ModManagers.zip`, SHA-256
`C325B987FDE681C7516C9F298D6D8E1939195BD7AE5A624C6C68A73CB0898F1A`.
Its package README and repository README lead with the public Crimson Desert
patch version **2.03.02**, confirmed against Pearl Abyss's 2026-09-23 patch
notes (<https://crimsondesert.pearlabyss.com/en-US/News/Notice/Detail?_boardNo=133>).
EXE file version 1.0.0.2976 and Steam build ID 25477059 are only technical
identifiers. The installed EXE hash matches the exact guarded build profile.
The owner correctly distinguished the public patch version from EXE metadata;
do not call 1.0.0.2976 the game version again.

Package self-test and exact ZIP/expanded-payload equality passed; final
2.1.14 expanded package passed the bootstrap smoke with no parent stdin.
30/30 native tests and managed tests passed on the same code. No new in-game
run has happened yet. Earlier immutable local candidates 2.1.12 and 2.1.13
were never installed or published; do **not** use them for the one live test.
Next action is DMM install of exactly 2.1.14 while the game is closed, followed
by one live integrated check. End the turn at user action; no polling.

# Earlier 2.1.12 candidate checkpoint — superseded by 2.1.14

The owner chose one production ZIP, keeping the narrow per-light geometric
visibility path compiled but marking it EXPERIMENTAL and setting
`SourceVisibility.Enabled=0` in the shipped INI. A live view in front of a shed
had falsely labelled lights behind solid wood `SOURCE VISIBLE`; do not promote
that classifier or include it in release acceptance. Normal raw/EMA ManyLights,
ambient, HTTP/WebSocket and both HUDs remain enabled. Published 2.1.11 is untouched.

Nexus user buck0021 reported only "error 6" and a speculative WebView diagnosis.
The actual source and log have not been supplied, so the cause is unconfirmed.
The ASI bootstrap did violate `STARTF_USESTDHANDLES`: it passed a non-inheritable
host-log handle and the game's possibly absent stdin to `CreateProcessW`. The
new code opens inheritable host-log and NUL-input handles, closes them after
launch, and falls back to no redirection if NUL cannot be opened. The boot
smoke now clears its own stdin to model a GUI process. This is a concrete
hardening fix, not proof that the external report is resolved. A comment already
requests the user's actual error location and bootstrap log; do not claim WebView
is a requirement.

The exact build 25477059 profile was promoted to `locally-validated` based on
the prior all-on live run plus subsequent HUD captures after camp-to-shed player
movement: position and yaw changed with the scene, native camera/ManyLights
remained live, and ambient values were populated. Exact EXE hash is
`57DA440D72F4DB974F25FEF047CF84C4DADD999A88CB2A3C5AF4C9BD67FDE1E7`;
`allowAutomaticCompatibility=false`, so other EXEs still fail closed. The
separate per-light visibility verdict is not part of this validation claim.

The immutable candidate ZIP is
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.12-ModManagers.zip`, SHA-256
`B37EE43EEE85E9DA4E71852CC29258E37B1D242D557D72FF3A850AF3BAC42016`.
It is `CDT_RESEARCH=OFF`, targets Steam build 25477059, and contains the fixed
bootstrap and default-off experimental INI. Package self-test passed; 30/30
native CTests passed; managed tests passed after adapting the private-candidate
gate test to an in-memory research fixture; the exact expanded ZIP passed the
bootstrap smoke with no parent stdin and live HTTP health/schema/smoothed route.
These are offline/host tests, not the owner's new game test.

**Next step:** with the game closed, install exactly this ZIP through DMM; start
and load one save. When the owner reports in-game, run one bounded integrated
check (`scripts/Check-IntegratedTelemetry.ps1`) and inspect the status/HUD and
bootstrap log. If it passes, finish main README/release ledger, commit, and
publish to GitHub and Nexus. Never wait/poll for the owner between turns; and
never overwrite either published 2.1.11 or this candidate ZIP.

# Build 25477059 integrated.3 live check and integrated.4 candidate — 2026-09-23, Codex

The owner installed `integrated.3` and sent a screenshot showing LIVE player,
camera, native light radar and fullscreen rendered lights. The bounded one-pass
`scripts/Check-IntegratedTelemetry.ps1` report is
`artifacts/integrated-check-20260923-205124.json`: health `playing`, exact
private compatibility `research-exact`, player/camera/orientation and authored
plus rendered lights available, smoothed groups available, all three WebSocket
connections received. The ambient HTTP sample was available, but its WebSocket
sample and most subsequent samples were unavailable. All 11–15 current rendered
light sources had visibility `unknown/waiting-for-volume`, never fabricated
clear/blocked. This was one all-on run, not a series of isolated feature tests.

Ambient intermittency had a precise cache bug: `SkyAmbientReader.Capture`
decoded a cached unchanged sample with the old default producer RVA instead of
the build-specific `expectedProducerRva`. It reset the mapping and reported
`bridge-invalid` followed by `bridge-missing` during its retry interval. Fixed
in source and covered by a two-read relocated-producer mapping regression test.

The SDF visibility acquisition was read *without changing game memory* using
the preserved exact-ASI state-inspection script, retargeted in-memory to the
installed ASI hash. Evidence is `artifacts/sdf-state-build25477059-20260923-2055.json`.
Three stable readings showed `phase=Failed`, reason
`list-reset-before-confirmed-submit`, no completed readback, and the last
contexts at loading frames 7/8 with zero camera. The SDF path had armed before
the managed host's playable-world signal, while ManyLights itself already
waited for that signal. The change delays SDF interception until the same
signal. It does **not** relax list/fence/identity guards or claim that the
remaining GPU-list behavior is validated; a new live check is required.

The next immutable private DMM ZIP is
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.12-build25477059-integrated.4-ModManagers.zip`
(SHA256 `11634393F778FF2A3C0BA6F57CF7BE9CA04488A6970945529C7339D54DFC9163`).
`CDT_RESEARCH=OFF`, exact EXE hash, all current product switches on, profile
still `research`. Managed tests and all 30 native CTests passed; package
self-test passed. `.4` is built but **not installed or live validated**. The
game was still open at last check with `.3`. Close it before replacing via DMM;
after restart, run one bounded integrated report and inspect source-visibility
reasons. If still unknown, inspect the exact ASI's SDF phase once, not another
blind toggle series. Do not poll/wait for the owner; end the turn at user action.

# Build 25477059 private all-on startup correction — 2026-09-23, Codex

The owner installed `integrated.2` via DMM and started the game. Screenshot
showed both HUDs but `WAITING FOR DATA` and status `This game build needs an
update / Static position is not validated for this build.` Read-only health
confirmed `unsupported-build` with that exact error. Native log confirmed
the exact-build ManyLights detour and sky hook installed, but the managed host
stopped before its first sample. This was a double confidence gate: the private
exact-hash compatibility resolver accepted the research profile, then
`StaticPositionProbe.Resolve` rejected its `candidate` anchors (and the
orientation reader would have done likewise). No evidence here that the
ManyLights/Ambient/visibility paths themselves failed.

The corrected immutable ZIP is
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.12-build25477059-integrated.3-ModManagers.zip`
(SHA256 `FFA331A8895EF32365E08FF306769191BBD30B53C85C1617518C8D418250F731`).
Only `research-exact` may read candidate player anchors; public `tested` and
`automatic` modes keep the prior trusted-anchor rule. RIP uniqueness, profile,
pointer-chain, RTTI and per-sample plausibility guards remain. Managed tests,
including the candidate gate, and package self-test passed. The `.3` package
has **not yet been installed or live-tested**. Game was still open at last
check. Next step: close game, replace `.2` with `.3` through DMM, start/load,
then run the one-pass `scripts/Check-IntegratedTelemetry.ps1` and inspect the
HUD. Do not poll/wait for user action; end turn and resume on message. Older
`.1` and `.2` packages are retained but must not be installed for this test.

# Build 25477059 private all-on integration package — 2026-09-23, Codex

The owner rejected another sequence of single-feature game runs: test all
current **product** features at once. This entry describes the superseded
private ZIP
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.12-build25477059-integrated.2-ModManagers.zip`
(SHA256 `7E4791625AA50D480CAB0B8AF18A73E7B334CF110549A61FB86D2BB1A8DE0703`).
Immutable `integrated.1` preceded the build-specific Ambient reader correction;
do not install `.1`. Nothing has been installed or publicly released yet.

This exact-hash, `research`-status package has `CDT_RESEARCH=OFF` (current
product paths, not mutually exclusive console/probe modes). INI enables
Server, Notifications, Lights, ManyLights, Ambient, SourceVisibility, Overlay
and LightOverlay. The bootstrap passes `--private-exact-build` to the managed
host only because its native contract is explicitly private/research. The
ordinary CLI resolver still rejects the research profile. The host labels
compatibility `research-exact`; health `supportedBuild` remains null, never
claims public support. Game EXE hash and native hook/context bytes must match.
The Ambient reader now checks the per-build sky producer RVA from the profile;
otherwise the relocated hook would have been falsely reported as invalid.

Managed tests passed. Native CTest passed 30/30; offline preflight matched the
new ManyLights and Ambient callsites against the installed EXE. The package
self-test passed, and all eight product switches above were confirmed `1` in
the nine-file ZIP. Those are build/offline checks, not a live all-feature result.
One bounded post-install check is `scripts/Check-IntegratedTelemetry.ps1`,
which reads health, player/camera, raw/EMA lights, visibility, ambient and the
three WebSocket streams into one fresh report under `artifacts/`. It makes no
game changes and does not wait for the owner. The owner should also visually
confirm both HUDs/status; the report cannot judge presentation.

**One next step:** install integrated `.2` via DMM while the game is closed,
start/load once, then run the one-pass check when the owner reports in-game.
Separate genuine failures by their status/reason; do not restart broad light
research or claim everything works merely because the INI enabled it. End the
turn whenever user action is needed; no polling or waiting.

# Build 25477059 ManyLights recovery — controlled fire AN/AUS/AN, 2026-09-23, Codex

Interaction rule from the owner: never keep a turn/session running while waiting
for an in-game action or reply. End the turn; the owner resumes with `AN`,
`AUS`, `weiter`, or another message. No polling or sleep in the meantime.

The owner completed the isolated fire AN/AUS/AN with the portable lantern
stowed: visible illumination vanished on AUS and returned on AN. The full
authored-light vector stayed unchanged at three progressing frames; do not
repeat that branch. The previously proven filtered ManyLights path now also
passed a controlled AN/AUS/AN on the new game build (details below).
The owner explicitly wants future rendered-light checks to reuse this secured
ManyLights path, not to infer status from another array. Its exact build guards
and live response must be revalidated after updates; failure means unavailable
until diagnosed, not permission to guess a replacement.

The exact-build `25477059` definition is deliberately `research`, so the
managed public compatibility resolver still rejects it. A separate explicit
`-UnvalidatedDiagnostic` package path compiles a hash- and byte-guarded native
capture only, with server, ambient, visibility, notifications and HUDs OFF.
The ZIP is
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.12-build25477059-diagnostic.2-ModManagers.zip`
(SHA256 `FDE598D02DB617C194C3A0320097C12217D574EA3E844B21FE4467CE67CB9C4E`).
Diagnostic `.1` is an earlier immutable build; use `.2`. `.2` was installed
privately via DMM and run in the game; nothing was released publicly. Managed
tests passed; native CTest 30/30 passed;
native file preflight matched all ManyLights hook/caller bytes and both ambient
hook contexts against the current EXE without accessing the game process.
Ambient/spatial source locations remain static candidates, not runtime-validated.

The diagnostic has `Server.Enabled=0`, so the native detour initially remained
`bridge-waiting` despite successfully installing. Once the owner confirmed the
playable world, Codex used the existing
`NativeCaptureReadySignal.TrySet(processId)` one-shot gate; no plugin change or
rebuild was needed. That manual signal must be repeated after a restart of this
private package. The running process was PID 19204. The exact-build,
read-only `scripts/Capture-PrivateManyLights.ps1` recorded three fresh, advancing
bridge samples under `artifacts/light-research/`:

| Owner state | File suffix | Sequence / frame | Nearby count | Light within 0.5 gu of known fire `(-10529.755,611.292,-4420.300)` |
|---|---|---:|---:|---|
| AN | `201348-AN.json` | 232 / 25201 | 8 | 1 at `(-10529.738,611.467,-4420.300)`, sample #31 |
| AUS | `201437-AUS.json` | 979 / 27817 | 7 | 0 |
| AN | `201530-AN.json` | 1785 / 30650 | 8 | 1 at `(-10529.749,611.467,-4420.301)`, sample #24 |

The paired camera X/Z remained identical to 0.001 gu; captures were 15–47 ms
old. The sample index is transient, while the world-space location and the
AN/AUS/AN response identify the fire contribution. This validates the native
filtered ManyLights path for this fire on build 25477059, **not** the whole
product, ambient, source visibility, all lights, or update stability. Keep the
build definition in `research` status; do not promote it or publish `.2`.

**One next step:** validate the remaining product paths on this build in
separate bounded controls (starting with ambient/spatial and player movement),
then consider an exact-build production package. Preserve these three raw
captures and the old release package. When user action is needed, end the turn;
do not poll or wait.

# Game update 25477059 — checkpoint, 2026-09-23, Codex

Steam briefly installed build `25455892` and then replaced it with `25477059`.
The owner confirmed the second update was finished. Both EXEs were preserved
before further Steam changes:

| Build | Private backup | EXE identity |
|---|---|---|
| 25455892 (superseded) | `artifacts/recovery/20260923-190119-unknown-a9e5ca20/` | `1.0.0.2949`, SHA256 `A9E5CA2076367E7995B81A3A4803F7259AB7DAC3415DF8EA949043EF635A174A` |
| **25477059 (installed)** | `artifacts/recovery/20260923-191056-build-25477059/` | `1.0.0.2976`, 384521624 bytes, SHA256 `57DA440D72F4DB974F25FEF047CF84C4DADD999A88CB2A3C5AF4C9BD67FDE1E7` |

`dotnet run --project src/CrimsonDesertTelemetry.Cli -c Release -- check-update
'C:\Steam\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe'` reports
`anchor-check-failed` against the old definition. It is read-only and did not
enable any hooks. Do **not** install or advertise compatibility yet. No ASI was
installed. The existing untracked analysis files were left untouched.

Bounded old/new EXE disassembly gives *static candidates*, not runtime proof:

| Path | Candidate RVA in 25477059 | Evidence still needed |
|---|---|---|
| ProcessManyLights entry | `0x3DA8210` | Live owner/wrapper/resource and scene pairing |
| Counter binder / filtered binder | `0x3DA96CC` / `0x3DA9790` | Same R15/R12 roles seen statically; virtual binder slot changed `+0x4D0` → `+0x4E8` |
| ManyLights hook | `0x3DA97DA` | Instruction boundary verified; dispatch slot `+0x328` → `+0x338`, next call `+0x208` → `+0x218`; active shader still unverified |
| Ambient source A / hook A | `0x393100F` / `0x3931317` | Paired source-to-hook structure and Dispatch(1,1,1) seen; output layout/live values unverified |
| Ambient source B / hook B (diagnostic only) | `0x393424B` / `0x3934313` | Same caveat; do not enable B as product path |
| Spatial dispatch / exposure return | `0x389C000` / `0x362595A` | Unique longer dispatch-body prefix and matching consumer shape; live ownership/resource checks still required |

Read-only runtime spot check on PID 11280 (started 19:22; no ASI): the relocated
scene global `+0x6C8CF30` pointed to a live scene object with vtable RVA
`0x5D20718`. `scene+0x428` pointed to a 2816-byte-shaped scene buffer: 3840×2160
and camera XYZ about `(-10027.9, 522.4, -4434.8)`. The relocated static player
slot `+0x6D77858` gave about `(-10030.1, 519.3, -4429.3)`; proximity is plausible,
not a movement control. The scene frame counter `+0x2C8` advanced from `0x2580`
to `0x3CB7`, so the scene was progressing. **The old authored-light pointer at
`scene+0xF08` was null.** `scene+0xF10` was nonnull but its target did not have
the expected vector at `+0x10`; do not substitute it by guess. A bounded read-only
scan of only `scene+0x800..0x2000` then found one vector-shaped candidate at
**`scene+0xF90`**. Its descriptor contained 15 records, capacity 18, stride
`0xB8`; all 15 decoded at the old position/color/cone/active/selected offsets.
Eight were active with plausible nearby world coordinates, including two at
exactly `(-10029.832, 520.2973, -4428.764)`, close to the player. The inactive
records had zero positions. This strongly identifies the relocated authored-light
vector. A later controlled fire AN/AUS/AN left this vector unchanged; see below.
With only these candidate
offsets changed in diagnostic definition objects (not game memory; no profile
promoted), the full existing
`EngineCameraReader.Capture` passed basis/projection/scene checks twice, and the
full `EngineLightReader.Capture` returned `available`: 15 source records, 8
within 200 gu, 0 malformed/unsupported, 0 walk races. The eight decoded records
include point and spot sources, plausible linear colors, renderer scales and
directions. These are data/layout checks, not visual source identification.
The old RTTI-guarded player chain failed only at `owner+0x2B8`; a bounded owner
scan found exactly one physics candidate at **`owner+0x2C0`**. With that offset
in the diagnostic definition, the unchanged `PlayerOrientationReader` passed its
RTTI, position agreement and orthonormal basis checks, reporting heading 333.8
degrees. This
is not yet a movement/rotation control.
All addresses in this paragraph except module-relative RVAs are session-specific
and must not become update anchors.

Next: prepare an exact-build, fail-closed **private diagnostic** capture using
the changed native signatures and anchors, then repeat this known-light AN/AUS/AN
through paired ManyLights. Run tests and validate ambient before any promotion.
An EXE-only match does not validate shaders or per-light visibility.

Controlled AN/AUS/AN at the known four-fire-lantern camp, player
`(-10530.401, 609.15674, -4419.537)`, no ASI and no player movement: read-only
authored-vector samples at progressing scene frames 59996/61855/65107 all had
16 records, 15 `recordActive` and 6 `rendererSelected`. No position or status
disappeared and returned with the switched lantern; only two near-player record
positions drifted slightly while their flags and scales stayed constant. Thus
this CPU authored-light vector is **not a validated status source for this fire
lantern**. Do not treat this negative result as refuting the previously measured
ManyLights/rendered-fire path. Next controlled check must use the exact-build
ManyLights capture, not more searches in this array.

Follow-up visual clarification: the owner's *portable lantern* was still on
during that fire-switch series and masked the perceived illumination change.
With the fire on, stowing the portable lantern kept PID 11280 and the player
near the camp; authored slots 11 and 12 both changed from active/selected with
positive renderer scales (13.094/5.238) to inactive/unselected with scale 0.
Their positions had followed the player. This independently identifies those
two contributions as the portable lantern, not the switched fire. Slot 15 was
reused by a different record and must not be treated as a permanent light ID.
The isolated fire AN/AUS/AN was then completed with the portable lantern stowed,
same PID 11280 and stationary player `(-10530.632,609.1567,-4419.819)`.
Progressing scene frames 9331/18291/21721 all had 16 authored records, 14
active and 4 renderer-selected; the entire record set was unchanged between
these samples. The owner reported that the fire illumination visibly vanished
on AUS and returned on AN. This is a valid visual control that the earlier
portable light had obscured. Consequently the authored array is **not** the
fire-light state channel for this lantern; it says nothing negative about the
ManyLights path. Stop testing this array for that fire and move to the exact-build
render capture.

# Current checkpoint — handover to Codex, 2026-09-23, Claude

Nine days with no commits. This replaces four stacked checkpoints from 13–14
September with one statement of where everything actually stands, verified today
rather than recalled. Codex's own 13 September checkpoints stay below as history;
the fire-visibility one still holds the live test that was never run.

## Verified today

| | state |
|---|---|
| Nexus | `2.1.11` is `main`, uploaded 09-13 17:54, `2.1.10` archived. Nothing newer uploaded. |
| Git | `origin/main` == local, nothing unpushed, last commit `b80e22e` on 09-14. |
| Machine | **The mod is not installed.** `bin64` holds only leftover `crimson-desert-telemetry.deps.cfg` and `.runtimeconfig.cfg` — the known DMM removal behaviour, visible again. Game not running. |
| `C:\CrashDumps` | **empty.** WER has been armed for `CrimsonDesert.exe` since 09-13 and the unexplained local crash has not recurred in nine days. |

## The crash is done

`2.1.11` fixes a proven ordering violation: Telemetry installed its HUD swapchain
hooks synchronously inside DXGI's `CreateSwapChainForHwnd`, before Streamline had
associated the new swapchain with its command queue, which is exactly where the
users' `E_ACCESSDENIED` came from. Verified against NVIDIA's published Streamline
v2.11.1 source.

**jimos87 confirmed it externally** on 09-14 09:57: "It is now working for me :D ty"
(RTX 4080 SUPER, 616.92, DMM 2.8.1). That is one user, not the three consecutive
starts the acceptance gate asked for, and his own evidence had shown the failure was
intermittent. WHOLE (RTX 4090) never reported back. Public wording says one of two
confirmed and should not be tightened further without WHOLE.

Two things remain open and are recorded rather than solved: nobody ever answered how
many monitors the affected users run, and the owner's own 09-13 19:20 crash has no
dump and no explanation. Both of the owner's local crashes happened on a two-monitor
extended setup during an output change — a configuration the users may not share, and
a different signature (runtime, `+0x3D05303`) from theirs (startup, `+0x3D0FEA6`).
Do not merge those two.

## Goal 1 is done; goal 2 is explained, not solved

**Ambient / sky occlusion works and ships.** Measured 0.3916 on open ground, 0.0476
in an open-fronted stable, and **exactly 0** in a closed interior. Its one product
consequence: `sky × visibility` drives a lamp to black indoors, so the consumer needs
a perceptual curve with a floor. That is not optional.

**Per-light occlusion: the blocker is now explained.** See
[SDF_BAND_LIMIT.md](SDF_BAND_LIMIT.md). The field stores a thin signed band around
surfaces, not solid interiors: probing down through ground the player stands on, the
negative region is 0.6 gu wide (room) and 0.3 gu (camp), reaching −0.181 and −0.092,
and immediately below it the value returns to **+0.53027**, the positive clamp. So
"inside" is not a persistent state, and a ray registers a nonpositive value only when
it crosses that one-to-two-cell band in a way trilinear interpolation preserves —
which the room case proves it often does not.

**Therefore `value <= 0 along the segment` cannot be made reliable here.** Not by
smaller steps, denser sampling or a larger tolerance. Tracing, addressing and
acquisition are all fine; offline traces reproduce the native `closestApproach`
values exactly and the geometry is present as negative texels.

Two repairs are ruled out with evidence, not argument:

- **Counting solid texels beside the path** — refuted on the controlled same-source
  A/B: blocked pose touched 4, the three exposed poses touched 3. A room always has
  walls and a floor nearby.
- **Raising the hit tolerance** — it looks tempting because within the traced range
  the room separates by a factor of thirty (blocked 0.005/0.016, clear 0.529/0.517 at
  the clamp), but that is fitted to one room. Note the regression table's 0.160 and
  0.125 for the clear controls are **complete-path** minima landing on the lights' own
  housings inside the end margin; `left-3.0` has length 7.733 with its minimum at
  exactly 7.733.

Still worth fixing separately: the forced 0.05-gu minimum step skipped a genuine
negative interval on `near-box-b` (dense sampling finds −0.0073 at t = 13.25 against a
traced verdict of clear). A sphere trace is only valid stepping by at most the sampled
distance. It recovers that one case and no other.

## The decision that blocks everything

A swept query — "does anything come within radius r of the segment" — is the only
remaining candidate on this data, because it uses the distance rather than relying on
the sign surviving interpolation. Checked at the room minima: both blocked paths have
their nearest surface to the side, not the floor, so it would not merely be detecting
ground. It is untested and inherently conservative.

**Before building it, the product question should be answered:** the consumer is a set
of Hue lamps, not a renderer. Does it need per-source geometric truth, or roughly how
much light reaches the player and from where — which the working ambient feed already
answers? The user has not yet been asked this. Asking is cheaper than building for
either answer.

## Untested and waiting

`v2.1.12-fire-visibility.2` is built, scanned (ASI 4/71, ZIP 0/68) and **never
installed**. Its exact next test is in Codex's own checkpoint below and has not been
performed: one known fire, `visible -> blocked -> visible`, F11 in show-blocked mode
first, then rotate the camera without moving to confirm direction does not change the
verdict. Do not claim candles, lamps or the all-source requirement from one pass.

## Housekeeping, recorded not done

26 untracked throwaway files sit in the repository root (`capture_aba*.py`,
`parse_aba*.py`, `vt_*.py`, `test.cpp`, `debug_test.cpp`, `set_ini.py`, `temp_zip/`)
plus `native/.../src/spatial_readback_stub.cpp`, which is in no CMake list. They are
not mine and may hold the only copy of an analysis, so they were left alone.

## One next step

Ask the user how much per-light fidelity the lighting product actually needs. If the
answer is "geometric truth per source", the swept query needs a live controlled test
with `r` chosen on physical grounds. If "a conservative approximation is fine", the
existing narrow fire path plus ambient may already suffice and the remaining work is
in the consumer, not the renderer.

---

# Previous checkpoint — narrow rendered/fire visibility restored, 2026-09-13 late, Codex

## User decision and scope

The user ended the feature pause after 2.1.11 publication and asked for the earlier
live-working fire visibility behavior back **without new renderer research**. This
checkpoint restores only that small production path. Do not treat it as completion
of the all-light/player goal, and do not replace it with the broader research bridge
unless a later controlled failure requires that work.

Public 2.1.11 remains unchanged on Nexus. A matching GitHub release now also exists:
<https://github.com/fabianviol/CrimsonDesertTelemetry/releases/tag/v2.1.11>.
Its tag points to stable commit `a8c5ed87925a688532b50b00102d90a46ee69d46`
and its uploaded asset is the exact existing stable ZIP, SHA-256
`CB7DD68FFAA548A53F2A5303870C012F9CD0CC44944A46A0F59BF274457C5052`.
The new visibility candidate below has **not** been published there or on Nexus.

## Concrete production restoration

`CDT_RESEARCH=OFF` now:

- compiles `sdf_acquire.cpp`, which retains one fenced current R16 SDF volume;
- links `sdf_visibility.cpp` as `cdt_sdf_fire` with the exact earlier Variant A
  classifier: 0.6 start skip, 1.0 end margin, `max(distance, 0.05)` steps,
  nonpositive sample = blocked, at most 400 iterations;
- classifies only current renderer-selected records in `render_bridge.cpp`, from
  their capture-paired camera position. Camera direction and projection are not
  inputs, so a retained behind-camera record is still eligible;
- reads `[SourceVisibility] Enabled`, publishes additive metadata and restores
  `HideOccluded` plus configurable/default F11 in both light views;
- leaves raw and smoothed records, positions and RGB unchanged. Unknown/stale
  records remain displayed even when hiding is active.

The host creates the broad managed player/all-known-source query mapping only when
the ON ASI passes its private `--research-source-visibility` flag. OFF does not run
that client or compile the native result bridge, history, diagnostic series, dump
or trace infrastructure.
This is the same architectural family that previously produced working fire
verdicts; candles, lamps, authored-only sources and a complete 360-degree registry
are not claimed.

## Exact candidate and verification

Private package:

`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.12-fire-visibility.2-ModManagers.zip`

Expanded folder:

`artifacts/mod-manager/v2.1.12-fire-visibility.2-20260913-235738-037-8b9b65b8/CrimsonDesertTelemetry`

| Exact file | SHA-256 | VirusTotal | Microsoft Defender |
| --- | --- | --- | --- |
| ASI | `03989712E34BF418BB326A835A998CAC206863775D8EB182BA98A2C55ADAC1D7` | **4/71**: CrowdStrike, Cynet, McAfeeD, Microsoft | No matching detection |
| ZIP | `B2CF14D58F9F31F6238C617D77B66499CE13BCF3D88A55C12170CF1CA719C0BD` | **0/68** | No matching detection |

The VT result is equal to/better than the immediate v2.1.9 predecessor (4/71) and
earlier narrow visibility candidates (5/70–5/71, ZIP 0/67–0/68). It does **not**
reproduce the later broad research-bridge regression (ASI 11/71, ZIP 7/68).
This directly answers the user's AV question: restoring the narrow path did not
damage the measured scan profile. The known deterministic Bitdefender family came
from commit `6937fa9`'s bounded repeated diagnostic readback series, which remains
compiled out of OFF; see `ANTIVIRUS_FINDINGS.md`.

Verification completed against this production build:

- all **30/30 native CTests** pass;
- all **71/71 managed tests** pass;
- the new `release-fire-visibility` test covers first-volume waiting, clear/blocked
  fields, sources in front of and behind the camera, disabling, and byte-for-byte
  raw scene/light/counter preservation;
- the production package validator accepts all **34** offered settings, rejects
  research-only settings, verifies all nine files and verifies ZIP/staging equality.

## One next step

With the game closed, install the exact private ZIP above cleanly. Use one known
fire and perform `visible -> solid wall/building blocked -> visible`, keeping F11 in
show-blocked mode first so wrong verdicts cannot disappear. Rotate away if useful:
camera direction must not change the verdict while the renderer record remains in
the feed. If that passes, repeat with F11 hiding, record the live result here and
decide whether this limited fire restoration is releasable. Do not claim candles,
lamps or the final all-source product requirement from that one pass.

---

# Previous checkpoint — 2.1.11 PUBLISHED, 2026-09-13 evening, Claude

> Supersedes the "One next step" of the 18:46 checkpoint below, which still asked for
> external validation BEFORE publishing. The user overrode that and released.

## What changed after 18:46

**`v2.1.11` is live on Nexus.** Published 19:54, verified through the authenticated
API: `2.1.11 main 09/13/2026 19:54:00 id=38521561693168`, with **2.1.10 archived** at
the user's explicit instruction and the page version moved. Changelog appended from
`docs/releases/v2.1.11.md`. The exact ZIP `CB7DD68F…5052` and ASI `06EE760E…1B63`
were re-hashed from the published archive itself before upload, not trusted from the
documentation.

**The publish tool was broken and is fixed (`380eaa9`).** The first attempt died with
a bare 403 on the presigned storage PUT. `tools/nexus/NexusMods.psm1` deliberately
left `Content-Type` unset, with a comment claiming it is not signed. That comment was
wrong. Reading the storage provider's own error body showed
`X-Amz-SignedHeaders=content-disposition;content-md5;content-type;host`, and Cloudflare
R2 rebuilding the canonical request with an empty content-type. Nexus signs for
`application/octet-stream`; `application/zip` is rejected with the same 403. The lesson
worth keeping: a presigned PUT's 403 body carries the whole diagnosis, and the module
was discarding it.

**`docs/NEXUS_DESCRIPTION.bbcode` corrected in two places.** Its "Validation and
integrity" section still said the package needed the live second-monitor/HDR
replacement test -- that ran today and passed, so it now states the live validation
actually performed and leaves external confirmation as the only open item. And "What
the measurements mean" now says the linear HDR values are unclamped, routinely exceed
1.0 and are not 0-255 colours; the owner asked exactly that question, so users will.
Everything else of Codex's 2.1.11 rewrite is untouched, including the schema 1.5
statement, which matches `lightsSchemaVersion = "1.5"` in Program.cs.

## New evidence: a local crash the fix did NOT prevent

At about 19:20 the game ended after 2 h 15 min with 2.1.11 installed, following an
HDR on -> off change. **Telemetry's replacement path had completed successfully**
moments earlier -- `overlay.log` ends at 19:19:51 with the full sequence including
2.1.11's new `Existing HUD swapchain released for replacement.` line, then
`Overlay ready`. No `CreateSwapChainForHwnd` failure, no `E_ACCESSDENIED`, no
exception in any Telemetry log. Logs preserved under
`artifacts/crash-reports/local-20260913-1919-hdr-off/` with a CONTEXT.txt stating
what they do and do not establish.

**There is no dump for it.** The game's own handler did not write `last_crash.dmp`,
and Windows Error Reporting was configured per-application only, without
`CrimsonDesert.exe`. So the process vanished without either mechanism catching it,
which is itself a difference from the 16:17 crash that did produce one.

**WER is now armed** (user ran it elevated, verified):
`HKLM\...\Windows Error Reporting\LocalDumps\CrimsonDesert.exe`, DumpFolder
`C:\CrashDumps`, DumpType 2 (full), DumpCount 10; the folder exists. The next
unhandled crash produces a full dump automatically. This does not cover a hard
termination or a driver-level TDR.

**Do not merge this with the users' crash.** Codex's earlier warning was right and I
briefly violated it. The user then raised the decisive point: their machine now has a
**two-monitor extended setup with HDR on one screen**, and reaching the Windows
display settings requires leaving the game. Both local crashes happened during an
output change in that configuration. The users' crashes are at **startup**, with a
different signature. Whether the two overlap is unknown -- nobody has asked the users
how many monitors they run.

Retests tonight after the crash, all passing, all with 2.1.11: HDR switch at the main
menu (no HUD swapchain to release, so the new path did not even run), then HDR switch
in game (the release path did run, logged cleanly, no crash). So the crash is
**intermittent**, matching jimos87's report of one good run followed by a bad one. The
only obvious difference in the crashing run is uptime: 2 h 15 min versus ~20 min.

## Replies drafted for both Nexus users

Full text given to the user for posting, not yet posted. Both credit the specific
evidence that made the diagnosis possible -- WHOLE's Streamline log and exception
offset, jimos87's minidump and his observation of intermittency -- state plainly that
the cause was ours, and explicitly do NOT claim the crash is fixed for them. Both ask
for three consecutive starts and, for the first time, **how many monitors they run and
whether HDR is active**. jimos87's additionally covers the DMM leftover cleanup.

## Verified machine state

Installed and running 2.1.11. Five of the six deployed files match the published ZIP
byte for byte; only `crimson-desert-telemetry.deps.cfg` differs, carrying older
version labels with identical runtime assets -- the known DMM behaviour, now measured
rather than assumed.

## One next step

Post the two replies and wait for an affected user. Until one of them reports three
clean starts, 2.1.11 is a diagnosed and locally verified fix, not a closed report --
that wording is in the description and the changelog and should not be tightened
without their evidence. If a local crash recurs, `C:\CrashDumps` will now hold a full
dump: compare its exception offset and loaded modules against the two known cases
(`+0x3D0FEA6` external startup, `+0x3D05303` local runtime) before touching code.

---

# Previous checkpoint — Nexus crash compatibility, 2026-09-13 18:46, Codex

> This section supersedes the older source-visibility checkpoint below. The user has
> explicitly paused all occlusion research until the current Nexus crashes are fixed.

## Current external evidence

The Nexus posts page contained nine comments when checked on 2026-09-13. In
chronological order:

- jimos87, 2026-09-10 11:40: initial crash report.
- WHOLE, 2026-09-12 06:45: 2.0.2 package/deployment report and graphics-feature
  crash matrix; backend-only operation worked.
- fabianviol, 2026-09-12 10:51: replied to both threads, acknowledged the
  failures and requested native log/crash evidence from WHOLE.
- jimos87, 2026-09-12 17:18: current download still crashed.
- fabianviol, 2026-09-12 19:38: requested a clean 2.1.10 install and exact logs.
- fabianviol, 2026-09-12 19:40: announced 2.1.10 and requested the same controls.
- **WHOLE, 2026-09-13 05:04 (current):** build 25246367, RTX 4090, NVIDIA
  616.92, all six files copied manually, reported overlays off. Streamline
  DLSS/DLSS-RR 2.11.1 logs `linkSwapchainToCmdQueue` /
  `CreateSwapChainForHwnd` failure `0x80070005 (E_ACCESSDENIED)`, followed about
  one second later by `0xC0000005` at `CrimsonDesert.exe+0x3D0FEA6`.
- **jimos87, 2026-09-13 08:49 (current):** 2.1.10 sometimes reaches the game but
  still fails; supplied a DMM support package:
  <https://drive.proton.me/urls/V1AX5VHNV0#sIhaw6yiTPQJ>.

The support ZIP is preserved read-only under
`%TEMP%/nexus3374-support-jimos87-20260913`; no downloaded binary was executed.
Its SHA-256 is
`6062BD48244BE56220C75C14EED06FE55D66070BC0B3A0DF186DE44CA242DBE0`.
It proves DMM 2.8.1, RTX 4080 SUPER, driver 616.92, game build 25246367 and an
intermittent pattern: one enabled run exited normally, while the next enabled run
logged the same Streamline `E_ACCESSDENIED` and crashed.

The minidump proves the loaded Telemetry ASI is the exact public 2.1.10 build:
PE timestamp `0x6AA59B26`, mapped size `0x17C000`, matching local SHA-256
`8028E9EE43E846F79075618F9B7A522F5F66E2EB1784FFB48AF178AAFC0C0A78`.
It also contains Streamline, Steam's `gameoverlayrenderer64.dll`,
`DesertLinkCore_v1.0.7.asi` and `MasterLooter.asi`. The exception is the same
`CrimsonDesert.exe+0x3D0FEA6` reported independently by WHOLE. This is no longer
an unverified stale-file or packaging hypothesis.

The owner, WHOLE and jimos87 all used NVIDIA driver **616.92** in the relevant
tests (RTX 3080, 4090 and 4080 SUPER respectively). Parsing jimos87's exception
context against the exact local game executable (matching PE timestamp
`0x6AA22ABB` and image size
`0x16B0E000`) proves that the faulting instruction is `mov rax, [rcx]` with
`rcx == 0`. The crash thread's retained stack contains CrimsonDesert, `sl.common`,
`sl.reflex`, `sl.interposer` and `nvwgf2umx`, but no Telemetry frame. This supports
the sequence shown by the game log: swapchain creation fails first, then the game
dereferences its missing graphics object. It does not by itself absolve Telemetry
of causing the preceding swapchain failure.

Disassembly of the exact game image now identifies that missing object precisely.
The fault is in the routine beginning at RVA `0x3D0FE00`; it loads the member at
`[rbx+0xA8]` and calls vtable offset `0x120`, which is method slot 36,
`IDXGISwapChain3::GetCurrentBackBufferIndex`. The same member is used earlier with
vtable offset `0x50` and fullscreen arguments, matching
`IDXGISwapChain::SetFullscreenState`. Therefore the access violation is the game's
direct null dereference of its swapchain member after Streamline reported that the
swapchain could not be created/linked. HDR is not implicated: WHOLE reproduced the
same failure in SDR.

The exact shipped `sl.dlss_g.dll` (SHA-256
`DAE3F24A690F3DEE3FBD89BDDE47FBEB6BF480CEE376ECFD0242A50E0BB6E6C6`) was also
disassembled. The `linkSwapchainToCmdQueue` error path at DLL RVAs
`0x4B735`/`0x4BC31` logs the negative HRESULT returned directly by its internal
factory `CreateSwapChain`/`CreateSwapChainForHwnd` call. Combined with the official
v2.11.1 factory order, this places the failure in DLFG's post-create linking work:
the base swapchain has been returned, but the Streamline after-hook has not finished.
That is the precise interval in which 2.1.10 installed five shared swapchain-method
hooks and which 2.1.11 now leaves untouched. The external startup failure remains
distinct from the owner's later runtime display-transition crash until affected-user
tests establish whether both were fixed.

## First concrete defect and narrow fix

`overlay_graphics.cpp` hooked Present/Present1/Resize/SetColorSpace synchronously
inside the real DXGI `CreateSwapChainForHwnd` detour. At that point the inner DXGI
call has returned, but NVIDIA Streamline's outer wrapper has not yet associated the
new swapchain with its command queue. The code therefore modified the swapchain
implementation during Streamline's still-active creation path, exactly before its
`linkSwapchainToCmdQueue` failure.

This ordering is confirmed by NVIDIA's official Streamline **v2.11.1** source
(tag commit `019994e18d256a3e92347888deb527feb7f58bc0`), the exact Streamline version
reported by both users. Its `IDXGIFactory2_CreateSwapChainForHwnd` calls the base
factory first, then invokes Streamline's after-hooks, and only afterward calls
`setupSwapchainProxy`. Telemetry 2.1.10 ran `Track()` while that base call was
returning, so its five swapchain hooks were installed before those two Streamline
steps. The deferred tracking therefore removes a demonstrated ordering violation rather
than merely tuning a timeout after the crash.

The first rc.2 attempt still retained the newly returned swapchain in a pending
`ComPtr`. Microsoft documents that a flip-model HWND can have only one swapchain;
keeping the old one alive can make replacement creation fail. The owner then
reproduced that exact DXGI failure locally at 16:17:07 by switching the game's
output/HDR state for a newly connected second monitor. The game logged
`CreateSwapChainForHwnd failed: -2147024891` twice and crashed with `0xC0000005`
at `CrimsonDesert.exe+0x3D05303`. This is a runtime replacement path and a different
downstream crash routine from the users' startup RVA `0x3D0FEA6`; do not merge the
two acceptance claims.

The local DMM support ZIP and its 17,409,728-byte minidump are preserved under the
Git-ignored `artifacts/crash-reports/local-20260913-161707`; dump SHA-256 is
`290587A6193989B6AE8D8E23A4A7DF71BB7774498D3A9A59F3CF178310D2DF68`.
Disassembly of the local fault shows a null/invalid structure at `mov rsi,[rax+30h]`
inside the game's swapchain/backbuffer rebuild routine, not the external users'
`GetCurrentBackBufferIndex` null call.

The final narrow fix records each pending HWND and, before either hooked DXGI
factory create function runs, releases only Telemetry's pending or active
swapchain/renderer/queue references for that same HWND. It waits for submitted HUD
GPU work before releasing active resources. The worker still waits 500 ms before
installing presentation hooks. No research hook, tracing system or new production
feature was added.

The completed fix is commit `a8c5ed8` (`Release swapchain replacement compatibility
fix`). It is pushed to both `origin/main` and
`origin/codex/streamline-swapchain-fix`; local `main` is at the same commit. The
implementation was prepared in `C:/DEV/CrimsonDesertTelemetry-hotfix`. Paused
source-visibility commit `f750974` is preserved on branch
`codex/source-visibility-paused` and was not included.

Exact final-version package used for the local acceptance test:

- `artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.11-ModManagers.zip`
- production `CDT_RESEARCH=OFF` ASI: 1,276,416 bytes, SHA-256
  `06EE760E33E499252AF072711C28D55D2D65127E50B98A3E4FD26B39ADEC1B63`
- ZIP: 800,147 bytes, SHA-256
  `CB7DD68FFAA548A53F2A5303870C012F9CD0CC44944A46A0F59BF274457C5052`

Build, 71 managed controls, 30 native CTests and package self-test pass. All eight
D3D12 HUD/notification/light-marker SDR/scRGB smoke modes now create a replacement
immediately while the first chain is pending and again after the HUD accepted the
next chain. Microsoft Defender found no threats in either exact 2.1.11 file.
VirusTotal reports 0/68 for the exact ZIP and 4/71 for the exact ASI. The ASI
result is not a regression from public 2.1.10's 5/71 result. Local Defender found
no threat in either exact file.
The source and release documentation are on GitHub. Before telling users that
2.1.11 is available on Nexus, verify the Nexus file entry itself; the last
independently confirmed public Nexus binary in this checkpoint was 2.1.10.
Repeated starts by an affected 616.92 user are still required before calling the
external startup crash fixed because their evidence proves the old failure is
intermittent.

### Local live evidence

On 2026-09-13 at 15:03, the owner launched the exact rc.2 ASI on an RTX 3080 with
NVIDIA driver 616.92. That superseded ASI's SHA-256 was
95D733491FB8C09CCD7D80C5800F87C6379BFD6B178299C1902D6F0F89541EEF. The
overlay log records the intended sequence:

```text
Waiting for the game's D3D12 swapchain.
Game D3D12 swapchain created; waiting for graphics wrappers to finish.
Overlay ready: D3D12 / SDR.
```

After five minutes the game process was still responsive. Native light capture
was armed after the playable-world signal, recurring rendered-light capture was
ready, `/v1/health` reported `playing`, and player/camera, authored lights,
rendered lights and Ambient were all live. This is the first successful cold-start
control on the affected driver. At 16:17 the later monitor/HDR switch reproduced
the separate runtime replacement crash described above.

The owner then installed the exact final 2.1.11 production package and started the
game at 17:04 on the same RTX 3080 / NVIDIA 616.92 system. The installed ASI hash
was verified as
`06EE760E33E499252AF072711C28D55D2D65127E50B98A3E4FD26B39ADEC1B63`, exactly
matching the final package. At about 17:16 the owner repeated the same output/HDR
switch to the connected TV which had caused the 16:17 crash. The game remained
responsive. `CrimsonDesertTelemetry.overlay.log` recorded a second complete
replacement sequence:

```text
Game D3D12 swapchain created; waiting for graphics wrappers to finish.
Overlay ready: D3D12 / SDR.
```

The current game log contained no `CreateSwapChainForHwnd`, `E_ACCESSDENIED`, crash
or exception entry after the transition, and `/v1/ambient` continued returning
fresh data on port 27311. This is a passed one-way local reproduction test of the
runtime replacement crash. The reverse switch to the original output was requested
but had not been performed when the user asked for this handover. External RTX
4080/4090 startup acceptance remains separate and decisive for the Nexus reports.

## One next step

If desired, complete the local symmetry check by switching once back to the original
display and confirming another `Overlay ready` line without a game-log swapchain
error. Do not delay external validation for that check. Send the exact unchanged
2.1.11 ZIP and hashes above to at least one of the two affected users for three clean
starts with Overlay, Notifications, Lights and LightOverlay enabled. One
affected user reproducing three successful starts is enough for the first external
acceptance gate; the second user is useful confirmation, not a prerequisite for
learning whether the ordering fix works. Ask whether the HUD/markers initialize and
whether the Streamline `E_ACCESSDENIED` remains. If a run still crashes, preserve the
new game log and dump before changing code; compare the exception offset and loaded
modules to this checkpoint.

---

# Previous checkpoint — per-light direct visibility, 2026-09-12 (paused)

## Do not confuse the Nexus release with the research package

The public Nexus release is still the unchanged **2.1.10** production package,
uploaded on 2026-09-12 at 21:28. Nothing built after that upload was sent to
Nexus. The authenticated Nexus API confirms 2.1.10 is the active main version.

Exact published 2.1.10 artifacts:

| File | Bytes | SHA-256 | VirusTotal |
| --- | ---: | --- | --- |
| production ASI | 1,267,712 | `8028E9EE43E846F79075618F9B7A522F5F66E2EB1784FFB48AF178AAFC0C0A78` | 5/71 |
| production ZIP | 791,357 | `1A8DD7641E9431AA0FE383E68706D9519A0B93E2ED5E6A70129C4C4552DEBD24` | 0/68 |

Nexus reports that public file as safe. Do not remove, replace, warn about or
otherwise change the Nexus 2.1.10 release because of the later research scan.

The higher **ASI 11/71 / ZIP 7/68** results belong exclusively to the superseded
local `2.1.11-source-visibility.2` `CDT_RESEARCH=ON` package built around 23:18.
That package adds SDF readback, a player-to-light IPC bridge and other research
instrumentation. It was never uploaded to Nexus and must remain private. GitHub
`main` contains its source and documentation for continued development; this does
not alter the immutable production binary already hosted on Nexus.

## Previous priority (paused)

Focus only on this question for each local light already known to the project:

```text
Does solid world geometry block the direct segment from the player to this source?
```

Camera direction, on-screen state, Ambient, image brightness and bounced light are
outside this test. Fire, torch, candle, lamp, point and spot sources use the same
geometry rule. Stable release 2.1.10 and its hotfix/packaging work are complete;
do not reopen them unless they directly block this task. Do not work on CrimsonHue.

The production goal still requires both Ambient Occlusion and per-light Source
Occlusion in `CDT_RESEARCH=OFF`. Ambient already passed its controlled
open -> enclosed -> open test. Per-light visibility is not complete until research
and production each pass controlled live visible -> blocked -> visible tests,
including a retained source behind or outside the camera view.

## Current implementation

The previous camera-origin, renderer-only zero-crossing trace was rejected for two
measured reasons: it did not cover all known sources or the requested player
receiver, and filtered t233 wall samples can remain slightly positive.

The new research candidate consists of:

- `SourceVisibilityClient`: publishes player position + `(0,1,0)` and the spatially
  deduplicated union of authored and rendered light positions;
- `source_visibility_bridge`: a separate seqlocked native query/result mapping;
- `sdf_visibility`: quarter-cell segment sampling, near-surface threshold
  `0.5 * cellSize`, and blocked after one complete cell of connected samples;
- schema 1.5 metadata on both authored and rendered records, position-matched back
  without changing positions, RGB, raw records or smoothed contributions.

No camera direction or projection exists in the new bridge. The native worker
traces every new query against one immutable SDF volume. Unknown/stale results do
not hide or attenuate API records. The old render-capture visibility writer is
explicitly disabled while this research bridge is active.

The bridge is bounded to the nearest 256 distinct source positions, merges within
0.1 game unit, rejects a response after the player receiver moved more than
0.75 game unit, and expires results after 1500 ms. Sources absent from both input
feeds cannot be classified and must never be called blocked.

Implementation and API details: [SOURCE_VISIBILITY.md](SOURCE_VISIBILITY.md).
Preserved live failures/captures: [SOURCE_VISIBILITY_REGRESSION.md](SOURCE_VISIBILITY_REGRESSION.md).

## Evidence before live test

The exact native classifier reproduces all 17 preserved labelled controls:

- room: two blocked and two clear sources;
- same room source: three fresh clear-pose captures;
- Serkis: clear -> blocked -> clear;
- Warspike: two clear -> three blocked -> two clear.

This is offline replay of real retained volumes, not live acceptance. Relevant
automated checks pass:

- complete managed test executable, including the schema 1.5 player/query bridge;
- native SDF clear/blocked/freshness/context controls;
- native direction-independent query/result bridge;
- 384 WARP/debug-layer readback controls;
- research ASI compile and expanded/ZIP package validation.

The JSON schema parses successfully. `AGENTS.md` and `CLAUDE.md` are byte-identical.
The first `.2` game start exposed one compatibility defect: the managed host
correctly emitted new schema 1.5, but the ASI HUD parser still accepted only
through 1.4 and therefore labelled the entire stream incompatible. `.3` adds 1.5
to that strict allowlist and its additive-schema HUD regression. This was not an
ASI/DLL file mix and has no effect on Nexus production 2.1.10.

## Exact package for the next tester

Use only:

`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.11-source-visibility.3-ModManagers.zip`

This is a private `CDT_RESEARCH=ON` package for Steam build 25246367, not a public
release. It contains its own research README and a ready test INI:

- `SourceVisibility.Enabled=1`
- `Overlay.ShowDetails=1`
- `LightOverlay.HideOccluded=0` so wrong verdicts remain visible
- `Research.SignedDistanceReadbackIntervalMs=1000`
- up to 120 automatically started SDF transactions

Exact artifacts:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `CrimsonDesertTelemetry.asi` | 1,591,296 | `7C9B007E8DA4F73FDAF8047F5E9C74478E70005A226EBB82D6C6D9B2A37CCC02` |
| test ZIP | 943,190 | `96AB18A4E9BFF21694F9173CFB50A41F1CCE83A81405A3807DDDA8ED3FD7112E` |

Local Microsoft Defender custom scans passed both exact files with no threats.
Completed VirusTotal results for these exact `.3` research artifacts are
**ASI 11/71** and **ZIP 7/68** (the ZIP has 60 undetected and one analysis
failure; the ASI has one timeout and one failure). This is a
significant regression from the stable production build and blocks using this
architecture as a release candidate. It does not invalidate a private functional
test, but it must not be published or promoted to `CDT_RESEARCH=OFF` unchanged.
Investigate it later as a product/build regression without obfuscation or signature
gaming. The earlier `.1` and schema-incompatible `.2` artifacts are superseded
and must not be tested.

## One next step

With the game closed, install the `.3` ZIP cleanly through DMM. In the known room,
hold one player position where two identified sources are physically clear and two
are behind solid wood. Keep F11 in show-blocked mode. Verify that all four records
receive the correct status. Rotate the camera without moving the player and verify
retained verdicts do not change. Then perform visible -> blocked -> visible on one
identified fire/candle/lamp.

If the live result fails, capture the exact source positions, player position,
verdicts, `closestApproach`, `volumeSequence`, `contextFrame` and ages before tuning
anything. If it passes across relevant source types and behind-camera coverage,
move only the minimal bridge/classifier/acquisition pieces into `CDT_RESEARCH=OFF`
and repeat the same production test.

## Worktree care

Do not stage the unrelated root-level capture/VT helper scripts, JSON outputs or
scratch C++ files already present as untracked evidence. Do not delete prior
artifacts. Commit only the explicit implementation, tests, package tooling and
documentation for this candidate.
