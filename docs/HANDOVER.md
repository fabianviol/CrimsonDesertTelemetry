# Current checkpoint — filtered light tail fix, 2026-09-06, Codex/Astra

## Result / one next step

**1.3.0-preview.3 is built, tested and packaged; NOT yet installed/live-validated.**
The actual GPU valid-prefix counter is now captured with its light buffer and
the managed reader decodes only that prefix. No hardcoded33, color/motion heuristic,
smoothing or new hook. The miniHUD root arrow/camera cone are larger, outlined,
and drawn above the light dots; dots retain measured HDR-derived color swatches.
UI change commit `673651c`; counter-fix source is in the commit carrying this checkpoint.

One next step: user closes the game, installs preview.3 through DMM, restarts and
returns to a fixed lamp. Check a progressing stationary control then movement:
native counter[1] vs published prefix, paired resource identities, real anchor
stability and absence of old camera-attached tails. The count need not be33 at
another position. Preserve real firefly motion and pulsation. Only after that
consider remaining label/focus layout jitter. Current unchanged game: PID788,
started20:30, preview.2; no game/config/install writes performed this turn.

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
