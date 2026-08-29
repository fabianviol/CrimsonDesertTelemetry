# Crimson Desert Telemetry

Crimson Desert Telemetry is an independent, open-source, read-only telemetry layer for Crimson Desert. It exposes player and render-camera state through a local HTTP/WebSocket API and newline-delimited JSON so lighting systems, overlays, accessibility tools, and other community projects can consume the same neutral interface.

The project does not write to or inject code into the game process. Unknown game builds are rejected instead of being guessed.

## Current support

| Distribution | Build | Executable version | Status |
|---|---:|---:|---|
| Steam | `24994088` | `1.0.0.2658` | Locally validated |

Currently exposed:

- authoritative player world position;
- render-camera world position;
- camera up, right, and forward vectors;
- near/far plane, vertical field of view, and aspect ratio;
- consensus health data from redundant renderer copies.

The camera record is discovered structurally on every game launch. Absolute addresses are deliberately not stored because they change between sessions.

## Quick start

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
| `GET /v1/snapshot` | Most recent complete snapshot; HTTP 503 until available |
| `GET /v1/schema` | JSON Schema for the public payload |
| `WS /v1/stream` | Real-time JSON messages |

Start a different port or sample rate with `serve [port] [rate-hz]`. Rates from 1 through 240 Hz are accepted. The server can be launched before the game and reconnects after game restarts.

During a loading transition the stream emits a `game.state` of `loading`; after a game exit it emits `stopped`. In those state messages `player`, `camera`, and `quality` are `null`, so clients never need to mistake an old transform for current data.

JSON Lines remains available for scripts and recordings. `track` accepts an optional sample count (`0` means unlimited) and sample rate in Hz. Status messages go to stderr; stdout contains payloads only:

```powershell
dotnet run --project .\src\CrimsonDesertTelemetry.Cli -- track 0 60 > telemetry.jsonl
```

Example output (values shortened):

```json
{"schemaVersion":"1.0","sequence":0,"capturedAt":"2026-08-29T20:00:00Z","game":{"build":"24994088","state":"playing"},"coordinateSystem":{"unit":"game-unit","handedness":"right","upAxis":"y"},"capabilities":["player.position","camera.transform","camera.projection"],"player":{"position":{"x":-11157.0,"y":761.3,"z":-5969.5}},"camera":{"position":{"x":-11155.0,"y":763.0,"z":-5963.8},"up":{"x":0.0,"y":1.0,"z":0.0},"right":{"x":1.0,"y":0.0,"z":0.0},"forward":{"x":0.0,"y":0.0,"z":1.0},"nearPlane":0.2,"farPlane":null,"verticalFovDegrees":50.0,"aspectRatio":1.775},"quality":{"consensusCopies":48,"validCopies":100,"distinctStates":7,"rediscovered":false,"captureDurationMicroseconds":510}}
```

## How it works

The build definition resolves the game's static player-position globals through a version-bound signature. Camera discovery scans writable process memory for a complete render-camera record and accepts it only when all of these independent checks agree:

- position is within a plausible distance of the player;
- basis vectors are finite, unit length, mutually orthogonal, and right-handed;
- clip planes, field of view, and aspect ratio are plausible;
- the surrounding sentinel layout matches the observed renderer record.

The renderer retains many current and historical copies. At runtime the tracker refreshes the discovered address family and selects the largest quantized consensus state. Polling the cached family is sub-millisecond on the development machine. If every cached copy disappears after a load or region transition, the tracker performs a fresh structural discovery automatically. All clients share this one sampler; increasing the number of WebSocket clients does not cause additional game-memory scans.

The validated layout and evidence are documented in [docs/CAMERA_LAYOUT.md](docs/CAMERA_LAYOUT.md). The public payload contract is available as [schema/telemetry-v1.schema.json](schema/telemetry-v1.schema.json). Research provenance is recorded in [docs/PROVENANCE.md](docs/PROVENANCE.md).

## Build and test

```powershell
dotnet build .\CrimsonDesertTelemetry.sln -c Release
dotnet run --project .\tests\CrimsonDesertTelemetry.Tests -c Release
dotnet publish .\src\CrimsonDesertTelemetry.Cli -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -o .\artifacts\win-x64
```

## Safety and scope

This is an unofficial community project and is not affiliated with or endorsed by Pearl Abyss. Reading another process may still be restricted by game terms or anti-cheat software. Use it only where permitted, do not use it to gain a competitive advantage, and never submit game binaries or memory dumps to this repository.

The code is licensed under the [MIT License](LICENSE). See [CONTRIBUTING.md](CONTRIBUTING.md) before submitting support for another game build.
