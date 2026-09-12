# Current checkpoint — production occlusion, 2026-09-12, Codex

## ACTIVE PRIORITY: stable release of working features, occlusion work paused

The user explicitly changed scope on 2026-09-12: pause further per-light/sky-HUD
investigation and prepare a release build of what works now. Remove nonworking
options from the offered production INI; verify the remaining functions together
with every offered feature enabled, complete DMM deployment, startup/game stability,
and VirusTotal on the exact ASI and ZIP. Do not resume occlusion research until this
release work is handled. This overrides the earlier requirement to wait for both
occlusion goals before releasing any update; those remain the longer-term goals.
No consumer work. No unsupported claim that the external Nexus 2.0.2 crash is fixed.

Source visibility and its F11/HideOccluded controls are excluded from this release,
preserved for later development. Ambient's accepted route remains available;
its camera-sky wording/orientation complaint is pending, not silently declared fixed.
Planned release scope: raw/EMA lights, player/render camera, Ambient, HUD/radar,
markers, notices and the configurable F8–F10 controls. Full supported-feature
startup/live checks and exact artifact scans are required before publication.

Pause backup: `artifacts/recovery/occlusion-pause-20260912-165120/` contains the
uncommitted patch and inherited untracked files. No source/research/evidence was
deleted. The room's four labelled sources and three fresh free-pose volumes are
documented below and in SOURCE_VISIBILITY_REGRESSION.md. The requested return to
`verdeckt` was cancelled by this scope change; do not infer a completed return test.

**Final 2.1.10 package prepared, explicitly OFF.** The 31-key production INI enables
all offered boolean features. Source acquisition/trace is excluded at link/compile
time; raw publication remains intact with disabled additive metadata. Research ON
build/model and its original INI are preserved. 69 managed tests, API smoke,
27 selected native tests, all-UI SDR/scRGB and exact package validation pass.
The two existing research readback test failures are excluded, not fixed.
Packaged bootstrap passes in an isolated non-game process with all switches on.
Exact VT: ASI5/70, ZIP0/67, same five detecting vendors as .4 (Microsoft now C!ml).
Local Defender custom scans find no threats in either file; that is not a clean VT
ASI verdict. Final ZIP SHA256 is
`1A8DD7641E9431AA0FE383E68706D9519A0B93E2ED5E6A70129C4C4552DEBD24`;
ASI SHA256 is
`8028E9EE43E846F79075618F9B7A522F5F66E2EB1784FFB48AF178AAFC0C0A78`.
Nexus publication is authorized after one final user-run DMM/game acceptance.
Exact final scans: VirusTotal ASI5/71 with the same five detecting vendors as rc.2,
ZIP0/66; local Defender exit0 for both exact files with unchanged hashes.
**First candidate game run verified:** PID7260, started2026-09-12 19:26:27 CEST.
ASI, both DLLs, INI and runtimeconfig.cfg match the candidate; deps.cfg remains
2.1.8 metadata with identical runtime assets (only version labels differ).
Two 30-second captures have3596/3596 raw and3597/3597 smoothed/ambient available
messages, no empty light frames or malformed records, and33816 matching original
contributions in the smoothed feed. User confirms F8/F9/F10 toggling and menu entry/
exit work without flicker/crash. Automatic menu hiding is explicitly cancelled:
do not investigate it further. The debug console remains excluded from OFF.
Candidate Ambient route now PASS:8seconds per user-labelled phase, mean exposure
0.477526 outside ->0 indoors ->0.490643 outside,1442/1442 available ambient samples
with progressing sky/visibility frames. User then exited and deleted the package
through DMM; both game/host are gone and port27311 is free. DMM removed ASI/INI/logs
but left both DLLs and both CFG files. Reimport/activation repeats the deps.cfg
mismatch: DMM library6/6 correct, bin64 still5/6 exact. User confirms DMM1.9.4 and
requires proper removal of old package files. Official DMM2.7 notes include ASI
package/update fixes; current page lists2.8.1. User has been asked to update DMM
while the game stays closed, then repeat deployment/removal before the second
game start. No direct game/DMM writes were made by Codex. See
[stable release validation](STABLE_RELEASE_VALIDATION.md) for evidence.
After NVIDIA driver32.0.16.1692 installation, the first rc.1 start rebuilt shaders
and hit capture error258/WAIT_TIMEOUT during the long initial load; the second did
not reproduce it. The owner reports repeated new shader compilations throughout
Telemetry development and attributes them to the mod. A later start thought to be
unmodded also compiled shaders, but deployed files were not verified, so it does
not isolate the trigger. Final2.1.10 adds a one-shot host `playing` gate before native
light/sky capture, separates the60-second command-list submission bound from the
5-second submitted GPU-fence bound, and logs the stage. 70/70 managed and27/27
selected native controls pass. **Next step is final-package live acceptance and
publication, not the paused source experiment below.**

## Mandatory product goal and working rules

Production `CDT_RESEARCH=OFF` is complete only after BOTH controlled live tests
pass on supported build 25246367:

1. Local Ambient / Sky Occlusion: open -> enclosed -> open, with a clear and
   reversible difference under a solid roof/in a building/cave.
2. Per-light Source Occlusion: camera/player -> individual fire/lamp, visible ->
   blocked by geometry -> visible. Include off-screen/behind-camera coverage;
   screen-space visibility alone is insufficient.

Preserve raw and smoothed light records. Visibility is additional metadata, never
silent removal/filtering. Do not touch CrimsonHue until both goals pass. Reuse the
preserved SDF findings; no new PIX/renderer or AV research without a concrete need.
Keep production minimal; research instrumentation/history stays in the ON path.
Scan the EXACT final production ASI and release ZIP before release. A significant
new AV regression blocks release; investigate the product/build, never obfuscate
or game signatures. The user's earlier 0/75 result is a historical baseline, not
an assurance about a new binary.

**User controls the game and installs all packages through DMM. No Computer Use,
no direct installation or edits in the game directory.** Prepare configured ZIPs
and give one short, concrete manual step at a time.

User additionally requests ongoing virus checks of each new ASI AND DMM ZIP,
an optional HUD/radar mode that hides freshly blocked lights, and configurable
or individually disabled shortcuts for all four HUD functions (defaults F8-F11).
Update all current product descriptions. Commit and push completed changes to
GitHub. The user's latest request authorizes working-feature publication after
release acceptance; no GitHub release/tag has been created during preparation.

The user additionally requires all offered INI functions to be tested individually
and together; unsupported/broken options must be documented, rejected or removed
from the product profile. See `docs/INI_VALIDATION.md` for all 60 original options,
the original 34-setting intermediate profile and current 31-setting release profile,
concrete exclusions and the still-pending live matrix.
Automatic HUD/marker hiding in menus was explicitly cancelled by the user after
the first rc.1 game test. It is not implemented and must not be pursued further.
`playing` remains telemetry availability, not a menu-state contract.

## Current result — Ambient Occlusion complete; per-light production in progress

**Current focused test, `.4` installed by the user through DMM:** PID 34380.
The code DLLs, ASI and INI match `.4`; DMM still retained the old deps.cfg version
labels with unchanged runtime assets. A stationary 12-second control has 720/720
available raw messages and 24 progressing SDF volumes. The user's room screenshot
identifies four sources, two visible and two blocked; all four remain
CLEAR in all 720 messages. The user identifies the first and third physical markers
from the left as blocked: top label 11.9 gu and bottom label 3.6 gu. The hanging
point light (3.0 gu) and right spot (4.2 gu) are the visible controls.
A fresh retained CPU volume 1970/context 55044 is paired with API
capture 15359, and all four offline trace minima reproduce the live values.
Even minima of the complete piecewise trilinear paths remain positive, so neither
HUD filtering nor the known forced-step skip explains this particular captured
case. This does not yet establish whether the relevant geometry is absent or
represented incorrectly. Evidence and limits are in the new `.4` room section of
[live regression](SOURCE_VISIBILITY_REGRESSION.md).

The user then exposed the first/top source and confirmed `frei`. Three fresh paired
volumes 3096/3098/3100 reproduce its CLEAR verdict, with sampled minima
.489167/.492633/.493895 versus .012111 in the original blocked pose. Captures are
under `room-first-source-visible-01/` beside the room evidence. The user requires
the camera position/orientation as reference; the existing trace uses the paired
camera position, with orientation used for HUD projection, not an off-screen veto.

**Paused step, do not resume during release work:** the user had been asked to conceal that SAME first/top source again
and report `verdeckt`; capture three fresh paired volumes once confirmed. No
trace threshold or production code was changed for this diagnosis.
Keep the user's later camera-sky-visibility wording/orientation complaint queued
behind this source test, as explicitly requested. No consumer or new AV research.

**Latest live result: the user's `.3` camp test FAILED per-light recognition and
light-view continuity.** Opaque boxes/structures conceal lamps labelled visible;
the open fire is excluded from that report. A 20-second capture proves 18/1,197
raw and smoothed frames unavailable with `bridge-changing` while playing. Metadata
tracing under the native write sequence caused these shared HUD/radar gaps; its
preparation has been moved before atomic publication and a concurrency regression
passes. A separately proven normalized-sampling half-texel fix is also tested, but
does not solve all camp false clears. The SDF later stopped at volume 1853;
read-only live state is Ready/no failure with progressing GI and a frozen original
command-list generation. The user confirmed no interruption/menu change: they
had simply left the PC. Exact evidence,
limits and next test are in [live regression](SOURCE_VISIBILITY_REGRESSION.md).
The forced 0.05-gu trace step also demonstrably skips a negative interval; dense
sampling leaves other false-clear paths positive. These are still unresolved,
not permission to tune a hit threshold or declare source occlusion complete.
No live fix acceptance is claimed at this checkpoint.

**Current candidate:** `v2.1.10-source-visibility.4`, `CDT_RESEARCH=OFF`, prepared
but not installed by Codex. ASI 1,304,576 bytes, SHA256
`DF66EEB1D64230FD9A690B48580CC6CCFBAAC800A36CA0F08594CBEE739FE462`;
ZIP 812,304 bytes, SHA256
`2780A21D4C74BC9D5F21CC0393B2B8B6FE2E84218F2B4B2533DB78E99863EEA9`.
Path: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.10-source-visibility.4-ModManagers.zip`.
This candidate shortens native publication, reuses the reader's existing single
complete cache only within its unchanged 500-ms limit, corrects SDF half-texel
sampling, and permits narrowly guarded replacement of a stale completed SDF
binding. OFF suite: 26/28 pass, only the same inherited research failures.
Managed SDK8/9: 69/69; Python sampler: 10/10; production SDF acquisition: 691 WARP
controls without GPU debug errors/warnings. HTTP/WebSocket smoke passes on 27312.
These are synthetic/build results; the camp geometry and live restart remain open.
All production feature switches are on, including ShowDetails=1 and
HideOccluded=1 for the private all-functions test. Numeric settings retain their
bounded defaults; this is not a maximum-load or all-values acceptance claim.
F11 hides/restores fresh blocked HUD/radar sources. All four shortcuts accept
other virtual-key codes or 0; raw/EMA records remain complete. F9's inherited
overflowing disabled research section was corrected after synthetic image review.

Exact `.4` VirusTotal: ASI **5/71**, ZIP **0/67**; same five detecting engines as
`.3` (Bkav, CrowdStrike, Cynet, McAfeeD, Microsoft). Raw responses are in
`artifacts/validation/source-regression-20260912/`. Research ON ASI also builds.
Previous `.3`: ASI **5/70**, ZIP **0/67**. `.2` ASI was
**5/70**, ZIP **0/68**; v2.1.9 ASI was **4/71**. Historical 0/75 is not current
clearance; final release remains blocked. Do not call this a clean ASI or bypass AV; see
[production validation](PRODUCTION_OCCLUSION_VALIDATION.md).

The INI audit found a signed/unsigned negative-port mismatch (fixed, 20 controls)
and research switches that could replace normal feeds or silently mean something
different in OFF. `CDT_RESEARCH` now gates all research/console/explorer startup
and legacy HUD-test activation. OFF uses only Ambient/SourceVisibility product
switches; old research INIs cannot activate those research entry points. The
unchanged 60-key template is preserved as `CrimsonDesertTelemetry.research.ini`
for ON builds; OFF has 34 offered keys. This addresses demonstrated configuration
conflicts, not an AV hypothesis. No research sources/captures were removed.
Package/profile checks and all nine UI tests pass, including combined HUD,
markers and notifications in SDR/scRGB at 720p/1080p/4K. Full local native suite:
26/28, with only the same two inherited research failures. ON ASI builds and its
overlay-model test passes. Full live INI/all-functions acceptance is pending.
An independent code review finds no hidden Ambient dependency for SourceVisibility:
the source-only path still observes GI/camera and queue submissions. Ambient-only
leaves source metadata disabled; Lights/ManyLights off leaves capture stopped.
These are code findings, not new live isolation results.

Previous code/docs were committed and pushed through `bb30d10` to main
(including all seven previously local commits); that exact GitHub CI run passed.
No release/tag was created. Earlier GitHub CI exposed
an older SDK8 test overload issue at `SmoothedLightTests.cs:92`; explicit
`Enumerable.Reverse` fixes it. SDK8.0.425 now builds the complete solution with
zero warnings/errors and all 68 managed tests pass. CI is also set explicitly
to build the OFF ASI while keeping the direct research regression targets.
The next CI run exposed the pre-existing unavailable Ambient schema mismatch:
the producer sends a null local estimate, while the schema required an object.
The schema now accepts null only when unavailable and requires the existing
status object when available. Both branches and rejected combinations are
covered; the full HTTP/WebSocket smoke test passes on isolated port 27312.
The 68 managed tests also pass. Production port 27311 was untouched.
GitHub About and repository/package descriptions are updated. The external Nexus
page was not edited; its prepared English text is in `docs/PUBLIC_DESCRIPTIONS.md`.
Locally all production-relevant native paths pass;
the previous full native suite was 24/26 due to the two inherited research failures.
The ON ASI also compiles. Do not commit the inherited `spatial_readback.cpp`
changes or unrelated root diagnostic scripts with this work.

The fixes, tests, descriptions and exact `.4` evidence were committed and pushed
as `fd3bf54`. Its GitHub CI run is in progress; no release or tag was created.

**Earlier `.4` installation checkpoint (completed; see current test above):** the user closes the game and installs `.4` through DMM;
verify installed companions, then capture a stationary raw/EMA window and fresh
SDF update controls at the same camp. Resolve the remaining trace/geometry false
clears with a fresh controlled target; do not equate the continuity fixes with
per-light acceptance. CI at `505f5ca` passed. Ambient is accepted; per-light
live acceptance/frame cost and final AV clearance remain outstanding. A separate
public **2.0.2** DMM/graphics-crash report (Nexus post around 08:30, download
listing 06:02AM on 2026-09-12) is preserved in COMPATIBILITY_ISSUES; GPU/crash
evidence and exact package hash are still needed. Do not attribute it to new SDF.

Initial diagnosis read Gemini's handover, DecodeReference, the v2.1.8 diagnostic
state/log, then acquisition in the requested order. At 11:06:34 CEST it showed:

`L1 Y: wrapped=4000, relative=1, scale=1, origin=1000, cell=4001, bounds=[969,1031)`.

This did NOT prove an update-induced offset shift. The first defect was the
producer's input: it captured only 256 GI bytes and constructed
`gi[:256] + gi[:256] + scene[:512]`. The decoder needs ONE contiguous 768-byte GI
block at `owner+0x20`. Before/after GI snapshots are compared, not concatenated.

| Decoder field | Required bytes | v2.1.8 actually supplied |
| --- | --- | --- |
| inverse 0x10 | GI 0x10 | GI 0x10 |
| wrapped 0x130 | GI 0x130 | GI 0x30 |
| L1 origin/scale 0x150 | GI 0x150 | GI 0x50 |
| L1 relative 0x250 | GI 0x250 | scene 0x50 |
| UV 0x2E0 | GI 0x2E0 | scene 0xE0 |

Independent replay of 47 stable CPU contexts from 12 preserved capture folders:
full GI selects clipmap 1 in all; the v2.1.8 merge fails in all. This establishes
CPU layout, not GPU pairing; some source runs had rejected GPU pairing.

`spatial_acquire.cpp` now retains the complete `ConstantBytes` CPU block through
submission completion and passes it directly to the sampler. Removed the unused
scene snapshot and obsolete GPU GI-buffer resolution/copy/allocation tail. Scene
is still used separately to obtain the frame number. All decoder offsets/math
are unchanged; no offset was guessed and no synchronization research was reopened.

A second concrete input-layout defect was found before asking for a live test:
the mapped 64x32x264 R8 volume has 256-byte rows, but production sampled with a
64-byte stride. The sampler now accepts row pitch and samples mapped rows directly,
without another volume buffer. Packed research callers keep their 64-byte default.
The temporary absolute-path logger is removed and the existing null-input guard
is restored. Original diagnostic source/log and all inherited changes are preserved
under `artifacts/recovery/codex-ambient-takeover-20260912-111004/`.

## Verification and evidence

- OFF package build and package validator passed. No native compiler errors;
  CMake reports the existing third-party MinHook deprecation warning.
- 37 native sampler controls pass, including malformed GI assembly, padded XYZ
  data, poisoned padding, negative wrap, shifted world position and invalid stride.
- `Verify-NativeSampler.py`: 72 exact comparisons across nine preserved captures,
  zero mismatches. These are historical/offline results, NOT new live acceptance.
- Full CTest: 23/25 pass. Two RESEARCH-source tests fail on inherited edits in
  `spatial_readback.cpp`: `Records`/`LatestRecord`/retention were disabled before
  takeover. Failures: `spatial-texture-readback` check 37 and
  `spatial-paired-readback` check 281. That source is excluded from the OFF ASI;
  its uncommitted changes are preserved, not silently overwritten or included in
  this fix. Do not call the full suite green.
- Current diagnostic live API control before fix progressed sequence 24450 ->
  24527 and sky captures 935 -> 937; visibility stayed null throughout.
  Saved at `artifacts/light-research/ambient-input-fix-20260912/` alongside log.
- Current game EXE BCBF623AD5690147DC462AEAED5B4F97BD73296BA0D6AB54663586E7088B1C0E
  is already preserved at `artifacts/recovery/20260911-build-25246367/`.

## Package for the next controlled test (not a final release)

`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.9-ambient-input-fix-ModManagers.zip`

- ZIP SHA256 `69AB40274750A8B104677F5B520EC3465E5C525487D561009B81D53AC83E179F`.
- ASI SHA256 `2B9BAE0031E962280A92874B318F97E4D8758489C7DC287C08C162F4574F8F17`.
- ASI 1,394,176 bytes; ZIP 846,788 bytes. `CDT_RESEARCH=OFF`, build 25246367.
- INI ready: Ambient.Enabled=1, Overlay.ShowAmbient=1; SpatialProbe,
  SpatialReadback, SignedDistanceReadback, AmbientProbe and OcclusionTest all 0.
- Package/ZIP equality passed; existing v2.1.8 ZIP unchanged
  (`DEAD49F6FD561833E0E929662E0D1BFDC89A1F24142549C1B1C9B41E61D5F2DE`).
- Not installed by Codex, not publicly uploaded, no new AV scan yet.

## Ambient live A-B-A PASSED — user installed v2.1.9 through DMM

Game PID 31832, started 12:30:33 CEST. Read-only hashes verify ASI, both DLLs,
INI and runtimeconfig against v2.1.9. DMM retained v2.1.8 `deps.cfg`; only version
labels differ, with identical runtime target, DLL asset names and dependency
structure. Record this discrepancy; full installation equality is not claimed.
The running new native/managed code now supplies valid, fresh visibility.

Outside phase recorded at 12:48 CEST:
`artifacts/light-research/ambient-input-fix-20260912/live-aba-20260912-124843/01-outside.json`.
User screenshot saved beside it. 16/16 fresh samples, 16 distinct visibility
frames, age <=31 ms, API sequence 59727 -> 60190. Visibility 0.24005465..0.38915721,
mean 0.33465750. Player unchanged at (-11407.612,665.6101,-4225.9883); camera
(-11405.418, approximately 667.83,-4228.837), with <0.006 vertical idle movement.
This is the outdoor reference near the building.

Covered doorway phase saved as `02-covered-doorway.json` plus user screenshot in
the same live-aba folder. Same PID 31832, 16/16 fresh samples/16 distinct visibility
frames, age <=16 ms, sequence 77904 -> 78368. Visibility 0.00578101..0.00736786,
mean 0.00668928 (about 98% below the outside mean). Player stationary at
(-11407.994,665.5804,-4219.4927), camera approximately (-11404.833,668.522,-4219.8784).
The covered and outside windows are clearly separated.

Interior phase saved as `03-inside.json` plus user screenshot in the same folder.
Same PID, 16/16 fresh samples and distinct visibility frames, age <=32 ms,
sequence 84953 -> 85418. Visibility 0..0.000291782, mean 0.000069732. Player fixed
at (-11411.898,665.8191,-4214.6826); camera approximately
(-11414.794,668.458,-4213.7515) with <0.004 vertical idle movement. Screenshot shows
an enclosed room with a solid ceiling and active local lights/fireplace. The
sky-visibility signal is essentially zero despite those local light contributions.

Return-open phase is saved as `04-outside-return.json` with the user screenshot;
`stage-summary.json` compares all four phases. Same PID, 16/16 fresh samples and
distinct visibility frames, age <=47 ms, sequence 93834 -> 94298. Visibility
0.18810925..0.20661667, mean 0.19967639. Player (-11408.836,665.57056,-4224.0977),
camera approximately (-11404.971,667.79535,-4227.664), about 1.25 game units from
the initial camera position. It is a nearby return location, not identical pose.

All 64 samples are fresh with progressing controls. Ambient visibility reverses
from 0.33466 outside to 0.00669 under the roof, 0.00006973 inside, then 0.19968
outside again. The user's screenshots show sunrise during this route: mean sky
luminance rose from 0.00194 to 0.75966. The geometric visibility reversal is
measured separately from that brightness change. Ambient Occlusion is complete
for this controlled live acceptance on build 25246367; this does not claim an
exhaustive test of every cave or material. The signal samples CAMERA location.

User additionally confirms the HUD must include per-light visibility. Installed
v2.1.9 still shows raw light markers without per-light occlusion. The `.3`
candidate includes status and optional hiding, without dropping original API data.

The installed v2.1.9 OFF build still links `cdt_sdf_disabled`. The next production
implementation is implemented and synthetically tested: calibrated `sdf_visibility.cpp` via
one current volume, one pending GPU copy and three necessary deferred lifecycle/
barrier hooks in `sdf_acquire.cpp`. Do not restore root-binding discovery, tracing
or capture history. Geometry metadata is an additive native v3 bridge tail;
legacy v2 remains readable, raw/EMA RGB and source records remain unchanged.
`docs/SOURCE_VISIBILITY.md` describes pairing, bounds and freshness.

The Warspike fireplace at (-11402.502,666.559,-4216.367) is present in all 64
saved current source captures. It is behind the paired camera in all 16 covered
doorway samples (depth approximately -0.735), in-frustum inside, off-frustum in
both outdoor stages. Use this target for visible -> blocked -> visible plus
behind-camera validation; no history or new source registry is needed for that
controlled test. Preserved current-build SDF calibration is in
`docs/SDF_VARIANT_A.md` and
`artifacts/light-research/variant-a-pid31852-20260911-1242-warspike/`.

**Next:** let the user install the configured `.3` OFF DMM package and perform
the real fireplace test. Its exact ASI/ZIP scans are recorded above; per-light
live acceptance and final AV clearance remain outstanding. The project is NOT
complete or release-ready.
