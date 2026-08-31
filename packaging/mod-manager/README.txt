Crimson Desert Telemetry 1.0.0
========================

This first release package targets Definitive Mod Manager and JSON Mod Manager.
Complete install/uninstall and in-game validation in both managers is still pending.
It starts the read-only Crimson Desert Telemetry host automatically with the game.
The optional English in-game Dear ImGui HUD is completely disabled by default.
The native render camera is read directly. A cold start with upscaling off and
controlled yaw/pitch changes passed on the development NVIDIA setup; the user also
confirmed working in-game HUD behavior after restarting with preview.7. The HUD
retains automatic resolution scaling, including 4K. NVIDIA recording is unverified.
Supported game: Steam build 24994088, executable 1.0.0.2658. Unknown builds are rejected.

Requirements
------------
- Windows x64
- Microsoft .NET 8 ASP.NET Core Runtime (x64)
- Ultimate ASI Loader, or another ASI loader supported by your setup

Default API
-----------
Product version 1.0.0 retains HTTP v1 endpoints and JSON schema 1.1.
HTTP:      http://127.0.0.1:27311/v1/snapshot
Health:    http://127.0.0.1:27311/v1/health
Schema:    http://127.0.0.1:27311/v1/schema
WebSocket: ws://127.0.0.1:27311/v1/stream

Configuration
-------------
Edit CrimsonDesertTelemetry.ini before starting the game. The supported sample
rate is 1-240 Hz. The server listens on loopback only.

In-game HUD
-----------
To enable: set Enabled=1 in the [Overlay] section of CrimsonDesertTelemetry.ini
and restart the game. The default Enabled=0 skips all HUD graphics hooks, hotkeys
and its WebSocket client. Missing HUD configuration also defaults to disabled.
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
inside the game but does not modify gameplay values. See THIRD-PARTY-NOTICES.txt
for Dear ImGui, MinHook and JSON library licenses. No light-source module is included.

Camera compatibility
--------------------
The native camera source is resolved from guarded game globals, without Streamline
calls or a heap scan. Upscaling-off operation was tested after a cold start.
AMD/Intel GPUs, other upscalers, HDR and frame generation need further validation.
Player position/root orientation use separate game-memory sources. The existing
HTTP/WebSocket JSON 1.1 contract is unchanged. Camera quality counts are 1/1/1
for the single validated source; farPlane is null (no validated finite distance).
Camera timestamps indicate sampling time, not when the engine produced a frame.
Each capture checks both pointer routes, object types, successive field reads and
the render-context counter. Missing/changing data or a stalled counter is rejected.
These checks do not guarantee engine-frame atomicity.

The .deps.cfg and .runtimeconfig.cfg files contain unmodified .NET JSON metadata.
Do not rename them to .json: DMM mistakes loose JSON files for game-patch mods.
The bootstrap passes the dependency filename explicitly to dotnet exec. Because
.NET requires a .json filename for runtime configuration, it caches that small
text file under %LOCALAPPDATA%\CrimsonDesertTelemetry\Runtime, outside all mod and
game folders. Cache files are keyed by the configuration's SHA-256 and verified
before reuse. No executable or game data is copied to that cache.

Upgrading the first test package
-------------------------------
Replace the old package folder completely while the mod is disabled and the game
is closed. Do not merge over it: leftover .deps.json/.runtimeconfig.json files
would still be detected as invalid mods. Keep your INI settings if customized.
If your manager keeps an older Enabled=1 in [Overlay], change it to 0 manually
to disable the HUD. Configuration changes require a game restart.

Logs
----
CrimsonDesertTelemetry.bootstrap.log records plugin startup.
CrimsonDesertTelemetry.host.log records host diagnostics.
CrimsonDesertTelemetry.overlay.log records native HUD initialization/status.

This unofficial community project is not affiliated with Pearl Abyss.
