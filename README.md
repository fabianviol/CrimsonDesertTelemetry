# Crimson Desert Telemetry

Crimson Desert Telemetry is an independent, open-source, read-only telemetry layer for Crimson Desert. It exposes player and render-camera state through a local HTTP/WebSocket API and newline-delimited JSON so lighting systems, overlays, accessibility tools, and other community projects can consume the same neutral interface.

[Download the mod-manager package](https://github.com/fabianviol/CrimsonDesertTelemetry/releases)
| [API reference](docs/API.md) | [Client examples](examples)

The external telemetry host opens the game for read-only access. The optional ASI starts and manages that host and includes a separately configurable Dear ImGui HUD, **disabled by default**. When enabled, the HUD hooks DXGI presentation functions to draw inside the game; it does not modify gameplay values. With `[Overlay] Enabled=0`, no HUD hooks, hotkeys or HUD WebSocket client start; telemetry remains active. New executable builds are accepted only if the complete guarded layout can be resolved unambiguously; otherwise they fail closed.

## Current support

| Distribution | Build | Executable version | Status |
|---|---:|---:|---|
| Steam | `24994088` | `1.0.0.2658` | Locally validated |
| Steam | `25050808` | `1.0.0.2692` | Automatically recognized, then locally validated |
| Steam | `25116796` | `1.0.0.2760` | Locally validated |

The table lists manually tested builds. An EXE with the same bytes has the same
SHA-256 on every machine, but a displayed game-build number does not by itself
prove that every distributed EXE is byte-identical. Exact known hashes take the
tested path. For a different hash, telemetry can relocate the validated layout
using unique instruction, writable-global, player-type and multi-slot vtable
guards. Health reports this as `compatibility.mode: "automatic"`, never as manually
tested. Ambiguous, incomplete or implausible candidates are rejected. Automatic
recognition reduces routine update work; it is not a guarantee that every future
engine layout remains compatible.

Currently exposed:

- authoritative player world position;
- player physics-root forward/up basis and derived heading when valid;
- render-camera world position;
- camera up, right, and forward vectors;
- near plane, vertical field of view, and aspect ratio (`farPlane` is currently unknown/null);
- single-source validation and capture timing.

Builds `25050808` and `25116796` additionally support an **optional, disabled-by-default** nearby
engine-light feed. Enable it before launch with:

```ini
[Lights]
Enabled=1
NearbyRadius=100
```

It reports only verified engine light records inside that player-centred radius.
Fire/effect illumination, physical lumens, range and a generic `enabled` claim are
not included. With the module disabled, schema 1.1 and the existing player/camera
payload remain unchanged; enabled light output uses additive schema 1.2.

**DLSS is not required.** Version 1.0.0 reads camera data directly from the game's
native render-camera source, including with upscaling disabled. It resolves this
source through build-guarded game globals on every launch. No previous-process
heap address, Streamline API call or camera heap scan is required.

The native source passed a cold start with upscaling disabled and a controlled
yaw/pitch change on the development NVIDIA setup. AMD/Intel hardware remains
untested; renderer independence is not a claim of tested support on every GPU.

Historical research only: the DLSS-dependent camera-copy approach used in
preview.6 was replaced before v1.0.0. It is retained only in an explicit research
command and is never used by the release's normal telemetry reader or as a fallback.
See [native camera evidence and remaining tests](docs/ENGINE_CAMERA_RESEARCH.md).

## Quick start

For integrations, start with the [API reference and client examples](docs/API.md).
It documents every endpoint and data field, coordinate conventions, null/state
handling, WebSocket delivery, freshness and reconnection.

### Optional in-game HUD

The HUD is completely disabled by default, including when its configuration is
missing. To opt in, edit `CrimsonDesertTelemetry.ini` and restart the game:

```ini
[Overlay]
Enabled=1
```

`InitiallyVisible=0` only starts an enabled HUD hidden; it does **not** disable its
hooks or client. F8 cannot activate a HUD disabled with `Enabled=0`.

The English, passive Dear ImGui HUD shows independent player-root and camera
headings, camera pitch/FOV and player XYZ. **F8** toggles visibility; **F9** toggles
diagnostics. It never captures the mouse. Key codes, corner, scale and opacity are
configured in `CrimsonDesertTelemetry.ini` before launch.

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
[the overlay test checklist](docs/OVERLAY_VALIDATION.md). Engine-light telemetry is
opt-in and build-gated. Disabling/uninstalling the ASI requires closing the game; hot-unload
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

Current public version 1.2.0 keeps the existing `/v1/` endpoints. The default
player/camera payload remains JSON schema 1.1; opt-in lights use additive 1.2.
These version numbers are independent. Breaking public API changes require a new
major API/product version; clients should tolerate additive fields and capabilities.

When upgrading the first test package, replace its old folder completely while
the mod is disabled and the game is closed. Merging over it leaves old `.json`
files that DMM will continue to misclassify. Preserve customized INI settings.
If a manager preserves your previous `[Overlay] Enabled=1`, change it to `0` to
use the new disabled default. Existing installations are not silently overridden.

On upgrades, replace **both `.cfg` companions** together with the binaries; only
the INI contains user settings. In a local DMM upgrade, the game directory retained
old `.cfg` metadata even though the library and deployed DLLs were current.
If this occurs, close the game and replace those two files from the new package.

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

## Safety and scope

This is an unofficial community project and is not affiliated with or endorsed by Pearl Abyss. Reading another process may still be restricted by game terms or anti-cheat software. Use it only where permitted, do not use it to gain a competitive advantage, and never submit game binaries or memory dumps to this repository.

The code is licensed under the [MIT License](LICENSE). See [CONTRIBUTING.md](CONTRIBUTING.md) before submitting support for another game build.
