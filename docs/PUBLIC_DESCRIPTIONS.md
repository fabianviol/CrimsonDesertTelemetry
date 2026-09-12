# Prepared public descriptions — 2026-09-12

The GitHub About text below was applied on 2026-09-12. The Nexus description is
a prepared draft; the external Nexus page was not changed and no release was
created. Public package 2.0.2 and current development are deliberately separated.
Check [the current checkpoint](HANDOVER.md) before reusing these time-sensitive
status statements. Exact package/scan records belong with their own artifacts;
these descriptions make no antivirus verdict or release-readiness claim.

## GitHub About text

Crimson Desert light, player/camera and ambient telemetry via local HTTP/WebSocket. Configurable HUD and geometric source visibility metadata; production validation in progress.

## Nexus description draft — English

### Crimson Desert Telemetry

Inspect live light contributions, player position and the render camera in
Crimson Desert. The mod provides fullscreen light markers, a 3D light radar and
local HTTP/WebSocket APIs for your own overlays, tools and lighting integrations.
It does not drive physical lamps itself.

### Published package: 2.0.2

The published 2.0.2 ASI package targets Steam build **25246367** (executable
**1.0.0.2850**). Unknown executable builds are rejected by native capture.

- Current rendered light positions, linear HDR RGB, derived luminance and
  recognized spotlight directions/cone angles.
- Separate authored light records, plus a grouped and smoothed local-light feed.
- Player position/root orientation and native render-camera position,
  orientation and projection.
- Fullscreen light markers, a 3D radar and status notices. Defaults: **F8** for
  the corner HUD/radar, **F9** for diagnostics and **F10** for fullscreen markers.
- Local HTTP snapshots, health, schema and WebSocket streaming. The default
  address is `http://127.0.0.1:27311`; WebSocket is
  `ws://127.0.0.1:27311/v1/stream`.

The ambient path is disabled in the published 2.0.2 configuration. That download
does **not** provide the new production per-light occlusion metadata or F11
occlusion-hiding mode described below.

### In development: ambient and per-light occlusion

These are development features, not a new public release announcement.

**Camera-local Ambient Occlusion:** an OFF v2.1.9 test package passed a controlled
open/enclosed/open game route on build 25246367 on 12 September 2026. The separate
ambient API reports global sky, camera-local sky visibility and their working
product estimate. Global sky RGB itself remains unoccluded. This is not a
measurement of physical room brightness, and its reference is the camera rather
than the player root.

**Per-light Source Occlusion:** the current production implementation adds
clear/blocked/unknown geometry metadata to each rendered contribution and
preserves it in the smoothed feed's original contributions. Raw records,
RGB/luminance and EMA color values remain unchanged. Blocked lights are retained
in both API feeds.

The geometric test runs from the paired capture camera to an individual source
center. It supports off-screen or behind-camera targets that remain in the
current source list; the source feed is still view-filtered and is not a complete
360-degree registry. An absent, unknown or stale result does not mean a light is
off or blocked. A source-center result also does not describe its entire
illuminated region.

The implementation has synthetic test coverage. The current camp test exposed
falsely clear concealed lamps; corrections and live validation remain in progress.
Its real visible/blocked/visible and behind-camera acceptance is still pending. Both
ambient and per-light goals must pass before a final release. Production uses
bounded current-data acquisition; private research capture runs and histories
are not required.

The development production profile (`CDT_RESEARCH=OFF`) does not expose or
activate Research, Console, Explorer or the legacy `OcclusionTest`. Old INI
switches cannot enable them. Research remains preserved under `CDT_RESEARCH=ON`
with its own INI template. This corrects conflicting or inactive configuration
options; it is not an antivirus mitigation. See [INI validation](INI_VALIDATION.md).

**Development HUD controls:** the radar and markers show visibility metadata.
Optional `HideOccluded=1` hides only fresh known blocked sources, while unknown
or stale sources stay visible. **F11** toggles this display mode; it does not
filter either API feed. All four shortcuts can be reassigned using decimal
Windows virtual-key codes or individually disabled with `0`:

| INI section | Setting | Default |
| --- | --- | --- |
| `Overlay` | `ToggleKey` | `119` / F8 |
| `Overlay` | `DetailsKey` | `120` / F9 |
| `LightOverlay` | `ToggleKey` | `121` / F10 |
| `LightOverlay` | `OcclusionToggleKey` | `122` / F11 |

These four-key controls describe the development build; they must not be assumed
present in the published 2.0.2 package.
Automatic HUD hiding in every game menu is not implemented. Live validation with
all production features enabled is still pending; synthetic coverage does not
establish game compatibility for that combination.

### Requirements and installation

Windows x64, the Microsoft .NET 8 ASP.NET Core Runtime (x64), an x64 ASI loader,
and the supported game build are required. No DLSS, Streamline, NVIDIA, Nsight or
PIX runtime dependency is required. Actual AMD/Intel game compatibility remains
untested. The D3D12 overlay has automated SDR/HDR10/scRGB coverage; live HDR-display
and frame-generation acceptance remain incomplete.

Close the game, disable the old telemetry package and any separate
CrimsonHueConsole ASI, then import and activate the complete ModManagers ZIP in
DMM or JSON Mod Manager. Preserve useful INI preferences and migrate them into
the current sections. Keep all runtime companions together:

```text
CrimsonDesertTelemetry.asi
CrimsonDesertTelemetry.Core.dll
CrimsonDesertTelemetry.ini
crimson-desert-telemetry.dll
crimson-desert-telemetry.deps.cfg
crimson-desert-telemetry.runtimeconfig.cfg
```

Keep both `.cfg` names unchanged; DMM can interpret loose `.json` files as game
patches. Complete ZIP contents alone do not prove complete manager deployment.
A full install/uninstall matrix for both managers remains open.

### Known unresolved 2.0.2 report

A user reported that DMM omitted the lowercase host DLL and both `.cfg`
companions. Copying those files from the same package resolved their missing-host
error, but a separate crash with graphics features enabled remained. The
preserved local 2.0.2 ZIP contains all three files; the reporter's exact ZIP hash,
DMM version and crash environment are still unverified.

The graphics-crash cause has not been established. The current occlusion work
is **not a confirmed fix** for either the DMM deployment problem or that crash.
Useful diagnostic details include package/DMM versions, deployed file hashes,
the full INI, GPU/driver, other graphics mods and the bootstrap/native logs with
the faulting module and exception offset.

### What the light data means

Authored and rendered arrays overlap; do not add them together. One physical
lamp may produce several contributions. A missing contribution can reflect
culling or unloading rather than a permanent OFF state. Linear HDR RGB is
renderer data, not physical lumens, exposure-normalized lamp color or final
screen pixels. Display hiding changes only presentation.

Source and documentation: [GitHub](https://github.com/fabianviol/CrimsonDesertTelemetry).
See the [API reference](https://github.com/fabianviol/CrimsonDesertTelemetry/blob/main/docs/API.md)
for contracts, limits and freshness rules, and
[compatibility reports](https://github.com/fabianviol/CrimsonDesertTelemetry/blob/main/docs/COMPATIBILITY_ISSUES.md)
for the unresolved report.

Created by fabianviol with Claude and Codex (OpenAI). Unofficial community project;
not affiliated with or endorsed by Pearl Abyss. MIT-licensed source; bundled
dependencies retain their own notices.
