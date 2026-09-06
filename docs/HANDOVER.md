# Current checkpoint — 2.0.0 live startup verified, 2026-09-06, Codex/Astra

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

**One next step: ask user to confirm the visual notice timing (silent loading,
then six-second success) before proceeding to GitHub/Nexus preparation.** INI says
6000ms and notice lifecycle/raster tests passed; the appended overlay log has no
timestamps, so it does not independently prove this run's on-screen timing.
New untracked `media/` belongs to the user/other work and was left untouched.

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
- Normal notification startup/loading/discovery remains silent, without arbitrary
  loading timeout. Ready requires playing + fresh requested data (valid empty light
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
