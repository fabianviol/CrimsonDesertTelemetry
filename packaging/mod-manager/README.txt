Crimson Desert Telemetry
========================
Live light contributions, player/render-camera telemetry and local ambient data,
with a corner HUD, 3D light radar, fullscreen markers and startup/error notices.
Local HTTP/WebSocket APIs support other tools. This mod does not drive lamps.

This CDT_RESEARCH=OFF candidate restores the earlier narrow
camera-to-rendered-source visibility path that worked live for fires. It uses one
current SDF volume and does not include research history, dumps or tracing. It is
not a complete light registry: candles, lamps and retained off-screen sources still
need live acceptance. No API records or RGB values are filtered or attenuated.

Source and support: https://github.com/fabianviol/CrimsonDesertTelemetry
Validation: https://github.com/fabianviol/CrimsonDesertTelemetry/blob/main/docs/STABLE_RELEASE_VALIDATION.md
API: https://github.com/fabianviol/CrimsonDesertTelemetry/blob/main/docs/API.md

Requirements and installation
-----------------------------
Windows x64, Microsoft .NET 8 ASP.NET Core Runtime (x64), one x64 ASI loader,
and Steam build 25246367 / EXE 1.0.0.2850. Native capture rejects unknown builds.
Close the game, back up useful INI preferences and disable previous telemetry
packages and any separate legacy console ASI, if installed.
Import and enable the complete ZIP in DMM. Keep your existing loader and other
mods. Verify ALL six runtime files beside the game EXE, from the SAME version:

CrimsonDesertTelemetry.asi
CrimsonDesertTelemetry.Core.dll
CrimsonDesertTelemetry.ini
crimson-desert-telemetry.dll
crimson-desert-telemetry.deps.cfg
crimson-desert-telemetry.runtimeconfig.cfg

Important DMM upgrade/removal note: local tests with DMM 2.8.1 showed that deleting
this package can leave both Telemetry DLLs and both lowercase CFG files behind,
and an upgrade can retain an older deps.cfg. With the game closed, remove the old
package in DMM, then verify and delete only these four leftover Telemetry files
before importing the new ZIP:

CrimsonDesertTelemetry.Core.dll
crimson-desert-telemetry.dll
crimson-desert-telemetry.deps.cfg
crimson-desert-telemetry.runtimeconfig.cfg

Do not delete your ASI loader. Back up a customized Telemetry INI before replacing
the package. A clean DMM 2.8.1 import was verified to deploy all six current files.

Keep .cfg names unchanged; some manager versions interpret loose JSON as patches.
The bootstrap caches only runtime configuration text under
LOCALAPPDATA/CrimsonDesertTelemetry/Runtime, keyed and verified by SHA-256.
No executable or game data is copied into that cache. Hot-unloading is unsupported.
Complete ZIP contents do not prove complete manager deployment. JSON Mod Manager
and manual ASI-loader installation need the same files; their current full
installation/uninstallation matrix remains pending.

Compatibility fix retained from 2.1.11: an overlay-owned DXGI reference could keep
an old flip-model swapchain alive while NVIDIA Streamline or the game replaced it
for the same window. That failure was observed as CreateSwapChainForHwnd
E_ACCESSDENIED. Automated nested/startup-style and runtime display/HDR replacement
tests pass. Confirmation on the two external reporter systems remains pending;
include a DMM support bundle if a crash persists.

Controls and configuration
--------------------------
All offered feature switches are ON in the supplied 34-setting INI, including
diagnostics and source visibility. F8: corner HUD/radar. F9: details. F10:
fullscreen light markers. F11: show/hide freshly blocked light records.
Each shortcut accepts a decimal Windows virtual-key code; 0 disables that key.
Defaults: Overlay.ToggleKey=119, Overlay.DetailsKey=120,
LightOverlay.ToggleKey=121, LightOverlay.OcclusionToggleKey=122.
Keys do not stop API capture.
Automatic hiding in every menu is not implemented; use F8/F10 to hide the views.
INI changes require a game restart; display keys work immediately.

Research, Console, Explorer, the broad player/all-source bridge and OcclusionTest
remain absent from this production build. Merge preferences into the new template.
Configuration ranges, dependencies and tests:
https://github.com/fabianviol/CrimsonDesertTelemetry/blob/main/docs/INI_VALIDATION.md

Lights.Enabled includes authored lights; ManyLights adds renderer contributions
at 20 Hz by default. Ambient requires both. Server.SampleRateHz defaults to60;
faster API polling does not create additional GPU samples. NearbyRadius uses game
units. LightSmoothing controls grouping and EMA; raw values remain unchanged.
Radar3D=0 selects the compass. AutoScale/Scale/Opacity/Corner control layout.
LightOverlay.Radius/MaxMarkers/MaxLabels bound visual clutter, not source discovery.
HdrPaperWhiteNits=200 sets shared HDR UI white, clamped to80-500 nits.
Notifications show readiness briefly or persistent actionable errors. Readiness
requires valid player and render-camera data. Native light/sky capture waits for
that first playable-world signal, so initial menu/shader loading cannot consume a
GPU transaction. Consult logs if graphics initialization fails.
Set Enabled=0 in Overlay, LightOverlay AND Notifications to disable all UI owners
and UI hooks/client. InitiallyVisible=0 only hides an enabled view. Server and
native light capture are independently configured.

API and data limits
-------------------
HTTP:      http://127.0.0.1:27311/v1/snapshot
Health:    http://127.0.0.1:27311/v1/health
Schema:    http://127.0.0.1:27311/v1/schema
WebSocket: ws://127.0.0.1:27311/v1/stream
Ambient:   http://127.0.0.1:27311/v1/ambient
Smoothed:  http://127.0.0.1:27311/v1/lights/smoothed

The host listens on loopback only. Do not launch another host on its port.
Routes remain v1; light-enabled snapshots use additive schema1.5, otherwise1.1.
Rendered records receive camera-to-source visibility when a fresh SDF volume covers
their segment. Unknown/stale records remain present with null attenuation.
Authored/rendered arrays overlap. One lamp may produce multiple contributions.
This is not a full360-degree registry; a missing source does not prove OFF.
Linear HDR RGB/luminance are renderer values, not lumens or final pixels.
Ambient contains global sky, camera-local sky exposure and their derived estimate.
Exposure is sampled at the camera location, not the fraction of sky on screen.
An earlier OFF package passed open/enclosed/open; this candidate's live checks
are separate. It is not measured room brightness. Camera-orientation sensitivity
remains under investigation. Root orientation is not body pose; display spot-arrow
and frustum lengths are schematic. Fast motion can expose projection latency.

Graphics and compatibility
--------------------------
Automated D3D12 tests cover SDR/HDR10/scRGB and all UI owners together.
Live HDR-display, frame-generation and AMD/Intel game acceptance remain pending.
No NVIDIA, DLSS, Streamline, Nsight or PIX runtime dependency is required.
Unknown output format/color-space combinations are unsupported. HDR UI uses two
extra full-resolution textures and a scene composite; SDR has no extra composite.
Software-GPU tests do not establish compatibility with every game setup.

Diagnostics
-----------
The host reads memory; the plugin uses guarded code/D3D12 hooks and GPU copies.
No gameplay-control API or anti-cheat bypass is provided.
Logs beside the plugin: CrimsonDesertTelemetry.bootstrap.log (startup/host),
CrimsonDesertTelemetry.host.log, CrimsonDesertTelemetry.overlay.log (UI),
CrimsonDesertTelemetry.native.log (light/ambient capture).
Exact ASI/ZIP scan and live results belong to the versioned validation record;
an earlier clean scan is not a verdict for this binary.

Current development goal and blocker
------------------------------------
The goal is reliable local ambient/sky occlusion plus geometric visibility for
each relevant light, including off-screen and behind-camera sources. This candidate
only restores the earlier rendered-source/fire step. Consistent candles, lamps,
all known sources, walls, buildings and terrain remain the blocker. Ambient has
passed an open/enclosed/open route. The complete current source and preserved
research are on GitHub. Forks are welcome.

Created by fabianviol, developed with Claude and Codex (OpenAI).
See THIRD-PARTY-NOTICES.txt for dependency licenses.
Unofficial community project; not affiliated with Pearl Abyss.
