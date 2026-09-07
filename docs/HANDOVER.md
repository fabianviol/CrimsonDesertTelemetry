# Current checkpoint — ambient shader family verified, 2026-09-07, Codex/Astra

**Latest bounded follow-up:** native A explicitly selects the named
`PrecomputeAmbient` technique pass before its captured dispatch. All SIX entries
for csPrecomputeAmbient in the existing shader index were extracted and compared:
their executable function bodies are identical (hash/evidence in
docs/AMBIENT_DECODE.md). Variant choice within this current-build family is no
longer a decoding blocker; do NOT build another shader hook merely to choose
between them. A bound-PSO hash was not captured; historical decoder reports keep
runtimeShaderIdentityVerified=false. No ASI/config/API change or new game capture.

**New result:** exact packing and sample normalization derived from the existing
csPrecomputeAmbient export. 9 signed coefficients per working RGB channel,
256 upper-hemisphere samples, shared sum/128. Mean sky RGB=C0/(2*.282095);
upward irradiance/pi quadrature=-C1/.488603. Same fixed color matrix as older
ManyLights research; decoder preserves working RGB and an inverse-matrix result.
Estimated Rec.709 luminance is labeled as an input-primaries assumption, not lux.
Detailed derivation, source hashes/lines, limitations: **docs/AMBIENT_DECODE.md**.

Private scripts/AmbientSh.psm1 +Decode-AmbientProbe.ps1 added; reader gains an
optional PassThru with unchanged default output. 178 synthetic/offline checks
pass; all360 preserved gameplay samples decoded under an EXPLICIT profile
assumption. Each capture directory has a new ambient-derived-candidate.json;
original binary/parser exports unchanged. Native A is known, live PSO shader
hash still NOT captured. No API/plugin/config/package/consumer changes.

**One next step:** prepare paired ambient/exposure diagnostics, reusing the
existing ExposureOwner route and four-float4 CPU candidate documented below.
Establish its provenance/timing before calling it the sampled GPU exposure;
do not assume UAV state for a CBV copy or divide every local light by one scalar.
Keep raw sky values and explicit relative units. Existing consumer exports read row7/56, not
the SH evaluation; don't claim otherwise. Local roof occlusion is unresolved;
if another live control is needed, use a quick out/in/out in ONE capture rather
than separated stand runs. Do not rebuild the decoder or redo the earlier heap/GI
search. AmbientProbe=1 still pauses normal/derived local lights until restored0
and restarted. User need not stay at their last pose.

## Three gameplay captures — reference

**Latest: return-outdoors run3 complete.** User reports outside the home at about
01:45 in-game, moonlight. Same PID34736/ASI/config. Explicit start23:09:15,
complete23:10:15: 120 valid fenced samples, frames56372..59645 over59.843s,
A RVA3849BB7 only, resource0x137024E80, fences241..360. API sequences55771->62632,
no errors; player(-10404.208,611.77277,-4424.308) unchanged at endpoint reads.
Paired CPU camera near(-10405.824,614.823,-4419.3525). Probe returned IDLE.
User released from standing; do not assume they remain here in the next turn.

Evidence `artifacts/light-research/ambient-live-20260907-pid34736-outdoor-return/`:
`ambient-probe-34736-11299500-3.bin`, parser JSON, native log and INI.
Binary SHA256F65F00872BDDAA6F901AA4A272C6F3F8A13EEC9CA28263546CD41AD171486344.
All three gameplay runs passed (360 samples). **No local indoor/outdoor
acceptance:** run2 end to run3 start has an uncaptured244.860s gap, and the moon
direction changed23.1885deg. No doorway transition captured; not a controlled
same-lighting A-B-A. Raw SH/scalar lanes change nonuniformly, not decoded RGB.
Details and source packing notes in research/light-source-tests/GPU_LIGHT_LAYOUTS_25116796.md.

Offline decoding follows at the top checkpoint; global sky and local occlusion
remain distinct. No plugin/API/config/package change during these measurements.

## Indoor run2 reference

User reports
inside the player home, with windows. Same PID34736/ASI/config as below. Player
(-10396.898,612.0811,-4414.3125), paired CPU camera near
(-10396.708,615.220,-4411.6694). Explicit start23:04:11, complete23:05:10:
120 valid fenced samples, frames38296..41870 over59.562s. Same A producer and
resource0x137024E80, fences121..240 (no old fence reuse), B=0. API sequences
38002->43596 and unchanged player position at the endpoint checks; no errors.

Evidence `artifacts/light-research/ambient-live-20260907-pid34736-indoor/`:
`ambient-probe-34736-10995062-2.bin`, parser JSON, native log, installed INI.
Binary SHA2567674AE55340D713107FFFB1C085728991B5A8451B4386ECD2DDEF3B92332B277.
Rows0..7 and56 vary; others unchanged within this run. Raw row6 XYZ
(.0003239843,.0004012281,.00030329468)->(.0003145608,.00038555585,.00029848135),
W stays1. Sun direction(-.017686604,-.33864108,.94074947)->
(.081872046,-.33665884,.9380607). Both solar/lunar directions have changed
substantially since the outdoor recording, so the lower raw coefficients do
NOT prove indoor occlusion. No decoded ambient RGB or locality claim yet.

Return-outdoors run3 above completed in the same process; time drift remains
a confound, despite the short spatial distance outside the home.

## Outdoor reference in the same process

User accepted local-light smoothing and moved to ambient. Restarted PID34736
(22:51:48) loads the same local-lights.1 ASI, hashB80FEA8597F1399EECBBA982C396D7E0651E36E32387CB82A38E2A0764AA1400.
Installed INI Research/AmbientProbe=1; log confirmed IDLE before the explicit
start. Player/camera API playing and progressing at the camp, not the menu.
Assistant sent ONE `Start-AmbientProbe.ps1 -ProcessId 34736` after this check.

**Outdoor run1 complete:** 120/120 parser-valid GPU-fenced records, frames
8611..11402 over59.984s, native path A RVA3849BB7 only (B=0). Resource
0x137024E80, native width65536, heap1/DEFAULT, queue2/COMPUTE. Logical view
64x16; copied1024byte prefix. Paired CPU camera(-10499.338,614.517,-4378.455)
through(-10499.338,614.515,-4378.455); player remained
(-10502.611,610.52826,-4373.8613). API sequences2483->11414, no errors.
Capture started22:54:52 and completed22:55:53. Subsequent indoor run2 above
reused this process without a restart or ASI replacement.

Evidence: `artifacts/light-research/ambient-live-20260907-pid34736-outdoor/`
contains `ambient-probe-34736-10436500-1.bin`, parsed `ambient-readback.json`,
native log and INI. Binary SHA256
`484B1DAE203BEAA2D40492A77B4D7188F2C57E8FC7CCE951E7189A189AEC8FBC`.
Rows0..6 and56 change; all other rows unchanged over this run. Raw row6 XYZ
changes(.008332003,.005748682,.001810758)->(.002566751,.001718922,.00048462633),
W .15278931->.23067258. These are raw coefficients/scalars, NOT decoded RGB.
Sun/moon directions also advance. The outdoor signal changes substantially
without movement: **time/weather confounds a simple sequential indoor decrease**.

Indoor and return-outdoors comparisons are recorded above.
Do not claim player-local ambient based on global sky coefficients alone. SH
packing/normalization, exposure, actual shader-variant identity and direct
sun/moon color/intensity remain unvalidated; no ambient public API yet.
Normal/derived local-light streams intentionally remain paused in diagnostic
mode. After experiments restore AmbientProbe=0 and restart; backup path below.

## Previous checkpoint — smoothed local-light live stand test passed

User additionally requested grouped/smoothed **local lights**, not ambient, for
immediate CrimsonHue consumption. Implemented in telemetry, no CrimsonHue edits.
Raw `/v1/snapshot`, `/v1/stream`, schemas1.1/1.4 and HUD data remain unchanged.
New HTTP `/v1/lights/smoothed` + WS `/v1/lights/smoothed/stream`, separate envelope
schema1.0. Contract/consumer warnings/config: **`docs/SMOOTHED_LIGHTS.md`**.

Current filtered ManyLights only (do not double count authored list). Conservative
0.15gu proximity groups, sum linear HDR RGB, 200ms time-based EMA once per NEW
GPU capture. Single lights smoothed too. Spatial session-local tracking ignores
sample indices; it is approximate, NOT physical identity. No transitive groups.
Current centroids and raw directional contributions preserved; no invented
aggregate spotlight cone. Missing groups removed immediately, stale/fault/loading
clears all output and tracking. Consumer must watchdog capturedAt/sequence, not
publishedAt, and use colorLinear once, not add contributions/raw stream again.
INI `[LightSmoothing] TimeConstantMilliseconds=200, GroupRadius=0.15`; CLI flags
also available. No ambient schema or physical lamp output added.

Private **normal-light** DMM package installed and running in PID23572, not published:
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-local-lights.1-ModManagers.zip`
SHA256 `97D1480A3DBFAA14F484879C813D79D3B00639A68BD87E927E110CCF41A7005B`;
ASI SHA256 `B80FEA8597F1399EECBBA982C396D7E0651E36E32387CB82A38E2A0764AA1400`.
Normal ManyLights enabled, AmbientProbe absent/off. Use this package for Hue,
NOT ambient-probe.2 (that ZIP still has the old managed host and pauses lights).
This new ASI includes the explicit-start ambient instrument too; later enable
Research/AmbientProbe=1 and restart if needed, instead of downgrading the host.

Evidence: managed build/tests pass incl. grouping/HDR sum, singleton/group EMA,
sample permutation/rate independence, disappearance/staleness/invalid controls,
32768 dense records and 4096 tracked groups, unchanged raw serialization and
separate subscriptions/health faults. Real loopback HTTP/WS smoke passes; native
16/16 CTests pass. Package validator passes. Packaged ASI bootstrap on a copied
fixture at port27316 successfully forwards nondefault333ms config. These checks
are synthetic/offline; the following stand test is separate real-game evidence.

Live PID23572 (started22:39:07), installed ASI hash matches B80FEA85...; native
log confirms normal20Hz ManyLights, no ambient probe. API playing, user standing
at(-10502.611,610.52826,-4373.8613). 12s simultaneous WS capture: **717 raw +717
derived messages,188 distinct captures**, all available, age0..94ms (mean46.38).
Player position unchanged; raw telemetry sequence12423->13147. Matched all188
GPU capture sequences,8791 contributions unchanged,22773 EMA lane checks and
529 repeated-capture checks passed, no violations. Group count39..43; not a
claim that every scene source was static or every group has physical identity.

Nearest known fire at(-10507.645,610.937,-4368.322): two contributions in all188
captures, **one tracking ID despite161 distinct sample-index combinations**.
Raw/smoothed mean luminance0.830806/0.830421; summed absolute step variation
9.57781->5.31268 (**44.53% reduction**). Blue singleton at
(-10510.692,611.633,-4371.438): one ID despite84 index values; step variation
0.0206773->0.0173479 (**16.10% reduction**). Reduction is the measured temporal
variation of this short run, not color accuracy or a universal noise measure.
No lamp was toggled during this capture; physical Hue and movement/off/culling
acceptance are not claimed by the stand test.

Evidence: `artifacts/light-research/smoothed-live-20260907-pid23572-a/`
raw.jsonl,smoothed.jsonl,before.json,after.json,validation.json. Reproduce with
`scripts/Capture-LightStreams.ps1` (bounded, read-only API; new directory) and
`scripts/Test-LightStreamCapture.ps1` (offline; creates new validation.json).
Original public2.0.0 and both ambient ZIPs remain immutable. No game files changed
by the assistant; the user installed the preview through their workflow.

User accepted the remaining fire pulsation at200ms and explicitly chose to move
on to ambient; **do not insist on another local-light off/on test**. It remains
an unperformed additional control, not a blocker to this next experiment.

**Next — ambient restart prepared:** current PID23572 still runs normal lights.
Assistant backed up the installed INI to
`artifacts/light-research/ambient-control-20260907/CrimsonDesertTelemetry.before-ambient-pid23572.ini`
and added `[Research] AmbientProbe=1` to bin64/CrimsonDesertTelemetry.ini only.
It takes effect on the NEXT process; no ASI/package changed or live trigger sent.
User should restart the game and load outdoors. On "ready", verify new PID,
installed B80FEA85... ASI, native log IDLE and progressing API player/camera;
then use `scripts/Start-AmbientProbe.ps1 -ProcessId NEWPID` and inspect first
complete GPU samples BEFORE asking for an interior comparison. Same ASI/ZIP
supports this; no downgrade to an older diagnostic package. ManyLights and its
smoothed stream intentionally pause in ambient mode. Afterwards restore
AmbientProbe=0 and restart; preserve all other user configuration.

## Previous checkpoint — ambient readback works; explicit-start probe.2 ready

Probe.1 ran in PID34848 with the expected ASI hash. **120 real GPU-fenced
samples, one DEFAULT resource, path A active; no faults.** However the automatic
60s window completed during loading/menu, before the camp was loaded. CPU scene
cameras remained near zero/(0,1000,0), unlike the later progressing live API camp
control. This validates the instrument/producer, NOT outdoor ambient semantics.
Raw capture/log/config/parser export preserved in
`artifacts/light-research/ambient-live-20260907-pid34848/`; full evidence in
`research/light-source-tests/GPU_LIGHT_LAYOUTS_25116796.md`, final subsection.
User was told the early start was our diagnostic-control mistake, not an absent
ambient source. Game is now absent (read-only process check); no ASI replaced.

Private probe.2 fixes control, not the measured path. Starts IDLE even with valid
loading/menu frames. The assistant explicitly starts each run after verifying a
fresh playing API snapshot using `scripts/Start-AmbientProbe.ps1 -ProcessId PID`.
This signals `Local\CrimsonDesertTelemetry.AmbientProbe.PID`; no Explorer/console
enablement, remote memory writes or public API changes. Verify log acceptance
and NEW `ambient-probe-PID-TICK-RUN.bin`, then use `Read-AmbientProbe.ps1`.
Each run: 2Hz/120 valid samples; returns idle. Repeated runs work in the same
process and keep increasing fence values. Busy requests are discarded, faults
and external stop permanently refuse restart. Each file is CREATE_NEW.

Private DMM ZIP (configuration included, not published or installed yet):
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-ambient-probe.2-ModManagers.zip`
SHA256 `E1463B646CE417182B7B8A03970B94A12811F79BABB9E3350E2CD89E5A8D7812`;
ASI SHA256 `9EFC5A2EE11738BADFF0FEF5434093E753884D6F9E4C3CE46B844318B02789E0`.
Native Release build and 16/16 CTests pass, including actual named-event control,
idle suppression, two GPU-gated runs (no old-fence reuse), file isolation,
busy/fault/stop negative controls. Parser fixture/15 negatives and package
validator pass. Public2.0.0 hash unchanged. These new control tests are synthetic.
ManyLights/rendered-light API and markers remain paused in this private mode;
player/camera/authored path remains. Restore AmbientProbe=0 plus restart afterwards.

Sun/moon directions are included in every scene record. Direct sun/moon color
and intensity are NOT separately validated. Ambient packing/normalization,
exposure and player-local interior/shadow semantics remain open; no public schema.

**Next:** user installs probe.2 through DMM and loads outdoors; on "ready", verify
PID/hash/fresh API, signal ONE start, inspect first complete samples while still
outside, and only then request an indoor comparison using a second bounded run.
Do not consume another loading sequence or branch into broad GI research.

## Previous checkpoint — ambient probe ready for DMM installation

User authorized continuing ambient validation and has now confirmed the game
is closed; process absence independently verified. PID30016 pointers below are
historical. **Private ZIP installation/restart has not happened yet.** No ambient
GPU sample has been measured in the game yet; public2.0.0/API remain unchanged.

Native route now resolved using existing bridge owner (no heap scan):
filterOwner+10 -> Renderer+668 -> SkyOwner+98 -> outer+30 -> inner+168 resource.
Live inner stride16/count64 confirms the1024-byte candidate. UAV producer and
CBV consumer both use SkyOwner+98. Full addresses and evidence are in
`research/light-source-tests/GPU_LIGHT_LAYOUTS_25116796.md`, final subsection.

Private probe implemented in existing `render_capture.cpp`, not another DLL or
public schema. `[Research] AmbientProbe=1` selects it **instead of ManyLights**;
`[Lights] Enabled=1/ManyLights=1` stay set. Explorer/console remain disabled.
Exact EXE guard plus executable-section/signature preflight; two post-dispatch
hooks at RVA3849BB7(skyRDI/commandRBX) and384CBA3(skyRBP/commandRDI).
Validates64x16 view and native UAV buffer bounds; copies1024bytes immediately,
on that native list, then waits for the exact submission's queue fence.
CPU SceneConstants sampled/rechecked around recording; not yet proof of the
GPU-bound scene CBV. Captures source/outer/sky/path/frame identities at recording.
No exposure capture/SH decoding/local-interior semantics/public ambient API yet.

Bound:2Hz,120 accepted samples, then no more copies. Output alongside ASI:
`ambient-probe-PID-TICK.bin` (CREATE_NEW, max468480bytes). Each3904byte record:
64byte private header,2816byte scene,1024byte ambient. Read/validate with
`scripts/Read-AmbientProbe.ps1 -Path <file> [-OutFile <new-json>]` (PS7.4+).
Header/finite-row validation rejects incomplete data, not evidence of absence.
Native log reports source width/heap/queue, each path's hit count and progress.
The normal rendered-light API/markers are unavailable during this diagnostic;
player/camera/authored light path remains. Restore AmbientProbe=0 and restart
(or reinstall public2.0.0) afterwards. Public package defaults are unchanged.

Verification: native Release build, **16/16 CTests PASS**; both actual hook
signatures independently match the current disk EXE. Both thunks pass160000
multithreaded calls preserving GPR/XMM/flags/stack. Real D3D12/WARP verifies
both path IDs, bad stride/count/short/non-UAV rejection, unrelated submission,
blocked GPU before fence, exact output/scene/provenance, duplicate-frame refusal,
bounded stop and no ambient bytes published into the light bridge. Parser:
positive GPU fixture +15 invalid inputs +JSON export/overwrite refusal pass.
These are synthetic/native tests, NOT live ambient-light acceptance.

Private DMM ZIP (not published, public release untouched):
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-ambient-probe.1-ModManagers.zip`
SHA256 `8A7E2378A44A68535689DF3159A53F6F233FC3CFB6E9EBAEB3C6B10ECAF638AD`.
Expanded `artifacts/mod-manager/v2.0.1-ambient-probe.1/CrimsonDesertTelemetry`;
ASI SHA256 `C5880F3F5BC06FA81C092F0A7FA95BD1C273463694DD04FB5885CA92527255E4`.
Uses unchanged managed/runtime/license payload from the actual GitHub2.0.0 ZIP;
only ASI/INI/private README differ. INI already enables the bounded probe.
Package validator/self-test and ZIP-to-expanded-payload comparison PASS.
Original2.0.0 ZIP SHA3212DD... remains unchanged.

**Next:** user installs the private ZIP via DMM, loads into an
outdoor scene, and inspect first complete ambient samples/log BEFORE requesting
an indoor comparison. If no hit, use path counters, valid scene progression and
phase diagnostics; do not call an empty file absent ambient. Do not publish.

## Previous checkpoint — ambient stream feasibility

User noticed CrimsonHue needs environmental light in addition to local lights;
authorized a bounded investigation of existing sources. **Published 2.0.0 and
its HTTP/WebSocket schemas are unchanged; ambient is not implemented.** No live
instrumentation or game installation changes in this check.

Two actual offline shader producers found, not only matching names:
`csPrecomputeAmbient` (entry6acf206f) and
`GenerateAmbientFromEnvironmentAtmosphericScatteringCS` (entryca7a87f5) write
`g_texPrecomputedAmbientUAV`, float4 stride16, u2 space39 in the inspected
variants. Directional ambient/SH coefficient evidence is strong; not a single
ready-to-stream RGB. Existing GI reflection has a 1024-byte
`PrecomputedAmbientConstantBuffer` (b32 space35), but the runtime producer,
UAV-to-CBV link, decoding/exposure convention and behavior indoors are unverified.
Sky ambient must not be advertised as measured local illumination at the player.

Reuse findings/paths/hashes in
`research/light-source-tests/GPU_LIGHT_LAYOUTS_25116796.md`, final ambient section.
Ignored evidence: `artifacts/light-research/ambient-check-20260907-sky*`;
16 representative sky-compute entries decoded/validated, 491,971 bytes total.
Already available SceneConstants contain sun/moon directions, not ambient RGB;
do not restart camera discovery or equate atmosphere input parameters with output.

**Next:** identify the live ambient producer/output, validate actual resource
bounds and immediately read back the compact result with fence/scene provenance;
then compare daylight and shade/interior. Reuse existing graphics instrumentation.
Only after validation define an additive ambient stream; no broad GI investigation.

## Previous checkpoint — published 2.0.0 ZIP passes user live test

User authorized both releases. GitHub **2.0.0 is public**:
https://github.com/fabianviol/CrimsonDesertTelemetry/releases/tag/v2.0.0
Release ID 383735205; tag source `44c7a72e3aeebcdfb7d5fc6253dc7510f68a2bf8`.
Release workflow 34063681737 and CI 34063670548/34063681715 succeeded.

Published asset 547756043: `CrimsonDesertTelemetry-v2.0.0-ModManagers.zip`,
805939 bytes; nine-file payload inspected.
ZIP SHA256 `3212DD1FD6CDEE5F537C2000572965648B97D0AC38335381E65BEF086FBBE214`.
ASI SHA256 `D5D6337637E4582704105B9D95C803E2A7F7F6AD87DA31E57AF6D20167C3E4F9`.
This GitHub-built archive is distinct from the preserved local validation ZIPs.

Post-publication acceptance: the user explicitly tested the **GitHub release ZIP**
in the live game and reported "funktioniert alles" (2026-09-07). This confirms
the published package, including the HDR implementation, works on their setup;
it is not merely acceptance of the older local build. No new independent API or
capture measurements were taken in this follow-up; output mode was not specified.

Nexus https://www.nexusmods.com/crimsondesert/mods/3374 was updated through its
editor/uploader: the new lighting-first 2.0 description is saved; the same ZIP
was uploaded through Update existing file and Save file succeeded. The table shows
Main / Primary / 2.0.0. Public file ID **14887**, 787 KiB; at final verification
Nexus says **not yet downloadable: virus scanning in progress**. Public version,
new description, file summary and changelog were checked; no scan bypass attempted.
Previous 1.2.1 was archived, not deleted. The new screenshot,
"Version 2.0 - Live light markers and 3D radar", was uploaded and made the thumbnail;
https://youtu.be/eyRkkTXAU64 was added. Automatic Nexus CI remains unconfigured;
`NEXUS_AUTO_PUBLISH` is unset. This release used the site UI, not automatic CI.

Native Release and 14/14 CTests pass; GitHub also verifies 56 managed tests and
HTTP/WebSocket coverage. HDR10/scRGB uses the original linear-light UI compositor
with configurable 200-nit white (80–500); SDR retains its direct path. HDR evidence
is WARP/synthetic plus real ImGui integration/SDR↔scRGB transitions, **not live HDR
display/game acceptance**. No suitable HDR setup was available; the new live test
above does not establish actual HDR-output/display coverage. Full native lighting
still targets exact Steam 25116796.
API v1/WebSocket `/v1/stream` and raw schemas remain unchanged; no complete-light,
permanent-OFF, persistent physical-ID or exact-pixel-color claim is made.

Public GitHub release is visibly Latest, with the verified asset available through
the release API. Nexus requirements still correctly point to the x64 ASP.NET Core
runtime and ASI loader. Next, if following up: check file 14887's automatic scan
result on Nexus; do not upload it again. No further source/game installation work
is required by publishing. In Nexus's file-description editor, locator fill did
not persist; normal select-all/type/Tab did, verified on the public download page.

## Previous checkpoint — HDR added; publication paused

User stopped the GitHub release after learning the old HUD explicitly disabled
HDR output, then authorized implementing HDR without a live HDR display/game test.
**No v2.0.0 tag, draft or public release was created.** Main was pushed through
`1e7d999` (lighting-first README/media/credits); repository About/video updated.
WebSocket is unchanged: `/v1/stream` carries JSON; HTTP snapshots remain available.

Implemented original D3D12 HDR compositor: HDR10 R10/PQ/Rec.2020 and FP16 linear
scRGB, alongside unchanged direct SDR rendering (8-bit plus 10-bit SDR). UI goes
into a transparent FP16 target, then converts/mixes with the game in linear light.
No game tone mapping, output color-space setting or HDR metadata is changed.
Transparent finite scene pixels and scene alpha are preserved. UI reference white
is `[Overlay] HdrPaperWhiteNits=200`, clamped80–500, shared by all UI/notices.
Mode/format changes pause incompatible drawing and rebuild only after our GPU fence.
Two extra full-resolution GPU textures/copy/composite are used for HDR UI; not SDR.

Verification: native Release build and **14/14 CTests PASS** (8.73s), including all
previous SDR/light/notification/WebSocket tests. New offscreen WARP tests verify
PQ/scRGB pixel goldens, alpha, gamut conversion, negative/extended scRGB and repeated
states; real ImGui scRGB tests cover notices/fonts/4K/resize, plus FP16→SDR→FP16.
This is synthetic GPU evidence, NOT live HDR-game acceptance. No HDR-capable setup
available per user. Sources/tests: `overlay_hdr.*`, `overlay_graphics.cpp`,
`overlay_hdr_tests.cpp`, `graphics_smoke.cpp`; details in OVERLAY_VALIDATION.md.

GitHub CI34062330798 passed managed56/API but exposed flaky native capture smoke:
Sleep(2) can leave GetTickCount64 unchanged and throttle away one-shot test calls.
Old binary reproduced both missing capture and skipped queue rejection. Fixed
test-only interval=0, phase diagnostics and6s deadline; production20Hz/5s policy
unchanged. Targeted and full native runs pass. CI log stays ignored under
`artifacts/github-release-v2.0.0/`. A fresh GitHub CI must verify the saved update.

HDR validation ZIP (803152bytes):
`artifacts/mod-manager/v2.0.0-hdr-20260907-001024/CrimsonDesertTelemetry-v2.0.0-ModManagers.zip`.
SHA256 `8B92CE3AC3E4EC09A468DF772AC84BFFA127895E3507D317A28FB8B775D71E80`;
ASI `C781D4F04B2A24D7403DA073150CDC86406B7537D32120E2773946B64619F26E`.
Nine-file validator/negative controls/ZIP equality PASS; README and INI explicitly
match current sources. It uses the newly built ASI and unchanged managed/runtime
payload from the verified pre-HDR2.0 package (host source/ABI/API unchanged).
The original versioned ZIP hash still matches E8A268...; no replacement occurred.

Saved/pushed source: `09d9462` (Codex). GitHub CI
https://github.com/fabianviol/CrimsonDesertTelemetry/actions/runs/34063281265
is green: managed/API/WebSocket plus all14 native tests, including HDR paths and
the corrected capture smoke. Working tree/package were verified; publication
recheck confirms no2.0 release/tag and latest published remains1.2.1.
Next: await resumption of the stopped publication; tag the validated source only
when release is requested again. No ASI was installed into the game during this
HDR pass. Nexus remains untouched.

## Previous checkpoint — GitHub 2.0.0 release preparation

User authorized GitHub publication, fully refreshed descriptions with lighting
first, the supplied screenshot/video, and Codex coauthor credit. Nexus publication
is separate, not requested in this turn. Repository variable NEXUS_AUTO_PUBLISH
was verified unset before publication; the Nexus workflow must remain inactive.

README, release notes, package README, API/provenance/validation and contributor
guidance now describe the unified 2.0 feature set and its actual limits. Screenshot
`media/screenshot1.jpg` is public with permission; the large local MP4 stays ignored.
Demo: https://youtu.be/eyRkkTXAU64. No plugin/gameplay/config changes in this pass.
Publication audit found no game binaries, captures or credential material in the
unpublished product commits; independent research remains local and unchanged.

Next: push main, verify GitHub CI, then tag v2.0.0. The existing release workflow
builds a fresh package and creates a draft; verify its asset before publishing.
Do not replace the locally tested ZIP below. Its hash records the live-tested
pre-description-refresh package, not the new GitHub build. No release published
at this checkpoint; replace this paragraph with the actual result after completion.

## Previous checkpoint — 2.0.0 startup behavior accepted

User requested version **2.0.0**, not another preview, for the forthcoming release.
User reports "läuft" after installation/restart. **2.0.0 is installed and the
packaged live data path passes.** NOT uploaded/tagged/published. PID40280 started
23:20:22 CEST; loaded bin64/CrimsonDesertTelemetry.asi disk hash matches the package
below. Current-start native log confirms exact-build/context detour and recurring
20Hz capture with paired counter/fence; bootstrap starts hostPID39732, no errors.

Read-only API control23:23:25→27 CEST: playing/tested, sequence4767→4845,
captureSequence2460→2480, frame8436→8496, light age16→15ms, malformed0;
18 authored sources and56 filtered contributions within configured radius.
Player and camera are independent, available poses; health.error=null.
These are contributions, not counts of distinct physical lamps.

User subsequently repeated startup and explicitly accepts the observed behavior:
the success notice appears as soon as data arrives, already during the visible
loading sequence. This supersedes the earlier request to wait until that sequence
has completely ended. No change requested. INI duration remains6000ms; precise
on-screen duration was not separately measured. PID/API evidence above is from the
earlier measured run, not a fresh measurement of this later startup.

Known accepted edge case: returning to the title screen without restarting can
leave HUD/data visible for several seconds, until stale or another load. Cause
unproven (continued engine activity vs state detection); user explicitly says leave
it alone because the HUD can be hidden. Do not reopen this as a release blocker.
No further plugin changes for these accepted startup/title-screen observations.
Publication status and the authorized media use are superseded by the top checkpoint.

- Native EXE hash, hook/caller guards and scene/wrapper/GPU contract now derive from
  `definitions/build-25116796.json` via CMake. Production StartCapture validates PE,
  executable sections, exact hook and three known caller/binder contexts before
  MinHook. Unknown EXEs remain barred from native game instrumentation; ABI2 unchanged.
- `check-update <exe>` is read-only/offline and never enables candidates. Current
  EXE returns exact-profile-anchors-checked: 11 matched code/RTTI anchors. Scene
  vtable has no proven relocatable fingerprint; it and authored layout remain
  unverified by this offline check. Current direct-layout automatic promotion is
  deliberately NOT implemented; no unsupported "just change SHA" shortcut.
- Build profiles reject unknown/duplicate JSON fields, missing chain offsets,
  malformed/ambiguous patterns, bad RIP bounds and inconsistent native contracts.
  Real player-chain fixtures cover identity, position, basis and replaced pointers.
- Normal startup/loading/discovery waiting messages are suppressed, without arbitrary
  loading timeout. Ready requires API playing + fresh requested data (valid empty light
  feeds count), default6s/clamped5–10s. Local bootstrap/native faults render without
  a host or validated game hooks. Errors may appear before loading; explicit
  Notifications.Enabled=0 still disables them. Unsupported graphics can prevent UI;
  retain logs. Radar, raw RGB and light API semantics were not changed.
- Recovery entry: `docs/UPDATE_RECOVERY.md`; three preserved archive tools now travel
  with product Git, reused from research af5485b. No active PSO/shader-identity gate:
  unchanged EXE + changed shader assets remains a real unclosed risk. Exposure
  normalization/capture remains separate and unfinished; evidence is below.

Verification: final2.0 managed build0warnings; 56/56 managed tests, HTTP/WebSocket
smoke, 11/11 native CTest paths PASS. Actual EXE file matches generated hook+all
contexts. Negative production StartCapture tests leave code untouched/no trampoline.
Raster tests verify silent startup/loading and local errors with HUD/host absent.
Package validator/negative cases and ZIP payload equality PASS. Reused shader
inspector reproduced ProcessManyLightsCS from preserved PASC/DXBC; no game writes.
Normal PR CI now runs native tests too. No whole-game future-update claim.

Package: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.0-ModManagers.zip`
SHA256 `E8A268B2B0A9D2C592A13789E0C67FC0F5C7E75BF81F09ADE7736B5784D16D12`.
Expanded: `artifacts/mod-manager/v2.0.0-20260906-231021-302-bb4227cc/CrimsonDesertTelemetry`.
ASI SHA256 `F2B62762945EC0E3A1FEDFB5B8836FB927A1DF81520A777D1F4EE6A521B1387D`.
Host FileVersion2.0.0.0; package compiled from this turn's working tree on base8ad54db
(the automatic informational-version suffix names that base, not a clean source tag).
Older versioned ZIPs are untouched. Release notes: `docs/releases/v2.0.0.md`.

Source saved in `7a33227` (Codex). Private recovery copy verified at
`artifacts/recovery/20260906-231424-3734727b/manifest.json`: product Git history
through7a33227, independent research af5485b, baseline EXE and selected shader/index
evidence. Both bundles verified; EXE copy hash matches. Same-disk copy, not off-device
backup. Original captures remain untouched; research remains clean/no remote.
Never publish the EXE/shader/index backup. No local closeout work remains.

## Previous checkpoint — update-stability audit (proposal now implemented above)

User confirmed preview.4's enlarged radar/frustum and grouped light UI in game
with a screenshot. PID was not rechecked. Product source remains `db727ca`;
this audit changes documentation only, not plugin/config/releases or game state.
No GitHub/Nexus upload is authorized by the audit request or was performed.

**Result: restart-safe and guarded against unknown EXEs, not automatically
portable for the current renderer.** Native EXE SHA gate stops game hooks before
startup on an unknown hash. The current direct camera profile also has no automatic
resolver: only historical `renderer-camera-v1` is supported by BuildCompatibility.
Actual offline `check-compatibility` on current EXE4D99...F454 fails with zero layouts,
reference24994088 missing static-position-xy-write. Exact current profile still works;
this is a recovery-coverage gap, not evidence the running telemetry is invalid.

Prioritized hardening proposal (NOT implemented):
1. Shared validated build contract for managed/native roots, layouts, hook signature
   AND surrounding register/binding provenance; a read-only update-check command
   should report individual failed anchors. Never just update the SHA allowlist.
2. Extend recovery to current direct-camera/scene paths, using existing names and
   producer chains. Native candidates must not become automatic hooks merely because
   one pattern matches. Layout/type/queue-state checks and a short live control remain.
3. Preserve a compact current recovery recipe/tools/reference manifest in product Git,
   plus private backup of irreplaceable research/artifacts. Independent research Git
   is clean at af5485b but has NO remote; product ignores research/external/artifacts.
   Existing shader names, entry hashes, seed, hook/counter chain below are valuable;
   old build-specific scripts and stale tail-validity conclusions must not be reused.
4. Add update-contract negative tests: production hook rejection (existing D3D12
   capture smoke bypasses StartCapture), current-layout relocation/ambiguity and real
   player pointer-chain fixtures. Present PR CI tests managed only; native tests run
   in release workflow. Test safe refusals in normal CI before new profile promotion.

Separate residual risk: only EXE bytes are identified. Shader-only asset changes
could keep that hash yet change 48-byte fields/counterbyte4/color semantics. No active
PSO/shader identity gate exists; numerical plausibility cannot prove field meaning.
Profile loading also lacks central schema/contract validation. Exact/trusted player
mode may retain static position when orientation validation fails (not automatically
a bad position); existing tests do not execute PlayerOrientationReader's real chain.

Checks run without new builds/instrumentation: existing managed50/50 PASS; native
hash-guard, bridge and 8-thread/80k thunk tests PASS. These do not validate a future
game version. Audit sources: BuildCompatibility.cs, BuildDefinition.cs, definitions/,
Program.cs528..533, EngineCameraReader/EngineLightReader/RenderLightReader,
native build_guard.cpp/instruments.cpp/render_capture.cpp/filter_thunk.asm;
research recovery entry points and current native/shader evidence remain below.

One next step: agree/implement the bounded current-build recovery/contract hardening
before publishing. Exposure capture remains approved but separate and unfinished.

## Previous checkpoint — larger 3D radar and light detail groups

**1.3.0-preview.4 is built, tested and packaged for DMM.** At packaging time the
installed game was PID33348 / preview.3 (superseded by the user's preview.4 confirmation);
no game files, capture hooks, raw API records or RGB normalization were changed.
One next step: user closes the game, installs the new DMM ZIP,
then checks the fire detail panel and camera pitch in the larger radar.

- Radar uses the full panel width (logical radius205, previously80); normal panel
  height550, diagnostics806. Root/camera/FOV/XYZ readouts sit below the radar.
- Camera frustum uses measured forward/right/up, vertical FOV and aspect in the
  same affine 3D projection as light positions. XZ ground, Y height; no clamped
  vertex heights or elevated flat yaw wedge. Length .4*radarRadius is schematic.
  Missing/invalid projection metadata hides the frustum, not guessed geometry.
- Nearby detail cards merge by pairwise distance<=.15gu (complete-link, no chain),
  only as presentation. Separate raw contribution values stay in spatial order;
  no physical-object IDs, sums, pulse classification or smoothing are invented.
  At most64 detail candidates/four visible contributions per group; raw API records stay.
  GPU slots appear only with F9 diagnostics. Fixed card width avoids digit-width
  jitter; compact values retain HDR/tiny magnitudes and small viewports scale cards.
- All six UI/client test paths passed: model, general HUD, notifications, combined
  lights, lights-only and WebSocket. Raster checks include a .03gu pair with an
  independently changing second row, pitch/roll with unchanged yaw, missing camera
  basis, isolated near/behind clipping, stale clearing, resize and 800x480/4K.
  Pitched4K and compact screenshots visually inspected; live game check pending.
  Relevant source: overlay_hud.cpp, overlay_model.{h,cpp}, overlay_tests.cpp,
  graphics_smoke.cpp. Package defaults/hotkeys remain unchanged.

ZIP: `artifacts/mod-manager/CrimsonDesertTelemetry-v1.3.0-preview.4-ModManagers.zip`
SHA256 `8D8FB31CBCE31979200541C7454D44C52BADC67E2AF99EAA2310F097C6DFBBC6`.
Expanded: `artifacts/mod-manager/v1.3.0-preview.4-20260906-222529-708-5a75c437/CrimsonDesertTelemetry`.
Package validator/negative cases passed; older immutable ZIPs preserved.
Screenshots: `build/light-overlay-preview4-{small,4k,pitched4k,compact}.bmp`
(synthetic fixtures, NOT game screenshots). Source research repository unchanged.

**Exposure remains a separate approved follow-on, not included in this HUD build.**
Bounded read-only check22:13:52 CEST PID33348: ExposureOwner+D8 contains FOUR plausible
exposure float4; +118 already contains pointers, so never blindly read80 bytes.
Bridge frame40441, unchanged seqlock101142, validCount42: CPU E before/after
.08051319/.08049921 predicts mode1 factor1.32961224; blue lamp measured1.32911805
(-.0372%). Strong candidate, still NOT frame-paired upload provenance. Missing
proof: how this CPUblock reaches the bound Exposure-CBV for the sampled frame.
GPU copy also lacks CBV suboffset/heap/state proof; never assume the light UAV state.
Do not revisit generic GPU searches or silently divide all effects by this value.

## Previous checkpoint — filtered light tail fix and fire diagnosis

## Result / one next step

**1.3.0-preview.3 is installed in PID33348; user reports the result is already great.**
The actual GPU valid-prefix counter is now captured with its light buffer and
the managed reader decodes only that prefix. No hardcoded33, color/motion heuristic,
smoothing or new hook. The miniHUD root arrow/camera cone are larger, outlined,
and drawn above the light dots; dots retain measured HDR-derived color swatches.
UI change commit `673651c`; counter fix `ff1f85b`.

Current discussion: usable HUD/API presentation for overlapping fire contributions.
Agreed direction (not implemented): retain raw current contributions and
offer a separately labelled grouped summary only with defensible membership;
do not collapse nearby sources blindly or claim source-RGB sums reproduce pixels.
Actual appearance also depends on spatial/angular attenuation, visibility,
materials, indirect light and tone mapping. Source-derived summaries and a
view-dependent Hue estimate must remain distinguishable from measurements.
No code/config/game changes. Controlled preview.3 movement recording remains pending.
The user approved paired exposure capture, then raised the fire HUD's usability.
Next technical step: capture the matching ExposureConstantBuffer alongside the
existing light/counter sample to validate its scalar. Separately, GPU-slot numbers
must not act as persistent HUD identities; do not smooth away real fire variation.

### Fire HUD diagnosis — 21:57 CEST

User screenshot identifies the fire at (-10507.66,610.94,-4368.33), two nearby
SPOT contributions with very different amplitudes. Read-only API recording:
`artifacts/light-research/fire-two-contributions-20260906-215755.jsonl`, eight seconds,
PID33348/preview.3; 482 playing rows, 480 available/two bridge-changing, 124 distinct
captures35782..35905, native frames56555..56978. Player fixed, camera Y bob .00671.
Both fire contributions occur in EVERY available capture; assign by disjoint
height bands, not sampleIndex. Lower Y610.91943..610.9313: luminance .12450..18824,
116/123 index changes; upper Y610.94904..610.9607: .45831..91976, 121/123 changes.
Each spans 37 GPU slots. Their source-RGB luminance sum varies .63132..1.04581;
it does not cancel to a constant. Blue glass is always slot1 here, luminance
2.58732..2.66662 (3.1% span), while the fire sum spans 65.7%. Fire variation remains
after division by the blue control; it is not explained by that common factor.
Aggregate fire RGB ratio is roughly 1:.303..310:.0754..0763 (mostly amplitude).
This spatial pair is an experiment association, not a generic physical-light ID.

Code confirms sampleIndex is simply the GPU valid-prefix index. HUD independently
re-sorts and places labels by crosshair distance every frame, so near contributions
can exchange screen boxes. However, exact projection with each envelope camera
shows ZERO pair-rank swaps here: the upper contribution is closer to the crosshair
in 124/124 captures and 480/480 available API rows. A box-swap explanation is not
supported for this recording. Normal HUD should not foreground transient slots;
one stable detail panel can retain individual current values without claiming an
unproven grouping or pixel-accurate combined brightness. No plugin/config edits.

### Exposure cause and spot labels — 21:37 CEST continuation

The upstream shader is identified: `InjectLightsCS` entryhash b606b219,
`InjectLightGroupsCS`05125ef9. Local artifact prefix
`artifacts/light-research/light-rgb-inject-20260906-2215-<entry>` (.ll/.json/.dxbc/.padxil;
filename2215 is a label, not the measured live time). InjectLights LL458..506:
q=1/max(0.0001,ExposureConstantBuffer._exposure0.x), b21/space35 byte0.
Mode0 factor1; mode1=min(max(.01,q),.1+9.9*saturate(.01*q)); mode>=2=clamp(q,.05,150).
SceneCB byte2748 bit1 adds multiplier.1 when set, else1. Do not infer that bit's
meaning just from packed name `_isPhotosensitiveMode_isAllolwBlood`.

Current CPU pack1438AF200 writes only SourceRGB(+3C)*SourceScale(+4C) to GPU+10.
1438AF322..351 encodes GPU+3C as def+75<<1 when def+74 enabled, OR prioritybit;
shader >>1 recovers mode directly, no+1. Read-only21:37:26 PID33348, stable18record
source vector: all THREE selected anchor definitions have useExposureAdaptation=1,
mode=1, four flicker floats+60..6C allzero. Blue def46A32A4B550, warm46A32A4C368,
crystal46A32A4B7E0. AuthoredRGB/scale match the prior series. At21:38:01 coherent
bridge frame60149 SceneCB byte2748=1: bit1 unset, no extra .1 dimming.
Thus shader + active lamp settings establish the exposure-adaptation route.
Observed common factors imply exposure0.x~23.9..26.2 in the active curve branch;
this is INFERRED, not directly sampled/frame-paired exposure.

Exposure binding: filterowner+10→renderer+690→exposureOwner+C0→wrapper+30→inner.
Current owner46AF0028C00, inner46AF1597C40, resource(inner+168)=195830640.
The resource is a readable COM object (not a proven GPU VA). Map method1437ACBE0
uses cached CPU pointer inner+158; cache isNULL. No Map call/hook/new capture run;
no currently available CPU Exposure0 value via this checked path. Not proof all
possible CPU copies are absent. Plugin remains preview.3 unchanged.

Spot question, API21:37:01/capture17210/frame57240/age15ms:68records=44spot+24point;
nearest5gu=3spots, nearest10gu=5spots. Blue and warmglass match authored SPOT
half-angle26.997278deg/downward; crystal POINT. Two nearest warm records around
(-10507.66,610.9584,-4368.328) /(-10507.6455,610.92175,-4368.327) are actual
SPOT27.109184deg/downward. Do not assign a physical object solely from proximity.
HUD correctly reports decoded types, not all lights as SPOT. Internal spotlight
representation does not promise a visually obvious narrow beam.

### Live RGB diagnosis — 21:26 CEST

Artifact `artifacts/light-research/overlay-blue-rgb-diagnostic-20260906-2126.jsonl`,
21:26:07.933..15.863:483 playing API rows,481 rendered available/2 bridge-changing,
120 distinct captures, native frames25140..25533. Player/camera direction fixed;
camera Y varies0.0157. PID33348 started21:17:34. Separate coherent bridge check:
ABI2/flags15, frame28097, validCount68 (input counter0=2693), bank1,
output18DD79DB0/counter18DD79520. No counterless fallback or old bank-count pattern.

Blue glass (-10510.692,611.6332,-4371.4375), warm glass
(-10493.734,611.61084,-4364.254) and crystal(-10528.074,611.3604,-4354.011)
are each present exactly once in120/120 captures; positions exactly stable.
Authored colors and rendererScale stay bit-identical in483/483 rows:
blue1.977898, warm7.5021653, crystal3.0008664.

Blue rendered luminance ranges0.061520785..0.067384094 (~9.53% max/min),
largest adjacent step0.000529051 /0.8511%, median absolute step0.000124909.
Start0.06157888→end0.06684646, small reversals, no parity alternation.
Normalized blue RGB stays effectively constant: R/B~0.7190869,G/B~0.8528073.
All nine RGB channels across the three anchors fit
`renderRGB = commonFactor * M * authoredRendererRGB` to1.285e-6 relative spread;
commonFactor0.038209728..0.041851336, anchor luminance correlations>0.9999999995.
This proves common scaling of these controls, not a changing blue hue or source
swap. Each anchor changes sampleIndex23 times/seven slots; never use it as identity.

Shader evidence (same Process LL below): ordinary nonnegative input RGB receives
a static 5%-luminance floor and constant matrix
M=[[.61312,.33951,.04737],[.07020,.91636,.01345],[.02062,.10958,.86980]].
No dynamic multiplier on that path. ExposureConstantBuffer is read only in its
negative-RGB special route. At this earlier checkpoint the upstream cause was not
established; the later InjectLights findings above supersede that uncertainty.
Older blue-glass factor-of-two report at research handover3733 was real amplitude
evidence but never proof of pulsing. Current HUD shows renderer-scaled values,
not just the constant authored color; luminance is derived from those same RGB.

### Why this bound is the engine's, not a visual heuristic

Current EXE function0x143CB5000 selects matching counter/output wrappers from
owner+0x638/+0x648 using owner+0x8F8. At existing hook0x143CB65CA, R12=output,
original R15=counter, R13=owner, RBX=command wrapper. The ASM thunk now passes
all four; original R15 is read from [r15] after R15 becomes the saved-stack pointer.
Both GPU copies are on the same command list before the same submission fence;
resource refs and capture-time identities survive through publication.

Game archive shader `ProcessManyLightsCS` atomically increments **byte4 / DWORD[1]**,
using the previous value as its output index (stride48, capacity32768).
Independent consumers `InitSortingDataCS` and `InitSortingDataIndirectCS` read
byte4 and give only indices<count valid sorting keys; the tail receives -1.
`SetDispatchIndirectArgumentsRecursiveCS` uses CPU literal _srcStartIndex=4
as a BYTE offset, not DWORD[4]. DWORD[0] and the later consumer's DWORD[2] are
not this filtered-prefix length. A special input.color.w>99999 producer route
can write at input index without incrementing; it does not enlarge consumer range.

Reproducible archive lookup: CrimsonForge `hashlittle(case-sensitive UTF8name,
0xC5EDE)` matched three known entry controls. Entry hashes:29588660 (Process),
84574902 (InitSorting),5eecfe7e (Indirect),a2b2b7e9 (DispatchArguments).
Extracted variant paths `shadercache__/63bb3e83_9d3ccf48_5_<entry>_3_deba1dcd_b13a9f29.padxil`,
group0017. Evidence prefix `artifacts/light-research/filtered-count-exact-20260906-2113-<entry>`:
.padxil/.dxbc/.ll/.json. Process shader hash c40f89b9b04627219c4b77a1736da3b7;
LL1075 append, LL1101 output; InitSorting LL86 load, LL169 prefix bound.
One archive variant per kernel inspected, not every permutation/live PSO hash.

Native bridge ABI v2 appends256 counterbytes after lights; header88/96/104 stores
output/counter/owner addresses,112 bankindex (UINT32_MAX unknown),116 counterBytes.
Flags15 require paired counter. C# rejects v1, absent identities and counts>32768;
zero is a valid empty current list. Existing public JSON schema1.4 is unchanged.

Verification: 50/50 managed tests; native bridge concurrent publication test;
capture/thunk CTests2/2 plus foreign-device rejection; 80,000 parallel thunks
preserve registers/flags and all four arguments. D3D12 test holds GPU execution
behind a fence, verifies all counterbytes, alternating resource pairs and frozen
capture-time identities, rejects missing/undersized/non-UAV/aliased counters.
Graphics smoke passed and small/4K preview rendered; small image visually checked.
Package validation passed. These tests do not replace the pending game check.

### Live diagnosis — 20:40 CEST

Video reviewed in extracted frames:
`C:\Users\fabia\Videos\NVIDIA\Crimson Desert\Crimson Desert 2026.09.06 - 20.36.51.02.mp4`.
Player moves without an intentional camera pan. #41/#170/#192 shift together
by about(-5.96,+0.16,+0.89) world units while a lamp contribution near
(-10491.68,610.89,-4370.00) remains world-fixed. These are not just jumping labels.

Read-only artifact `artifacts/light-research/overlay-static-lamp-diagnostic-20260906-2037.jsonl`
was actually recorded20:40:01.903..05.791:151/151 playing/available rows,57 distinct
captures/native frames29329..29521, zero malformed/unavailable. Player XYZ and
camera X/Z/basis/FOV are constant; camera Y bobs0.01826. Native frame parity
separates two exact populations:

| Native frames | Captures | Published / within35 | Frozen tail, indices>=33 |
|---|---:|---:|---:|
| Odd | 27 | 332 / 236 | 299 |
| Even | 30 | 335 / 222 | 302 |

Only indices0..32 change RGB. Every tail record has exactly frozen RGB within its
parity group and camera-relative XYZ spread<=0.0001001; world Y follows camera
bob. #41/#192 match the video. Known glass anchor(-10493.734,611.61084,-4364.254)
remains world-fixed57/57. Strong stale-tail/alternating-buffer evidence, not proof
that every constant-color light is invalid. Actual GPU resource identities/count
are not exposed yet. Producer+recorded transport age103..349ms; the recorder
skipped84 API sequences but saw57/58 captures (not proof of HUD packet loss).

Preview.2 code confirmed the gap: native `render_capture.cpp` copied every accepted matching
48x32768 resource without publishing its identity or valid count. Managed
`RenderLightReader.cs` scans all32768 slots, treating position.w≈pi as validity.
The old pi criterion in `GPU_LIGHT_LAYOUTS_25116796.md` came from an UNFILTERED
buffer observation, not proof of current filtered-tail lifetime. A completed GPU
copy and paired scene camera do not prove every copied slot was rewritten.
Separately, HUD focus rank and collision-based label placements are stateless,
so changing neighbors/indices also make labels jump; fix presentation separately.

## Package / controls

Immutable ZIP:
`artifacts/mod-manager/CrimsonDesertTelemetry-v1.3.0-preview.3-ModManagers.zip`
SHA256 `5F107B18B5B1677A8366F6102503A1077BEF6FA4D8865E452F09C45A8B203372`.
Expanded:
`artifacts/mod-manager/v1.3.0-preview.3-20260906-211037-749-5c0dd6b7/CrimsonDesertTelemetry`.
ASI SHA256 `B45170BD2F1BC691C1902CEF2993ABE28C83443CF79C791893F0F26E66916CF2`.
Previous preview.1/2 ZIPs remain unchanged for rollback. Never overwrite releases,
replace the ASI loader/other mods, or install directly instead of the user's DMM.

- F8: corner HUD; F9: diagnostics; F10: fullscreen light markers.
- Package enables both views. `[Overlay] Radar3D=0` restores the old compass.
- `[LightOverlay]`: Enabled=1, InitiallyVisible=1, ToggleKey=121, Radius=35,
  MaxMarkers=512, MaxLabels=6. Configurable bounds: 2048 markers / 16 labels;
  label placement examines at most 64 candidates. Units are game units, not metres.
- Missing config defaults UI modules off. Overlay/LightOverlay/Notifications must
  all be disabled to skip UI hooks/client. HUD-hidden does not stop light capture.
- Existing notices and single ASI/host/config architecture remain. Console/explorer
  stay disabled. User wants DMM; settings require restart, no hot-unload.

## Implementation / limits

`native/CrimsonDesertTelemetry.Asi/src/overlay_*.{h,cpp}`:
strict bounded rendered-record parsing; immutable shared record storage avoids
copying arrays every Present. Optional invalid/missing feeds clear records, not
core telemetry. Render freshness includes producer age, transport, parsing and
time since receipt, capped at 500ms. No historical markers kept as live.

Corner HUD retains XYZ/root/camera numbers; oblique player-centered radar adds
colored contributions, schematic height stems, root/camera yaw and view guide.
Fullscreen rings/labels show measured XYZ, linear HDR RGB/luminance, distance,
sample-local index and spot cone/direction when available. Center-priority labels
avoid HUD/reticle/other rings. Fixed-length arrows are schematic, not light range.

Only current **filtered rendered** records are used; do not sum authored+rendered.
Radar can show behind-camera records only if the feed retains them, not complete
360-degree coverage. No scene-depth test: markers may show through walls. HDR
swatches are SDR visualization, not the game's tone mapping. No physical lumens,
stable object IDs, generic OFF field or sun/sky/emissive completeness claimed.

World coordinates already use capture-paired reconstruction; screen projection
uses the latest published camera basis/FOV/aspect, rejecting near/behind/invalid
points. It is not a Present-synchronous camera: fast-motion latency/alignment is
the primary live-check risk. Drawing still requires D3D12 / 8-bit SDR.

## Previous preview.2 HUD verification

Release build and **6/6 native UI/client tests pass**: overlay-model, overlay-d3d12,
notifications-d3d12, light-overlay-d3d12, light-overlay-only-d3d12, overlay-websocket.
Real D3D12 readback tests cover height-sensitive radar, projected rings/spot arrow,
behind/near clipping, stale/missing clearing, initially-hidden then shown,
independent toggles, Present/Present1, both resize paths, 4K and center detail card.
The center card initially failed visual QA because its placement gap intersected
the reticle margin; fixed and protected by a dedicated pixel regression.

Visually inspected small/4K test images: `build/light-overlay-*.bmp` (synthetic).
WebSocket test covers marker-only startup, >64KiB fragmented light payload,
immutable shared storage and loading invalidation. Actual recorded A2 JSONL
accepted by `CrimsonDesertTelemetryOverlayTests --snapshot <file>`: 337 records,
178 in front of its camera, 77 inside viewport. ZIP/expanded nine-file payload,
configuration and no-loose-JSON/no-second-ASI validation passed.

## Established baseline / preserved research

Previous detailed integration checkpoint is preserved in Git:
`git show 0d7ac9b:docs/HANDOVER.md`. Implementation `afcc1cc`; recorder fix
`73aea96`. That preview.1 passed cold start, real camera/player movement and
physical lamp A-B-A in PID27140 (now closed): target88/88 → 0/87 → 89/89;
three controls always present, B/A2 same view. 900 API rows, max rendered age79ms.
Artifacts: `artifacts/light-research/unified-lamp-aba-pid27140-20260906-*.jsonl`.

Movement artifact `unified-camera-movement-pid27140-20260906-01.jsonl` in the same
folder: 917 distinct captures, two static anchors stable throughout; filtered
crystal omissions are not OFF. Twelve transient bridge-changing rows and38
captures with one rejected record remain bounded reliability follow-ups.
Light capture/source itself is unchanged in preview.2.

Exact native-supported Steam build25116796, EXE SHA256
`4D99C15C58BD20A94D354D10AE395D1FAC777D59EF52CBA8080DC3FC8DC6F454`.
Native instrumentation fails closed on other hashes; automatic relocation and
combined real-game console startup validation remain separate tasks.

Product is this repository. `research/` is the preserved independent Git repo
(migration commit af5485b); `external/`, `artifacts/`, and original workspace under
`archive/crimsonhue-workspace-20260906/` are preserved/ignored. CrimsonHue is only
the future Hue consumer. No original files were deleted. Research entry points:
`research/light-source-tests/CODEX_HANDOVER_FIRE.md`,
`GPU_LIGHT_LAYOUTS_25116796.md` beside it, and
`research/console-enabler/HANDOVER.md`. Do not restart resolved research paths.
