# Crimson Desert Telemetry

**Live light, player, camera and local ambient data for Crimson Desert, with HTTP/WebSocket APIs, fullscreen light markers and a 3D radar.**

Inspect positions, colors and brightness of current light contributions from fires, candles, lanterns and glass/crystal lamps, alongside the player and render camera. Use the data in your own overlays, tools and lighting integrations.

[Published downloads](https://github.com/fabianviol/CrimsonDesertTelemetry/releases)
· [Watch the demo](https://youtu.be/eyRkkTXAU64)
· [API reference](docs/API.md)
· [Client examples](examples)

The [grouped and smoothed local-light stream](docs/SMOOTHED_LIGHTS.md) retains
the original contributions. The separate [ambient feed](docs/AMBIENT_STREAM.md)
carries global sky, camera-local sky visibility and their working product estimate.
Global sky RGB itself remains unoccluded.

The product goal is to answer both **how exposed the player/camera location is to
the sky and environment** and **which individual nearby game lights can actually
reach it through world geometry**, including sources off-screen or behind the
camera. Current development targets **Steam build 25246367**.

The current blocker is reliable occlusion in all its forms. Ambient passed a
controlled open/enclosed/open route, but its camera-orientation semantics still
need validation. Per-light geometry tests are not yet reliable across fires,
candles, lamps, walls, buildings and terrain, so production 2.1.10 excludes that
unfinished path. Markers can include lights behind geometry and F11 is unused.
The full current development state and preserved evidence are on GitHub; forks
and contributions are welcome. See [source visibility](docs/SOURCE_VISIBILITY.md)
and [release validation](docs/STABLE_RELEASE_VALIDATION.md).

[![Crimson Desert Telemetry: fullscreen light details and a 3D radar with the camera frustum](media/screenshot1.jpg)](https://youtu.be/eyRkkTXAU64)

*In-game light markers, separate contributions around a fire, and the player-centered 3D radar. Click the screenshot to watch the video.*

## What you get

| Data / view | Included |
|---|---|
| **Lights** | World position, current linear HDR RGB, derived linear luminance, and direction/cone half-angle for recognized spot contributions |
| **Player** | World position, physics-root forward/up vectors and heading when valid |
| **Camera** | Native render-camera position, orientation, vertical FOV, aspect ratio and near plane |
| **Fullscreen light overlay** | Markers at projected light positions; aim toward a source to inspect position, color, brightness and distance |
| **3D light radar** | Nearby light contributions with height, player heading and a camera frustum that follows pitch and roll |
| **Local API** | HTTP snapshots and health, WebSocket streaming, JSON Schema and JSON Lines recordings |
| **Local ambient** | Camera-local sky-exposure estimate with independent frame/age, alongside global sky RGB; controlled open/enclosed/open acceptance on build 25246367 |
| **Status notices** | Brief success when data becomes ready; actionable startup/build/capture errors |

Lighting, both HUD views and status notices are enabled in the supplied configuration. Each can be configured separately; the HUD is not required to consume the API.

**No NVIDIA or DLSS requirement.** The camera is read directly from the engine, not from a DLSS/Streamline data stream. Light capture and the overlay use standard DirectX 12 interfaces. Neither Nsight nor PIX is needed to run the mod. AMD/Intel game compatibility has not yet been tested.

**SDR and HDR overlays.** The DirectX 12 renderer supports 8-bit and 10-bit SDR, HDR10 (10-bit PQ/Rec.2020), and FP16 scRGB. It automatically uses the game's buffer format and color space for the HUD, light markers and notices. The recorded HDR validation includes real ImGui rendering and SDR/scRGB transitions; no live HDR-display/game test has been performed. [Validation details](docs/OVERLAY_VALIDATION.md).

## Install or upgrade

Requirements:

- Windows x64 and a supported Crimson Desert build.
- Microsoft **.NET 8 ASP.NET Core Runtime (x64)**.
- An x64 ASI loader, such as Ultimate ASI Loader, installed for the game.

1. Close Crimson Desert.
2. Download the **ModManagers.zip** asset from the release — not GitHub's source-code archive.
3. Disable the previous telemetry package and any separate legacy console ASI in your mod manager.
4. Import **and activate/deploy** the new package in Definitive Mod Manager (DMM) or JSON Mod Manager. Keep all included files together.
5. Start the game and load a save. The local host starts automatically.

DMM 2.8.1 clean deployment was verified locally. Its upgrade/removal path can
leave both Telemetry DLLs and both lowercase CFG files behind. With the game
closed, remove the old package, verify and delete only those four leftover
Telemetry files, then import the new ZIP. Do not delete your ASI loader. A complete
matrix for other systems remains open; see the [validation record](docs/MOD_MANAGER_VALIDATION.md).

Do not merge old binaries or metadata into the new package. Preserve your INI preferences, but migrate them into the current sections. Replace both `.cfg` companions together with the DLLs; they contain .NET metadata and must **not** be renamed to `.json`, which DMM can interpret as game patches. Do not leave the former console ASI in a loader search path.

### Controls and configuration

| Key | Action |
|---|---|
| **F8** | Show/hide the corner HUD and 3D radar |
| **F9** | Toggle additional diagnostics |
| **F10** | Show/hide fullscreen light markers |

The HUD does not capture mouse input. Hiding it does not stop telemetry.
These are defaults: all three shortcuts can be reassigned in the INI using decimal
Windows virtual-key codes, or individually disabled with `0`. F11 is unused.

Edit `CrimsonDesertTelemetry.ini` before starting the game:

```ini
[Server]
Enabled=1
Port=27311
SampleRateHz=60

[Lights]
Enabled=1
NearbyRadius=100
ManyLights=1
ManyLightsSampleRateHz=20

[Ambient]
Enabled=1

[Overlay]
Enabled=1
InitiallyVisible=1
ShowDetails=1
ToggleKey=119
DetailsKey=120
Radar3D=1
ShowAmbient=1
HdrPaperWhiteNits=200

[LightOverlay]
Enabled=1
InitiallyVisible=1
ToggleKey=121
Radius=35
MaxMarkers=512
MaxLabels=6

[Notifications]
Enabled=1
DurationMilliseconds=6000
```

- `[Lights] Enabled=0` disables both authored and rendered light feeds. `ManyLights=0` disables only the native GPU light capture.
- `[Ambient] Enabled=1` enables the separate sky/ambient path and requires native ManyLights capture.
- `NearbyRadius` controls the API's player-centered light radius; `LightOverlay.Radius` controls both light views. Distances are **game units**, not a claimed metre conversion.
- Disable **Overlay, LightOverlay and Notifications** to skip all UI hooks/client. The server and light capture have their own switches.
- `InitiallyVisible=0` hides an enabled view at launch; hotkeys cannot enable a view whose `Enabled=0`.
- `HdrPaperWhiteNits` controls all HDR UI brightness, including markers and notices with the corner HUD disabled. The default is 200 nits, clamped to 80–500. It does not change the game's HDR settings or metadata.
- Radar/marker swatches visualize measured HDR values; they do not reproduce the game's tone mapping. Nearby contributions share a detail box without merging, summing or smoothing their raw measurements.
- Geometric source occlusion and its hiding controls are excluded from this release. Old `SourceVisibility`, `HideOccluded` and `OcclusionToggleKey` settings cannot activate them. Raw and smoothed records remain complete.
- The camera frustum uses the real basis and view angles; its drawn length is schematic. World X/Z axes are not compass north; player-root orientation is not an animated body pose.

The production `CDT_RESEARCH=OFF` profile contains only product settings. Research,
Console, Explorer, source occlusion and the legacy `OcclusionTest` cannot be activated in this build,
including through an older INI. Research builds retain those controls in a separate
template. See [configuration dependencies and validation](docs/INI_VALIDATION.md).

### Startup and errors

There is no persistent "loading" notification. A success notice appears for six
seconds once valid player and render-camera data establish a playable world. The
native light/sky capture waits for this one-shot signal, so initial menu and shader
loading cannot consume a GPU capture transaction. The duration is configurable
from 5–10 seconds. Actionable errors may appear immediately and remain until resolved.

The status display starts independently of game-memory validation, so an unknown EXE or missing host/runtime can still produce an explanation. If the graphics overlay itself cannot run, consult the logs beside the plugin:

- `CrimsonDesertTelemetry.bootstrap.log`
- `CrimsonDesertTelemetry.host.log`
- `CrimsonDesertTelemetry.native.log`
- `CrimsonDesertTelemetry.overlay.log`

Known behavior: after returning to the title screen without restarting, data/HUD may persist briefly before becoming stale or being replaced during loading. The views remain hideable with F8/F10. Telemetry availability is not a definitive menu/loading-screen detector.
Automatic hiding in every game menu is not implemented. A live run with all
production features enabled still needs validation; synthetic UI tests do not
establish that result.

## Use the data

Connect through **HTTP** at `http://127.0.0.1:27311` or **WebSocket** at `ws://127.0.0.1:27311/v1/stream`. Both carry JSON; JSON is the data format, not a separate connection method. The server binds to **IPv4 loopback only**.

| Endpoint | Purpose |
|---|---|
| `GET /v1/snapshot` | Latest telemetry snapshot |
| `GET /v1/health` | Game, compatibility and capture health |
| `GET /v1/schema` | JSON Schema for the active payload |
| `WS /v1/stream` | Live JSON messages |

```powershell
Invoke-RestMethod http://127.0.0.1:27311/v1/snapshot
```

Product versions and API versions are separate: routes remain **HTTP API v1**. With lights enabled the additive snapshot schema is **1.4**; with lights disabled it remains **1.1**. Optional per-light metadata adds no route and changes no original RGB values. Clients should check capability/status fields and freshness instead of assuming every source is always available.

- `lights.sources` contains authored engine-light records.
- `lights.rendered.sources` contains current filtered renderer contributions, including the investigated fire/candle path, reconstructed using the camera paired with their capture.
- The additive `sourceVisibility` metadata is retained as `unknown` / `disabled` in this release. Geometric source occlusion is excluded; original records and RGB smoothing remain unchanged.
- Player/camera telemetry defaults to 60 Hz; native light capture defaults to 20 Hz. Faster API polling does not create additional GPU samples.
- The arrays **overlap**; do not add them together. One physical lamp can produce several contributions.

See [API fields, examples and freshness rules](docs/API.md) and the [client examples](examples). This is a local data API, not an API for controlling the game.

### Running from source

For development, install the .NET 8 SDK:

```powershell
dotnet run --project .\src\CrimsonDesertTelemetry.Cli -- diagnose
dotnet run --project .\src\CrimsonDesertTelemetry.Cli -- serve 27311 60 --lights
dotnet run --project .\src\CrimsonDesertTelemetry.Cli -- track 0 60 --lights > telemetry.jsonl
```

The external host alone can read player/camera and supported authored lights. **Filtered rendered lights require the unified ASI in the game.** Do not start a second host on the same port as the ASI-owned one. Status messages go to stderr; JSON Lines go to stdout.

## Compatibility and limits

The current release candidate targets **Steam build 25246367 / EXE 1.0.0.2850**, using its exact validated SHA-256. The historical 2.0.0 package targets build 25116796 / EXE 1.0.0.2760. Older player/camera profiles remain preserved; those historical checks do not establish compatibility of a current native package with an older or unknown build.

Current user reports, including missing DMM host companion files and a separately
reported graphics crash with unproven cause, are tracked in
[compatibility issues](docs/COMPATIBILITY_ISSUES.md). These reports do not establish
a shared cause or a verified fix.

Native capture validates the EXE, hook instructions and surrounding caller/binding contexts before instrumentation. Shared build contracts and the read-only command below help recover after updates:

```powershell
dotnet run --project .\src\CrimsonDesertTelemetry.Cli -- check-update 'C:\path\to\CrimsonDesert.exe'
```

The report **does not enable an unknown build**. Historical basic-telemetry layouts have guarded automatic relocation, but the current direct-camera/ManyLights path is not automatically promoted. Future engine or shader-only asset changes can still require an update. See the [update recovery reference](docs/UPDATE_RECOVERY.md).

Important boundaries:

- This is current **filtered renderer data**, not a complete registry of every visible light. Sun, sky, emissive surfaces and every possible effect are not all covered.
- A missing contribution is **not** a permanent physical OFF state. Stable lamp IDs, physical lumens and validated light ranges are not supplied.
- Linear HDR RGB/luminance can vary with effects and exposure; they are not final screen pixels or exposure-normalized lamp colors.
- The radar can show behind-camera contributions still present in the feed, but is **not** a complete 360-degree registry.
- Fullscreen markers include lights behind geometry: geometric source occlusion and F11 hiding are excluded from this release after failed live acceptance. The [preserved investigation](docs/SOURCE_VISIBILITY_REGRESSION.md) will resume separately. Fast motion can expose capture/projection latency.
- HDR UI is composited in linear light, with configurable white brightness and unchanged pixels outside the UI. It does not tone-map the whole scene. Rendering HDR UI uses two extra full-resolution GPU textures plus a scene copy/composite; the SDR path has no extra compositor pass.
- Unrecognized output format/color-space combinations remain unsupported. Automated HDR rendering tests do not establish live HDR game or display compatibility; frame generation, other upscalers and AMD/Intel game setups remain unvalidated.
- The external host reads process memory. The unified ASI uses guarded renderer hooks, GPU copies and optional UI hooks; the full system is **not purely read-only instrumentation**. The console that can change debug values is available only in a separate `CDT_RESEARCH=ON` build.

## Build, test and contribute

```powershell
dotnet build .\CrimsonDesertTelemetry.sln -c Release
dotnet run --project .\tests\CrimsonDesertTelemetry.Tests -c Release
.\scripts\Build-ModManagerPackage.ps1 -Version 0.0.0-local.1 -Research off
ctest --test-dir build/native-package-release -C Release --output-on-failure
```

The native build requires Visual Studio C++/Windows SDK/CMake; see [local tooling](docs/TOOLING.md) for executable paths. Choose an unused local test version: package builds refuse to overwrite an existing versioned ZIP.

The production package uses `CDT_RESEARCH=OFF`; bounded ambient acquisition is included, while per-light SDF acquisition is excluded. `-Research on` preserves the experimental paths and selects [CrimsonDesertTelemetry.research.ini](packaging/mod-manager/CrimsonDesertTelemetry.research.ini), packaged under the normal INI filename. The separate profiles prevent old diagnostic settings from replacing product streams or offering inactive controls. See [INI validation](docs/INI_VALIDATION.md).

GitHub CI covers managed/API tests and native capture, guard and UI tests. Current acceptance and outstanding checks are recorded in [the handover](docs/HANDOVER.md); historical test totals are not a result for a new package.

See [contributing](CONTRIBUTING.md), [research provenance](docs/PROVENANCE.md), [2.0 release notes](docs/releases/v2.0.0.md), [Nexus publishing](docs/NEXUS_PUBLISHING.md) and [current public description drafts](docs/PUBLIC_DESCRIPTIONS.md). Game binaries, memory dumps, private captures and third-party checkout directories do not belong in the public repository.

## Credits and scope

Created and maintained by [fabianviol](https://github.com/fabianviol), developed with **Claude** and **Codex (OpenAI)**. Runtime third-party components are credited in [provenance](docs/PROVENANCE.md) and the package's `THIRD-PARTY-NOTICES.txt`.

This unofficial community project is not affiliated with or endorsed by Pearl Abyss. Use it only where game terms and applicable restrictions permit; no anti-cheat bypass or competitive advantage is provided. Source is licensed under the [MIT License](LICENSE).
