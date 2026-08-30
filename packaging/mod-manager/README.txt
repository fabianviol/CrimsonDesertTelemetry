Crimson Desert Telemetry
========================

This preview package targets Definitive Mod Manager and JSON Mod Manager.
Complete install/uninstall and in-game validation in both managers is still pending.
It starts the read-only Crimson Desert Telemetry host automatically with the game.

Requirements
------------
- Windows x64
- Microsoft .NET 8 ASP.NET Core Runtime (x64)
- Ultimate ASI Loader, or another ASI loader supported by your setup

Default API
-----------
HTTP:      http://127.0.0.1:27311/v1/snapshot
Health:    http://127.0.0.1:27311/v1/health
Schema:    http://127.0.0.1:27311/v1/schema
WebSocket: ws://127.0.0.1:27311/v1/stream

Configuration
-------------
Edit CrimsonDesertTelemetry.ini before starting the game. The supported sample
rate is 1-240 Hz. The server listens on loopback only.

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

Logs
----
CrimsonDesertTelemetry.bootstrap.log records plugin startup.
CrimsonDesertTelemetry.host.log records host diagnostics.

This unofficial community project is not affiliated with Pearl Abyss.
