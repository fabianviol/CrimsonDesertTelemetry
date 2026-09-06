# Current checkpoint — filtered light tail fix, 2026-09-06, Codex/Astra

## Result / one next step

**1.3.0-preview.3 is installed in PID33348; user reports the result is already great.**
The actual GPU valid-prefix counter is now captured with its light buffer and
the managed reader decodes only that prefix. No hardcoded33, color/motion heuristic,
smoothing or new hook. The miniHUD root arrow/camera cone are larger, outlined,
and drawn above the light dots; dots retain measured HDR-derived color swatches.
UI change commit `673651c`; counter fix `ff1f85b`.

Current discussion: API policy for several overlapping fire contributions.
Recommendation (not implemented/approved): retain raw current contributions and
offer a separately labelled grouped summary only with defensible membership;
do not collapse nearby sources blindly or claim source-RGB sums reproduce pixels.
Actual appearance also depends on spatial/angular attenuation, visibility,
materials, indirect light and tone mapping. Source-derived summaries and a
view-dependent Hue estimate must remain distinguishable from measurements.
No code/config/game changes. Controlled preview.3 movement recording remains pending.
One next technical step if continuing brightness work: capture the matching
ExposureConstantBuffer alongside the existing light/counter sample to directly
validate its dynamic scalar, after deciding the requested API representation.

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
negative-RGB special route. The factor's actual upstream cause is NOT established.
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
