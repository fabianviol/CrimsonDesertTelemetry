Crimson Desert Telemetry 2.0.0
=============================

Live light, player and camera data for Crimson Desert: local HTTP and WebSocket APIs,
fullscreen light markers and a 3D light radar with the camera's viewing frustum.
Inspect current light positions, linear HDR color and brightness, plus direction
and cone angles for spot contributions. Fires, candles, lanterns and glass/crystal
lamps were tested in game. Multiple contributions can belong to one physical lamp.

Download, documentation and support:
https://github.com/fabianviol/CrimsonDesertTelemetry
Video demonstration: https://youtu.be/eyRkkTXAU64
API reference: https://github.com/fabianviol/CrimsonDesertTelemetry/blob/main/docs/API.md

Requirements and compatibility
------------------------------
- Windows x64, Microsoft .NET 8 ASP.NET Core Runtime (x64), and an ASI loader.
- Current full-feature baseline: Steam build 25116796, EXE 1.0.0.2760.
- No NVIDIA GPU, DLSS, Streamline, Nsight or PIX dependency in the runtime paths.
  Upscaling-off operation was tested. Actual AMD/Intel game runs remain untested.
- DirectX 12 UI supports 8-bit/10-bit SDR, HDR10 (10-bit PQ/Rec.2020) and FP16
  scRGB. HUD, light markers and notices automatically use the game's buffer
  format/color space. Unknown combinations remain unsupported. All 14 native
  tests, including three HDR tests, pass; a live HDR display/game run is untested. Frame generation
  remains unvalidated. Telemetry/light capture has no HDR-output gate.
- Unknown EXEs fail closed for native light instrumentation. A game update may
  require an updated mod profile; matching an EXE hash alone is not sufficient.

Install or upgrade
------------------
1. Close the game. Preserve useful old INI preferences in a backup.
2. Disable previous telemetry packages and the old CrimsonHueConsole.asi. This
   single plugin replaces both; never leave the old console ASI active alongside it.
3. Import this ZIP into Definitive Mod Manager or JSON Mod Manager, then enable it.
   Alternatively, put its payload files beside the game EXE using your ASI loader.
   Keep your existing loader and other mods. A complete install/uninstall matrix
   across both managers is still pending; local DMM use before the HDR addition
   was verified.
4. Edit CrimsonDesertTelemetry.ini before launch if desired, then restart the game.
   Merge preferences into the new sections rather than keeping a stale whole INI.

Keep the .deps.cfg and .runtimeconfig.cfg names unchanged: DMM treats loose JSON
files as game patches. The bootstrap caches only the runtime configuration text
under LOCALAPPDATA/CrimsonDesertTelemetry/Runtime, keyed and verified by SHA-256.
No executable or game data is copied into that cache. Hot-unloading is unsupported.

Controls and defaults
--------------------
F8: corner HUD / 3D radar. F9: diagnostic details. F10: fullscreen light markers.
These controls do not capture mouse input. Light capture, both HUD views and status
notices are enabled in the supplied INI; the optional research console is disabled.

[Lights] Enabled=1 includes authored lights; ManyLights=1 adds filtered renderer
contributions, captured at 20 Hz by default. NearbyRadius uses game units, not metres.
[Overlay] Radar3D=1 selects the 3D view; Radar3D=0 restores the original compass.
[LightOverlay] Radius=35, MaxMarkers=512 and MaxLabels=6 bound visual clutter.
Nearby contributions share a detail box, but retain their individual raw values.
AutoScale adapts the HUD to resolution; Scale and Opacity are adjustable in the INI.
[Overlay] HdrPaperWhiteNits=200 sets white brightness for all HDR UI, clamped to
80-500 nits. This also applies to markers/notices when the corner HUD is disabled.
No game HDR settings or metadata are changed. Configuration changes need a restart.

[Notifications] shows a brief success notice when requested data is ready (default
six seconds, clamped to 5-10 seconds). It may appear during the visible loading
sequence. There is no persistent normal loading message. Actionable errors can
appear immediately and remain until resolved, including unsupported-EXE errors.
If graphics initialization itself fails, consult the logs instead.

Set Enabled=0 in Overlay, LightOverlay AND Notifications to disable all UI hooks
and their client. InitiallyVisible=0 only hides an enabled view. The telemetry host
is independently controlled by [Server]. Configuration changes require a restart.
Returning to the title screen may leave data/HUD visible for several seconds before
they become stale; the user can hide the views with their keys.

Local HTTP and WebSocket APIs
----------------------------
HTTP:      http://127.0.0.1:27311/v1/snapshot
Health:    http://127.0.0.1:27311/v1/health
Schema:    http://127.0.0.1:27311/v1/schema
WebSocket: ws://127.0.0.1:27311/v1/stream

HTTP snapshots and WebSocket stream messages both carry JSON. The server listens
on loopback only. HTTP v1 routes remain unchanged. Lights use
additive JSON schema 1.4; disabling Lights.Enabled restores schema 1.1. The API
includes player position/root orientation and the independent native render camera.
Do not start a second host on the same port while the ASI-managed host is running.

What the light values mean
-------------------------
Authored lights and filtered renderer contributions are overlapping views, not
arrays to add together. Filtered lights are current render data, not a complete
world registry or persistent physical-object identity. Absence does not prove OFF.
No complete sun/sky/emissive coverage, physical lumens or measured range is claimed.
Raw linear HDR RGB and luminance can change with effects and rendering/exposure;
they are not normalized lamp colors or a measurement of the final visible pixel.

The radar is filtered, not a complete 360-degree light inventory. World markers
have no scene-depth test and may appear through walls. Spot-arrow/frustum lengths
are schematic. Display swatches visualize HDR data, not the game's tone mapping. Fast camera
movement can expose capture/projection latency. Root orientation is not body pose.

HDR UI is blended over the game in linear light. Pixels outside the UI stay
unchanged; the compositor does not tone-map the whole scene. HDR rendering uses
two additional full-resolution GPU textures and a scene copy/composite while UI
is drawn. The SDR path has no additional compositor pass.

Safety and diagnostics
----------------------
The external host reads process memory. The native plugin uses guarded code and
D3D12 hooks for capture and UI. The optional research console can change game debug
values when explicitly enabled; the package is not wholly read-only instrumentation.
No gameplay-control API or anti-cheat bypass is provided.

Logs beside the plugin:
- CrimsonDesertTelemetry.bootstrap.log: plugin startup and host launch.
- CrimsonDesertTelemetry.host.log: host diagnostics.
- CrimsonDesertTelemetry.overlay.log: HUD initialization and status.
- CrimsonDesertTelemetry.native.log: integrated console and light capture.

Source users can run check-update against a new EXE for an offline diagnostic;
it does not automatically approve that build. See docs/UPDATE_RECOVERY.md.

Created by fabianviol, developed with Claude and Codex (OpenAI).
See THIRD-PARTY-NOTICES.txt for bundled library licenses.
Unofficial community project; not affiliated with Pearl Abyss.
