# World Builder / cdmodkit: reuse map — 2026-09-24, Codex

## Scope and evidence

Owner explicitly requested a broad review: light control, unresolved visibility /
sky questions, and alternative recovery routes for already working features.
This is source inspection and offline validation, NOT a live integration result.
No second ASI was installed, no third-party code built/executed, no game-memory
writes made, and no release changed.

Primary source: [Moon-yungg/crimson-desert-world-builder](https://github.com/Moon-yungg/crimson-desert-world-builder/tree/ee1f05a3ad1a61cd4aee66946155d0315fdc14e7),
full-history local clone `external/crimson-desert-world-builder`, inspected commit
`ee1f05a3ad1a61cd4aee66946155d0315fdc14e7`. Line references below refer to that commit,
mostly `asi/cdmodkit/cdmodkit.cpp` (abbreviated **core**). The Nexus entry is
[World Builder, mod 3546](https://www.nexusmods.com/crimsondesert/mods/3546?tab=description).
Do not assume the current source equals the downloadable Nexus binary. Source
version is 0.93; version labels on different publication surfaces differ.

Coverage: repository/module/tool inventory, deep review of object creation,
native dispatch, physics, resolvers and API contracts; relevant camera, loader,
overlay and parser paths. Not a formal security audit or every-line correctness
proof. Some comments/early research notes are obsolete; executable code wins.

## What this changes for CDT

| Area | Concrete finding | Reuse decision / missing proof |
|---|---|---|
| Switching existing lights | Native `SceneObjectClient::setEnable`-like call; real scene creation and server gimmick routes | Stronger entry points than another heap scan. Disabling an entire object is NOT its gameplay light switch; no RGB/intensity setter found. |
| Per-light occlusion | Game physics ray/shape entry points and a working-source sphere-sweep setup | Best new independent candidate for the narrow-band SDF false clears. Collision does not equal optical opacity; camera-to-light queries still unvalidated. |
| Local sky / interiors | Same physics can potentially query directions toward the sky | A new geometric estimate, not the game's ambient occlusion or irradiance. Does not repair our missing camera-sky stream by itself. |
| Update recovery | Profiler names + unwind metadata, masked signatures, RTTI, collector-vtable shape | Reuse these methods and a repeatable offline check. Current matches are not future-update or layout compatibility proof. |
| Player / mounts | World-global → controlled actor; character refresh and parent/tile transforms | Useful independent control / recovery route. Validate layouts; do not replace the current reader on plausibility alone. |
| Camera / lights | Scene-object camera and heap render-camera candidates | Keep CDT's frame-paired SceneConstants + filtered ManyLights. WB's pose ranking / manual lag is weaker for our purpose. |
| Object identity | Prefab catalog, gimmick keys, names, native UUID lookup | Useful semantic vocabulary and native lookup lead. Not an enumeration of original world lights or a ManyLights → persistent-object join. |
| Asset research | Native resource loader, PARC reflection, table/localization tools | Can avoid some repeated format research; keep our established CrimsonForge/PIX shader route. |
| Overlay / API / tools | Swapchain lifecycle lessons, queued mutations, editor operations, localization | Selective lessons only. Keep our HTTP/WebSocket streams, HDR and single-ASI architecture. |

## Native object and light routes

**Public API is smaller than the interesting internals.** `cdmodkit_api.h` exposes
spawn/move/remove/count/get/player/log/ready, not lamp toggles, RGB, raycasts or
arbitrary original engine objects. C IDs are vector indices, invalidated by
forget/clear; HTTP UIDs identify WB-owned records, not persistent engine UUIDs.
`cdk_spawn` (core 2542) obtains a count before a separate spawn and ignores its
result: do not treat the thread-safe header claim as completion or ID stability.
HTTP 202 means queued. Its pending counter is not the complete server/gimmick
queue, so zero pending is not sufficient interactive-spawn acknowledgement.

- **core 295–341:** 44-byte tiled transform construction; native enable and move.
  Positions use 1000-unit X/Z tiles. Native move disables, changes transform,
  then enables the object. Those are engine calls, not guessed flag writes.
- **core 528–563:** native `SceneObjectManager::createSceneObjectFrom()` receives
  correctly constructed game strings/path objects; manager route includes fixed
  `+0xE0 -> +0xEB0` offsets. Returned client object offers a targeted provenance
  boundary, not a blanket license to apply it to every subclass.
- **core 566–620:** original movement tick first, then one queued game job per
  tick. Readiness is not a fresh-world/epoch guarantee. Queued raw pointers need
  lifetime checks; do not run setters from our HTTP or render-readback thread.
- **core 1062–1183, 1245–1456:** captures a real gimmick spawn context, remaps
  pointers into bounded copies, supplies new identity/transform, executes
  prepare/commit and ServerField creation. Level and housing reasons are not
  interchangeable. External pointers remain external; this is not a portable
  standalone constructor. Actual code still calls `FreshKey` despite an older
  note claiming it was unnecessary.
- **core 1725–1746:** ServerField tick (vtable slot 9) processes server jobs and
  gimmicks. Other method hooks also install with tracing disabled. Do not import
  the entire hook surface just to obtain one execution context.
- **core 515–524, 1625–1648:** hiding a plain client uses enable(0), but hiding
  an interactive spawn removes its server actor. Moving one can remove/respawn
  it with a temporary plain stand-in. Neither is reversible lamp switching.
  Save isolation for every native side effect is not established by this review.
- **core 1794–1814:** a native UUID lookup exists, but the registry instance,
  lifetimes and how to obtain the desired lamp UUID remain to be established.

Direct join with OUR prior research: `notes/gimmickinfo.tsv` rows 1510 / 1518 have
`fire_Spark_Lamp` key **550011** and `fire_Spark_Lamp_off` key **549011**, matching
the exact old physical Source prefab
`/object/cd_gimmick/effect_gimmick/gimmick_fire_spark_lamp.prefab` and its `_off`
sibling. `data/prefabs.tsv` rows 30484 / 30486 include both, tagged
`Socket:4,Effect:2,Gimmick,Mesh,Other`. Tags are asset-component counts, NOT active
light counts. Large table keys must not be truncated into the observed u16
save-field layout. These rows do not prove live state can be changed by key swap.

`notes/GIMMICK_SPAWN.md` records LightSwitch / PrefabSwitch handlers and gimmick
states. There is **no implemented LightSwitch call** in the reviewed core. The
prior unsuccessful handler heap search remains unsuccessful; names alone do not
justify repeating it. Our Source child count `+0x238` and component counts are
still containers, never toggle flags. See [LIGHT_CONTROL_RESEARCH.md](LIGHT_CONTROL_RESEARCH.md).

## Physics: promising, but not an already solved visibility API

**core 947–1059, 1852–1942:** captures a game-originated sphere sweep near the
player, checking sphere RTTI, a specific collector layout, plausible start and
available hits. Copies query 0x200, transform 0x100, collector 0x200, hit data
0x100 and shape 0x200 bytes. It repairs the internal shape/hit pointers, resets
collector count/fraction, and substitutes start/displacement for a downward
ground query. The current call is queued on the game thread; an older comment
describing service from a physics hook is not the current execution path.

The result is a fraction/count/normal used for placement and sphere-radius
calibration. This is a **sphere sweep**, not a proven zero-radius optical ray.
Ray functions are resolved too, but their arbitrary-query construction is not
established merely by those addresses. WB's failed-query handling can become
`done=true, hit=false`; CDT must preserve **unknown**, not turn failure into clear.

Before using this for camera→light visibility: establish world/context lifetime,
query directions and units, radius/collector semantics, collision masks, safe
query budget, self/fixture exclusions, and streamed geometry behavior. Validate
both blocked and unobstructed paths. Windows, foliage, one-sided geometry and
non-colliding visible meshes can disagree with physics. Off-screen queries may
be possible but are not live-proven here. A hemisphere of queries would estimate
geometric sky openness; it cannot replace global sky SH, direct sun/moon light
or the renderer's camera AO contract.

This is useful precisely because [SDF_BAND_LIMIT.md](SDF_BAND_LIMIT.md) already
proves why denser sampling of the existing narrow-band field cannot fix all
false clears. Keep that result; do not restart the rejected tolerance search.

## Current-EXE offline validation

Target: public patch **2.03.02**, EXE **1.0.0.2976**, Steam **25477059**.
Preserved file `artifacts/recovery/20260923-191056-build-25477059/CrimsonDesert.exe`,
SHA256 `57da440d72f4db974f25fef047cf84c4dadd999a88cb2a3c5af4c9bd67fde1e7`.

New independent reader `scripts/Inspect-WorldBuilderAnchors.py` reads the source
as TEXT, never executes it; scans executable PE sections by flags, reports all
matches and source/EXE hashes, and refuses output overwrite. It found **17 literal
patterns: 15 unique, 1 absent on disk, 1 with 15 matches**. All 15 world-global
loads independently resolve to **one** global. This is useful corroboration,
not proof the upstream mod has survived a different game build.

| Role / upstream resolver | Current RVA (not VA) |
|---|---|
| setWorldTransform / setEnable | `0x26D4B40` / `0x26D55E0` |
| WorldGlobal, from all 15 references | `0x6D69190` |
| Movement tick entry | `0x42820E0` |
| StringDataAlloc / PrefabPathCtor | `0x14203F0` / `0x13A7BD0` |
| ResourceLoader.load / PathNormalizeCtor | `0x12D00B0` / `0x12C6A80` |
| ResolveActorCore / ResolveActorInner | `0x2827A40` / `0x2827D50` |
| ResolveRemovalLoop / ResolveActorCtor | `0x2780450` / `0x28F23F0` |
| ResolveUuidLookup | `0x1851010` |
| GameHash / gimmick prepare | `0x1364780` / `0x278F490` |
| Probe collector method → 12-slot vtable shape | `0x39DBE20` → `0x5D13528` |
| ResolveSoServerCreate | **No disk match**; signature embeds fixed displacements. Not proof of absence live. |

Separate named-profiler lookup with existing `preset-static-20260924.py` verified
string references inside unwind-described functions (image base `0x140000000`):

| Profiler string | Referencing function VA |
|---|---|
| `SceneObjectManager::createSceneObjectFrom()` | `0x143B58180` |
| `TtCastRay` | `0x14428D080` |
| `TtWorldCastRay` | `0x1442B0B50` |
| `TtCastShape` | **Two**: `0x14428D260`, `0x14428D650` |
| `TtWorldCastShape` | `0x1442B0C50` |

The two CastShape candidates need caller/argument disambiguation; upstream's
first-string/first-reference choice does not establish it. A decoded unwind
range may branch into other ranges: it is not a complete control-flow audit.
Disk bytes do not validate runtime-unpacked code, instance layouts or safe calls.

Authoritative private reports:
`artifacts/light-research/worldbuilder-anchors-25477059-20260924-v2.json` and
`worldbuilder-profiler-25477059-20260924.json`. Earlier non-v2 report is retained;
v2 fixes an enclosing-function label and optimizes scanning, not the hit RVAs.
Targeted script suite: **10/10 passed**. No game or native tests were claimed.

## Other reuse opportunities and deliberate non-replacements

- **core 145–214:** controlled-actor RTTI discovery and periodic refresh for
  character changes; TransformSync and parent transforms handle mounted/attached
  positions. Useful independent cross-check. Layout offsets and parent-presence
  heuristics remain assumptions to validate, not a new universal player API.
- **core 1966–2049; diag.cpp 150–214:** CameraManager scene transform and FOV
  paths, plus heap camera candidates ranked by pose and manual frame lag. HTTP
  `view` is a horizontal camera-to-player vector, not a full view matrix. No
  engine-frame pairing comparable to CDT. Do not regress to heuristic camera
  copies or mistake old CAMERA_LAYOUT notes for our current production path.
- **core 356–409; thumbgen.cpp:** captured native ResourceLoader plus worker
  decode can retrieve game assets; native layout/ownership still matters.
  Thumbnail PARC/reflection, recursive subprefabs, meshes/decals and localization
  can expose semantic asset information. Thumbnail lighting is the tool's own
  preview lighting, not an engine-light/ambient extraction route.
- **notes/FORMATS.md; data/ and notes/ TSVs:** prefab/gimmick/item/character,
  localized names, textures, meshes, appearances and reflected structures form
  a navigation catalog. They are not current scene instances or light identity.
  `parse_parc.py` is a promising reflected-asset reader; no lamp statechart was
  decoded this turn. `staticinfo_dump.py` strings are not a full typed row decode.
- **scripts/rebase.py, gen_sigs.py, xref.py, disasm.py, rtti_*.py:** useful
  relocation/RTTI techniques but hardcoded paths/old targets. Prefer our bounded
  inspector for the current comparison. Asset scripts may depend on external
  Python modules and pickle caches; do not blindly execute downloaded caches.
- **HTTP:** opt-in loopback, Host/Origin guards, bounded body/header and timeouts,
  queued mutations. Useful patterns for a future control API; not a replacement
  for CDT's telemetry/WebSocket/EMA schemas. Define completion, cancellation,
  world-epoch identity, write ownership and restore behavior before adding writes.
- **overlay.cpp 298–318:** game DXGI factory/queue and per-frame backbuffer
  acquisition/release address an upstream DLSS-switch lifetime issue. Add as a
  regression-review idea, not a diagnosis of our historical crash. No equivalent
  HDR color-space path found; keep CDT SDR/HDR10/scRGB.
- **editor/input/i18n/heap/guard:** undo/project organization, raw input capture,
  localization, isolated worker allocation and exception handling are auxiliary
  ideas. Passive HUD does not need editor capture; MinGW longjmp SEH fallback is
  not safe RAII transplantation. These are not current work items.
- **Tool hazards:** `ingame_test.py` launches/controls the game and mutates scenes;
  `make_release.py` replaces outputs, unlike our immutable packages. Do not run
  either for inspection. Diagnostic ReadFile hooking is explicitly gated after
  an upstream `NvMessageBus.dll` crash. `trace_hooks=0` does not mean no hooks.
  `http_api_test.cpp` is a synthetic fake-core test, not game evidence; not run.

## Adoption and next bounded step

Adopt selectively into our **single ASI**, not a new mandatory runtime dependency
or wholesale replacement of working telemetry. MIT source permits adaptation
with its copyright/license notice; preserve separate MinHook, ImGui, stb and
other attribution from upstream `THIRD_PARTY_NOTICES` for any code actually used.
This turn adds an independent source-reading inspector, not transplanted runtime.

Next: prepare a private, default-off **observation-only native object capture**
at the validated scene-creation/enable boundary, scoped to the already documented
lamp prefab/position. Validate exact instructions, calling context and object
lifetime before instrumentation; a real invocation is needed, not another heap
scan. It must recover a current object/child and preserve original behavior
before any disable/restore experiment. If that boundary cannot observe a useful
call within the bounded test, report that limitation, not absence of the lamp.

Track physics-query validation as the next independent visibility avenue. Keep
native object control, collision visibility and update recovery as separate
acceptance questions: success in one does not validate the others. The preset
route and proven post-filter ManyLights modulation remain alternatives; no
existing research or working reader was discarded.
