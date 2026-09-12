# Public descriptions — 2.1.10, 2026-09-12

These texts describe release 2.1.10, not the old Nexus 2.0.2 download. Use them
only with the exact package and hashes in [release checks](STABLE_RELEASE_VALIDATION.md).
The external graphics crash remains unresolved; this local release is not a
confirmed fix for that user's system.

## GitHub About text

Crimson Desert telemetry. Goal: reliable ambient and per-light geometric occlusion, including off-screen sources. Blocker: fire, lamp and world-geometry occlusion.

## Nexus description draft — English

The ready-to-paste Nexus BBCode version is
[NEXUS_DESCRIPTION.bbcode](NEXUS_DESCRIPTION.bbcode).

### Crimson Desert Telemetry

Inspect live lights, player position and the render camera in Crimson Desert.
The mod provides a 3D light radar, fullscreen markers, status notices and local
HTTP/WebSocket APIs for overlays, tools and future lighting integrations.
It does not drive physical lamps itself.

Target: **Steam build 25246367 / EXE 1.0.0.2850**. Native capture rejects unknown builds.

### Goal and current development blocker

The goal is telemetry that answers both how exposed the player/camera location is
to the sky/environment and which individual nearby lights can reach it through
world geometry, including sources off-screen or behind the camera. The current
blocker is consistent occlusion in all its forms: fires, candles and other lamps
still disagree with walls, buildings and terrain in controlled tests. Ambient
passed open/enclosed/open, but camera-orientation semantics still need validation.

The complete current development state and preserved research are on
[GitHub](https://github.com/fabianviol/CrimsonDesertTelemetry). Everyone is welcome
to fork the repository, test alternatives and contribute improvements.

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
configurable. INI changes need a game restart. See
[configuration](https://github.com/fabianviol/CrimsonDesertTelemetry/blob/main/docs/INI_VALIDATION.md).
Research, Console, Explorer and occlusion switches cannot activate through an old
INI in the production build; their separate research profile remains in source.

### Requirements and installation

Windows x64, Microsoft .NET 8 ASP.NET Core Runtime (x64), one x64 ASI loader and the
supported game build. No NVIDIA, DLSS, Streamline, Nsight or PIX runtime dependency.
Automated D3D12 tests cover SDR/HDR10/scRGB; live HDR-display, frame-generation and
AMD/Intel game acceptance remain pending.

Close the game, back up useful INI preferences, disable previous telemetry packages
and any separate legacy console ASI, then import and enable the complete ZIP
through DMM. Verify all six runtime files beside the game EXE, from the same version:

```text
CrimsonDesertTelemetry.asi
CrimsonDesertTelemetry.Core.dll
CrimsonDesertTelemetry.ini
crimson-desert-telemetry.dll
crimson-desert-telemetry.deps.cfg
crimson-desert-telemetry.runtimeconfig.cfg
```

Keep .cfg names unchanged. Migrate preferences into the supplied INI. Local DMM
2.8.1 testing showed that removal/upgrades can leave both Telemetry DLLs and both
lowercase CFG files behind. With the game closed, remove the old package in DMM,
delete only those four leftover Telemetry files, then import the new ZIP. Do not
delete the ASI loader. A clean import then deployed all six current files exactly.
JSON Mod Manager/manual-loader lifecycle validation remains pending.

### Known compatibility report

A public 2.0.2 user reported missing DMM host/cfg companions, then a graphics-feature
crash after manually restoring them. The preserved ZIP contains them, but their
exact deployed hashes, DMM version, GPU/driver and crash module/offset are unverified.
**The cause remains unresolved; a local pass does not establish a fix for that
system.** See
[compatibility issues](https://github.com/fabianviol/CrimsonDesertTelemetry/blob/main/docs/COMPATIBILITY_ISSUES.md).

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
