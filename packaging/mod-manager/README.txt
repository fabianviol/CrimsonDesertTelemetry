Crimson Desert Telemetry
========================

This unified preview package targets Definitive Mod Manager and JSON Mod Manager.
Complete install/uninstall and in-game validation in both managers is still pending.
One ASI starts the read-only external host and instruments the renderer for filtered
ManyLights readback. It also contains the optional research console; do not load
the old CrimsonHueConsole.asi alongside it. The unified capture needs a live restart
test before this candidate is considered a validated release.
The optional English in-game Dear ImGui HUD is completely disabled by default.
The native render camera is read directly. A cold start with upscaling off and
controlled yaw/pitch changes passed on the development NVIDIA setup; the user also
confirmed working in-game HUD behavior after restarting with preview.7. The HUD
retains automatic resolution scaling, including 4K. NVIDIA recording is unverified.
Manually tested games: Steam builds 24994088 (EXE 1.0.0.2658), 25050808
(EXE 1.0.0.2692), and 25116796 (EXE 1.0.0.2760). Other executable
hashes are accepted only when all guarded code, data, player-type and camera-table
sources resolve unambiguously; health labels this automatic, not manually tested.

Requirements
------------
- Windows x64
- Microsoft .NET 8 ASP.NET Core Runtime (x64)
- Ultimate ASI Loader, or another ASI loader supported by your setup

Default API
-----------
HTTP v1 remains unchanged. Lights are enabled by default in this candidate and use
additive JSON schema 1.4. Disabling [Lights] Enabled restores schema 1.1.
HTTP:      http://127.0.0.1:27311/v1/snapshot
Health:    http://127.0.0.1:27311/v1/health
Schema:    http://127.0.0.1:27311/v1/schema
WebSocket: ws://127.0.0.1:27311/v1/stream

Configuration
-------------
Edit CrimsonDesertTelemetry.ini before starting the game. The supported sample
rate is 1-240 Hz. The server listens on loopback only.

Nearby engine lights
--------------------
[Lights] Enabled=1 enables authored light records. ManyLights=1 additionally enables
automatic filtered renderer samples at ManyLightsSampleRateHz=20 on exact build
25116796. NearbyRadius is measured in game units, not claimed metres. Set Enabled=0
to disable both paths, or ManyLights=0 to disable only native lighting instrumentation.
Current filtered contributions include position, linear HDR RGB/luminance and spot
direction/cone, using a paired camera. They include the researched fire/candle path.
The authored and filtered arrays overlap; do not add them together. Missing filtered
data is not a physical OFF status. No permanent IDs, physical lumens, range or complete
coverage of sun/sky/emissive lighting are claimed. See docs/API.md in the source repo.

In-game HUD
-----------
To enable: set Enabled=1 in the [Overlay] section of CrimsonDesertTelemetry.ini
and restart the game. The default Enabled=0 disables the full HUD and its hotkeys.
Separate startup notices are enabled by [Notifications] Enabled=1. They show loading,
ready and actionable errors at the top left; readiness disappears after six seconds,
errors persist until resolved. A visible HUD moves notices out of its top-left area.
Disable both sections to skip all UI graphics hooks/client. They require D3D12/SDR;
if drawing fails, use the logs. Missing configuration defaults both UI features off.
Telemetry remains active independently through [Server] Enabled=1.

F8: show/hide the HUD. F9: show/hide diagnostics. No mouse input is captured.
Player-root heading (cyan) and camera heading (amber) are independent. X/Z labels
are world axes, not compass north. Player orientation is not an animated body pose.
Coordinates use game units. Loading/disconnected/stale values are not shown live.

The first HUD preview supports DirectX 12 and 8-bit SDR only. HDR and frame
generation are not validated. NVIDIA recording requires an actual recording test.
Edit [Overlay] settings in the INI before launch: Enabled, InitiallyVisible,
ShowDetails, ToggleKey, DetailsKey, Corner, AutoScale, Scale, Opacity, StaleMilliseconds.
InitiallyVisible=0 only hides an enabled HUD; its hooks and client remain active.
F8 cannot enable a HUD that was disabled at startup with Enabled=0.
Keys are decimal Windows virtual-key codes; 119=F8, 120=F9, 0=disabled.
Corner: 0=top left, 1=top right, 2=bottom left, 3=bottom right.
AutoScale=1 scales with render height relative to 1080p (2x at 2160p), including the
font atlas. AutoScale=0 uses the original fixed size. Scale is an additional
multiplier: 0.5-3.0; opacity: 0.2-1.0; use a decimal point. The panel is clamped to
fit small windows.
The ASI must load before the game's swapchain is created. Always restart the game
after installing or changing configuration. Hot-unloading the ASI is not supported.

The telemetry host reads game memory. The optional HUD hooks DXGI functions to draw
inside the game. Native lighting adds a guarded game detour and D3D12 submission hook;
optional research-console commands can alter game debug values. See THIRD-PARTY-NOTICES.txt
for Dear ImGui, MinHook and JSON library licenses.

Camera compatibility
--------------------
The native camera source is resolved from guarded game globals, without Streamline
calls or a heap scan. Upscaling-off operation was tested after a cold start.
AMD/Intel GPUs, other upscalers, HDR and frame generation need further validation.
Player position/root orientation use separate game-memory sources. The default
HTTP/WebSocket JSON 1.1 player/camera contract is unchanged. Camera quality counts are 1/1/1
for the single validated source; farPlane is null (no validated finite distance).
Camera timestamps indicate sampling time, not when the engine produced a frame.
Each capture checks both pointer routes, object types, successive field reads and
the source's own frame counter on current build (render-context counter on older builds).
Missing/changing data or a stalled counter is rejected.
These checks do not guarantee engine-frame atomicity.

The .deps.cfg and .runtimeconfig.cfg files contain unmodified .NET JSON metadata.
Do not rename them to .json: DMM mistakes loose JSON files for game-patch mods.
The bootstrap passes the dependency filename explicitly to dotnet exec. Because
.NET requires a .json filename for runtime configuration, it caches that small
text file under %LOCALAPPDATA%\CrimsonDesertTelemetry\Runtime, outside all mod and
game folders. Cache files are keyed by the configuration's SHA-256 and verified
before reuse. No executable or game data is copied to that cache.

Upgrading / merging the former two plugins
-----------------------------------------
Close the game first. Disable the old telemetry and CrimsonHueConsole packages in
your mod manager before enabling this single package. Preserve the old files/config
in a backup outside loader search paths. Never leave the old console ASI active:
native instrumentation refuses that conflict. Do not replace your ASI loader or
other mods. The source checkout also contains scripts/Install-UnifiedPlugin.ps1,
a bounded backup/install/restore helper for direct game-folder installations.
Keep useful INI preferences, but migrate them into the unified INI sections.
If your manager keeps an older Enabled=1 in [Overlay], change it to 0 manually
to disable the HUD. Configuration changes require a game restart.

Logs
----
CrimsonDesertTelemetry.bootstrap.log records plugin startup.
CrimsonDesertTelemetry.host.log records host diagnostics.
CrimsonDesertTelemetry.overlay.log records native HUD initialization/status.
CrimsonDesertTelemetry.native.log records integrated console and light capture status.

This unofficial community project is not affiliated with Pearl Abyss.
