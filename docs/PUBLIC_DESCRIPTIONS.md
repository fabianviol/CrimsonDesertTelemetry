# Prepared public descriptions — stability release, 2026-09-12

These texts describe candidate 2.1.10-rc.1, not the old Nexus 2.0.2 download.
They have not been posted to Nexus. Use them only with the corresponding package
after its [release checks](STABLE_RELEASE_VALIDATION.md). The external graphics
crash is unresolved; this candidate is not a confirmed fix for that user's system.
The GitHub About text below was applied on 2026-09-12. Nexus file publication
preflight passed in read-only dry-run mode; nothing was uploaded or published.

## GitHub About text

Crimson Desert light, player/camera and ambient telemetry via local HTTP/WebSocket. Configurable HUD, 3D radar and light markers. Per-light occlusion remains experimental.

## Nexus description draft — English

### Crimson Desert Telemetry

Inspect live lights, player position and the render camera in Crimson Desert.
The mod provides a 3D light radar, fullscreen markers, status notices and local
HTTP/WebSocket APIs for overlays, tools and future lighting integrations.
It does not drive physical lamps itself.

Target: **Steam build 25246367 / EXE 1.0.0.2850**. Native capture rejects unknown builds.

- Rendered light positions, linear HDR RGB, luminance and recognized spotlight directions/cone angles; separate authored light records.
- A separate grouped/EMA light feed preserving the original contributions.
- Player root and independent render-camera position, orientation and projection.
- Global sky, camera-local ambient exposure and a derived ambient estimate.
- Configurable HUD/radar, fullscreen markers and startup/error notices.
- Local HTTP snapshots, health, schema and WebSocket streaming, default port 27311.

**Per-light geometric occlusion is excluded from this release.** Controlled
concealed-source tests failed. The implementation and evidence are preserved for
later work. Markers can appear through walls; F11 and occlusion-hiding INI settings
are absent. No API records or RGB values are filtered. Retained visibility metadata
reports unknown/disabled with no attenuation.

Camera-local ambient passed an open/enclosed/open route in the earlier OFF v2.1.9
package. Current acceptance is tracked separately. Exposure is sampled at the
camera location, not measured as sky pixels in the image; its orientation
sensitivity remains under investigation. The estimate is not physical brightness.

### Controls

All offered feature switches are enabled in the supplied 31-setting INI.
**F8** toggles the corner HUD/radar, **F9** details, **F10** fullscreen markers.
Each accepts a decimal Windows virtual-key code; **0** disables that shortcut.
**F11 is unused.** Keys do not stop telemetry streaming.

| INI section | Key | Default |
| --- | --- | --- |
| Overlay | ToggleKey | 119 / F8 |
| Overlay | DetailsKey | 120 / F9 |
| LightOverlay | ToggleKey | 121 / F10 |

Automatic hiding in every menu is not implemented; use F8/F10.
Layout, scale, opacity, HDR white, marker limits, sampling and smoothing remain
configurable. INI changes need a game restart. See [configuration](INI_VALIDATION.md).
Research, Console, Explorer and occlusion switches cannot activate through an old
INI in the production build; their separate research profile remains in source.

### Requirements and installation

Windows x64, Microsoft .NET 8 ASP.NET Core Runtime (x64), one x64 ASI loader and the
supported game build. No NVIDIA, DLSS, Streamline, Nsight or PIX runtime dependency.
Automated D3D12 tests cover SDR/HDR10/scRGB; live HDR-display, frame-generation and
AMD/Intel game acceptance remain pending.

Close the game, back up useful INI preferences, disable previous telemetry packages
and any separate CrimsonHueConsole ASI, then import and enable the complete ZIP
through DMM. Verify all six runtime files beside the game EXE, from the same version:

```text
CrimsonDesertTelemetry.asi
CrimsonDesertTelemetry.Core.dll
CrimsonDesertTelemetry.ini
crimson-desert-telemetry.dll
crimson-desert-telemetry.deps.cfg
crimson-desert-telemetry.runtimeconfig.cfg
```

Keep .cfg names unchanged. Migrate preferences into the supplied INI. Complete ZIP
contents alone do not prove manager deployment. JSON Mod Manager/manual loader
installation require the same files; their current full lifecycle matrix is pending.

### Known compatibility report

A public 2.0.2 user reported missing DMM host/cfg companions, then a graphics-feature
crash after manually restoring them. The preserved ZIP contains them, but their
exact deployed hashes, DMM version, GPU/driver and crash module/offset are unverified.
**The cause remains unresolved; a local pass does not establish a fix for that
system.** See [compatibility issues](COMPATIBILITY_ISSUES.md).

### Data limits

Authored/rendered arrays overlap; one lamp can produce several contributions.
Discovery is view-filtered, not a full world registry. A missing light does not
prove OFF. Linear HDR RGB is renderer data, not lumens or final screen pixels.
Fast motion can expose projection latency. Display spot lengths are schematic.

The host reads memory; the plugin uses guarded renderer/D3D12 hooks and GPU copies.
No gameplay-control API or anti-cheat bypass. Exact scans apply only to the hashes
in the versioned validation record.

[Source](https://github.com/fabianviol/CrimsonDesertTelemetry) and [API](API.md).
Created by fabianviol with Claude and Codex (OpenAI). Unofficial; not affiliated
with Pearl Abyss. MIT source; dependencies retain their notices.
