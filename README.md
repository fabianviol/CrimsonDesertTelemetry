# Crimson Desert Telemetry

Crimson Desert Telemetry is an independent, open-source telemetry layer for Crimson Desert. It exposes player, render-camera and lighting data through a local HTTP/WebSocket API and newline-delimited JSON. CrimsonHue is a separate, future Philips Hue consumer of this API, not the telemetry implementation.

[Download the mod-manager package](https://github.com/fabianviol/CrimsonDesertTelemetry/releases)
| [API reference](docs/API.md) | [Client examples](examples)

The external telemetry host opens the game for read-only access. One ASI starts that host and contains the native ManyLights capture, optional Dear ImGui HUD and optional research console. ManyLights capture instruments the renderer and copies its filtered light buffer; it is not an untouched/read-only game run. Version 2.0 enables both HUD views; the research console stays off. Disabling the HUD does not disable lighting capture. Basic telemetry can recognize guarded compatible builds; native lighting instrumentation requires the exact validated executable hash and instruction signatures and otherwise fails closed.

## Current support

| Distribution | Build | Executable version | Status |
|---|---:|---:|---|
| Steam | `24994088` | `1.0.0.2658` | Locally validated |
| Steam | `25050808` | `1.0.0.2692` | Automatically recognized, then locally validated |
| Steam | `25116796` | `1.0.0.2760` | Locally validated, player position and orientation re-verified in 1.2.1 |

The table lists manually tested builds. An EXE with the same bytes has the same
SHA-256 on every machine, but a displayed game-build number does not by itself
prove that every distributed EXE is byte-identical. Exact known hashes take the
tested path. For a different hash, telemetry can relocate the validated layout
using unique instruction, writable-global, player-type and multi-slot vtable
guards. Health reports this as `compatibility.mode: "automatic"`, never as manually
tested. Ambiguous, incomplete or implausible candidates are rejected. Automatic
recognition covers the historical camera layout, not automatic promotion of the
current direct-camera/ManyLights path. Use the read-only `check-update <exe>` command
for individual current-path anchors; candidates never enable native hooks. See the
[update recovery reference](docs/UPDATE_RECOVERY.md). Shared build contracts and
strict runtime guards reduce repair work, but do not guarantee future compatibility
or detect every shader-only asset change.

Currently exposed:

- authoritative player world position;
- player physics-root forward/up basis and derived heading when valid;
- render-camera world position;
- camera up, right, and forward vectors;
- near plane, vertical field of view, and aspect ratio (`farPlane` is currently unknown/null);
- single-source validation and capture timing.

Builds `25050808` and `25116796` support nearby authored engine-light records.
The unified **2.0.0 package** additionally captures current filtered
ManyLights on exact build `25116796`, including the researched fire, candle and
glass/crystal light contributions. Lights are enabled by default:

```ini
[Lights]
Enabled=1
NearbyRadius=100
ManyLights=1
ManyLightsSampleRateHz=20
```

Schema 1.4 keeps authored records in `lights.sources` and adds current rendered
contributions in `lights.rendered.sources`: world position, current linear HDR RGB,
derived linear luminance and, for recognized spots, direction and cone half-angle.
The latter uses the camera saved with that capture, not the latest camera. Do not
add both arrays together: they overlap. A missing rendered contribution is not a
durable OFF state; culling and scene loading also affect visibility. Stable physical
IDs, physical lumens and coverage of every lighting mechanism are not claimed.
With `[Lights] Enabled=0`, schema 1.1 remains unchanged. The unified capture passed
local cold-start, moving-camera and physical lamp A-B-A checks on build `25116796`.
This does not establish broad hardware support or complete lighting coverage.

**DLSS is not required.** Camera data comes directly from the game's native
render-camera source, including with upscaling disabled. That source is resolved
through build-guarded game globals on every launch; no previous-process heap
address, Streamline API call or camera heap scan is involved.

The native source passed a cold start with upscaling disabled and a controlled
yaw/pitch change on the development NVIDIA setup. AMD/Intel hardware remains
untested; renderer independence is not a claim of tested support on every GPU.
See [native camera evidence and remaining tests](docs/ENGINE_CAMERA_RESEARCH.md).

## Quick start

### Startup and loading status

Normal startup, scene loading and discovery are silent. After the scene is playing
and requested telemetry is ready, a status notice appears for six seconds.
Actionable errors can appear immediately, including unsupported EXEs or a missing
host; they do not depend on enabling game-memory hooks or the full HUD. Unresolved
errors stay visible. A visible top-left HUD
moves the notices beside it (below on narrow screens). They share the existing
D3D12/SDR drawing backend; HDR or graphics-hook failure can prevent on-screen notices,
in which case check the bootstrap/native/overlay logs.

`[Notifications] Enabled=1` enables these notices in the package;
`DurationMilliseconds=6000` controls the ready duration (clamped to 5–10 seconds). Set it to `Enabled=0` to
disable them. No HUD hotkeys become active solely because notices are enabled.

For integrations, start with the [API reference and client examples](docs/API.md).
It documents every endpoint and data field, coordinate conventions, null/state
handling, WebSocket delivery, freshness and reconnection.

### Optional in-game HUD

Version 2.0 enables both HUD views. Missing configuration
still defaults them off. Startup notices have their separate setting above.
Edit `CrimsonDesertTelemetry.ini` before restarting the game:

```ini
[Overlay]
Enabled=1
Radar3D=1

[LightOverlay]
Enabled=1
Radius=35
MaxMarkers=512
MaxLabels=6
```

`InitiallyVisible=0` only starts an enabled HUD hidden; it does **not** disable its
hooks or client. F8 cannot activate a HUD disabled with `Enabled=0`.

The English, passive Dear ImGui HUD shows independent player-root and camera
headings, camera pitch/FOV and player XYZ below a full-width oblique 3D radar.
The radar shows current light contributions with height stems and a spatial
camera frustum: position, pitch, roll and horizontal/vertical view angles use the
camera basis and FOV. Its displayed length is schematic, not a measured view range.
`Radar3D=0` restores the original compass. **F8** toggles this corner HUD; **F9**
toggles diagnostics. **F10** independently toggles fullscreen world-light markers,
including position, RGB, linear-luminance and distance labels. Close contributions
(at most 0.15 game units apart pairwise) share a detail box, retaining separate
values in spatial order. This is only presentation grouping, not physical-object
identity or a combined appearance estimate. Raw API data and real light variation
remain unchanged; transient GPU-slot numbers appear only with F9 diagnostics.
Spot arrows
use a schematic length, not measured light range. No mouse input is captured.
Key codes, corner, scale and opacity are
configured in `CrimsonDesertTelemetry.ini` before launch.

Both light views use the current **filtered rendered feed**, not a persistent lamp
registry or the sum of authored/rendered sources. Behind-camera lights appear on
the radar only if still present in that feed. No historical markers are retained
as live data. The configured radius is in game units, not metres. Fullscreen
markers have **no depth test** and can appear through walls. Marker colors are an
SDR visualization of HDR values, not an exact reconstruction of scene tone mapping.
World positions retain capture-paired reconstruction; screen projection uses the
latest published camera. Fast-movement alignment requires a live visual check.
Disable Overlay, LightOverlay and Notifications to disable all UI hooks/client.

`AutoScale=1` scales the panel and font with render resolution (2x at 2160p relative
to 1080p); `Scale` remains an additional multiplier.

The camera reader follows the native source directly, without temporal copy selection,
smoothing or direction clamping. Failed validation publishes unavailable telemetry.

This initial renderer supports **DirectX 12, 8-bit SDR only**. HDR, frame-generation
compatibility and NVIDIA recording are not yet verified. Invalid or stale telemetry
is displayed as unavailable, not as live coordinates. World-axis labels are not
compass directions; player-root orientation is not the animated body pose.

The development setup passed a user-observed in-game restart test with the native
camera source. Broader compatibility tests remain open; see
[the overlay test checklist](docs/OVERLAY_VALIDATION.md). Lighting is separately
configurable and build-gated. Disabling/uninstalling the ASI requires closing the game; hot-unload
is intentionally unsupported.

### Mod-manager package

The ASI package targets Definitive Mod Manager and JSON Mod Manager. It
requires an ASI loader and the .NET 8 ASP.NET Core Runtime (x64); the host starts
in the background with the game. Full manager and in-game validation is still
in progress; see [the package test record](docs/MOD_MANAGER_VALIDATION.md).

Build the package with `scripts/Build-ModManagerPackage.ps1`. Keep all companion
files together. The `.cfg` files are .NET metadata, not game-patch JSON. The
bootstrap caches only the runtime configuration under
`%LOCALAPPDATA%\CrimsonDesertTelemetry\Runtime`, outside mod/game folders.

Current public version 1.2.1 keeps the existing `/v1/` endpoints. The default
player/camera payload remains JSON schema 1.1; opt-in lights use additive 1.2.
These version numbers are independent. Breaking public API changes require a new
major API/product version; clients should tolerate additive fields and capabilities.

Upgrading from 1.2.0 on build `25116796` moves `player.position` by roughly 6.5
units: that version reported the render camera under the player field, and 1.2.1
reports the player. Consumers that relied on it as an eye position should read
`camera.position` instead. Player orientation was absent in 1.2.0 on this build
and is published again.

When upgrading, close the game, disable the mod, and replace the old folder
completely rather than merging over it. Merging leaves stale `.json` files behind
that DMM keeps misclassifying, and it can leave old `.cfg` metadata in the game
directory while the deployed DLLs are already current. Replace **both `.cfg`
companions** together with the binaries; only the INI carries user settings, so
preserve that one.

The mod has to be activated in the manager after import, not only imported.
Deploying installs `CrimsonDesertTelemetry.asi` and its INI into `bin64`; without
that step the plugin never loads, and a manager may report that it cannot find the
ASI configuration.

### Standalone host

Requirements: Windows x64 and the .NET 8 SDK. Start Crimson Desert and load into the world, then run:

```powershell
dotnet run --project .\src\CrimsonDesertTelemetry.Cli -- diagnose
dotnet run --project .\src\CrimsonDesertTelemetry.Cli -- snapshot
dotnet run --project .\src\CrimsonDesertTelemetry.Cli -- serve
```

The server binds to loopback only and defaults to `http://127.0.0.1:27311` at 60 Hz:

| Endpoint | Purpose |
|---|---|
| `GET /v1/health` | Game, build, discovery, and stream health |
| `GET /v1/snapshot` | Latest published snapshot, including unavailable states; 503 with a health body before the first publication |
| `GET /v1/schema` | JSON Schema for the public payload |
| `WS /v1/stream` | Real-time JSON messages |

Start a different port or sample rate with `serve [port] [rate-hz]`. Rates from 1 through 240 Hz are accepted. The server can be launched before the game and reconnects after game restarts.

When required reads fail, the stream emits `game.state: loading`; this is telemetry
unavailability, not a confirmed loading-screen flag. A surviving host can emit
`stopped` after game exit; an ASI-owned host may disconnect first. These state
messages have null `player`, `camera`, and `quality` fields. Also check timestamps
and health: the latest snapshot is retained across health-only errors, and slow
WebSocket clients can skip samples. See [delivery and freshness](docs/API.md#websocket-delivery-and-freshness).

JSON Lines remains available for scripts and recordings. `track` accepts an optional sample count (`0` means unlimited) and sample rate in Hz. Status messages go to stderr; stdout contains payloads only:

```powershell
dotnet run --project .\src\CrimsonDesertTelemetry.Cli -- track 0 60 > telemetry.jsonl
```

Example output (values shortened):

```json
{"schemaVersion":"1.1","sequence":0,"capturedAt":"2026-08-29T20:00:00Z","game":{"build":"24994088","state":"playing"},"coordinateSystem":{"unit":"game-unit","handedness":"right","upAxis":"y"},"capabilities":["player.position","camera.transform","camera.projection","player.orientation"],"player":{"position":{"x":-11157.0,"y":761.3,"z":-5969.5},"orientation":{"source":"player-physics-root","forward":{"x":0.0,"y":0.0,"z":1.0},"up":{"x":0.0,"y":1.0,"z":0.0},"headingDegrees":0.0}},"camera":{"position":{"x":-11155.0,"y":763.0,"z":-5963.8},"up":{"x":0.0,"y":1.0,"z":0.0},"right":{"x":1.0,"y":0.0,"z":0.0},"forward":{"x":0.0,"y":0.0,"z":1.0},"nearPlane":0.2,"farPlane":null,"verticalFovDegrees":50.0,"aspectRatio":1.775},"quality":{"consensusCopies":1,"validCopies":1,"distinctStates":1,"rediscovered":false,"captureDurationMicroseconds":510}}
```

The optional `player.orientation` capability is emitted only when the validated physics-root read is valid for that frame. During loading or an actor transition, `player.orientation` is `null` and the capability is omitted; consumers must not reuse the previous direction.

## How it works

For an exact known SHA-256, the tested definition is used. For another hash, all
required instructions, globals and object tables must first be relocated uniquely
from a manually validated reference layout. Each camera capture then requires:

- position is within a plausible distance of the player;
- basis vectors are finite, unit length, mutually orthogonal, and right-handed;
- near plane, symmetric perspective projection, field of view and aspect ratio are plausible;
- the direct camera global and the main-root chain identify the same camera;
- context and camera vtables match the selected guarded layout;
- automatically recognized builds also produce a validated player-root RTTI chain before publishing;
- two successive camera-field reads and surrounding chain/counter checks agree.

At most three read attempts are made per capture. Source pointers are followed again
each time; missing objects or a render-context counter stalled for one second make
the camera unavailable. These external read checks reduce torn/stale reads but do
not guarantee engine-frame atomicity. Timestamps are sampling times, not engine times.
All clients share one sampler. The v1 quality field names remain compatible:
`consensusCopies`, `validCopies`, and `distinctStates` are each 1 for the single
native source; `rediscovered` marks a changed object chain/source. The native raw
far-related field is not exported as a validated distance; `farPlane` is null.

Current native-source evidence is in [docs/ENGINE_CAMERA_RESEARCH.md](docs/ENGINE_CAMERA_RESEARCH.md);
[docs/CAMERA_LAYOUT.md](docs/CAMERA_LAYOUT.md) describes the historical scanner only.
The live test record is in [docs/VALIDATION.md](docs/VALIDATION.md). The public
payload contract is documented in [docs/API.md](docs/API.md) and
[schema/telemetry-v1.schema.json](schema/telemetry-v1.schema.json). Research
provenance is recorded in [docs/PROVENANCE.md](docs/PROVENANCE.md).

## Build and test

```powershell
dotnet build .\CrimsonDesertTelemetry.sln -c Release
dotnet run --project .\tests\CrimsonDesertTelemetry.Tests -c Release
dotnet publish .\src\CrimsonDesertTelemetry.Cli -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -o .\artifacts\win-x64
```

## Releasing

Tagging `v*` builds the mod-manager package and drafts a GitHub release. Pushing
that package to the [Nexus Mods page](https://www.nexusmods.com/crimsondesert/mods/3374)
runs over the Nexus Mods v3 API, either from the `Publish to Nexus Mods` workflow
or from a local PowerShell session; both are dry runs until explicitly told
otherwise. See [docs/NEXUS_PUBLISHING.md](docs/NEXUS_PUBLISHING.md).

## Safety and scope

This is an unofficial community project and is not affiliated with or endorsed by Pearl Abyss. Reading another process may still be restricted by game terms or anti-cheat software. Use it only where permitted, do not use it to gain a competitive advantage, and never submit game binaries or memory dumps to this repository.

The code is licensed under the [MIT License](LICENSE). See [CONTRIBUTING.md](CONTRIBUTING.md) before submitting support for another game build.
