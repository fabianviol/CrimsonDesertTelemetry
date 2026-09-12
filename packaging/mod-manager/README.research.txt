Crimson Desert Telemetry - private source-visibility research build
===================================================================
This package tests one unfinished feature: direct geometric visibility from the
player to every local light that the existing authored/rendered feeds currently
know about. Fire, torch, candle, lamp and other light types use the same geometry
test. Camera direction and on-screen state do not participate.

THIS IS NOT A PRODUCTION RELEASE. The stable 2.1.10 feature set remains intact,
but this ASI additionally compiles in bounded SDF research acquisition. A clean
build and offline controls do not establish correct behavior in the live game.

The query origin is player position plus one game unit on world Y, avoiding the
physics root's ground contact point. Each source center is sampled through the
current t233 SDF at quarter-cell spacing. A source is blocked after one complete
cell of connected near-surface samples. Results are binary: clear or blocked.
Unavailable, stale, uncovered or over-budget results remain unknown.

The HTTP/WebSocket snapshot uses schema 1.5 while lights are enabled. Optional
sourceVisibility metadata is attached to both lights.sources and
lights.rendered.sources. Original positions, RGB, raw records and smoothed
contributions are unchanged. Unknown never means blocked and never removes a
record. The legacy field name lightCaptureSequence identifies the independent
visibility query in schema 1.5; volumeSequence/contextFrame identify its SDF.

Install and test
----------------
Close the game. Remove the previous Telemetry package through DMM and make sure
no old Telemetry ASI, DLL, CFG or INI remains beside the game executable. Keep
the ASI loader. Import and activate this complete ZIP, then start the game and
load a save. Do not combine files from different package versions.

The intended first live control uses one fixed player position and known lights:

1. Identify at least one unobstructed and one wall-blocked fire/candle/lamp.
2. Leave F11 in "show blocked" mode so every record and verdict is visible.
3. Record the marker distance/position and sourceVisibility status for each.
4. Rotate the camera without moving the player. Verdicts for retained sources
   must remain unchanged, including sources behind or outside the view.
5. Move so the same source follows visible -> blocked -> visible. Each change
   must be reversible and agree with solid world geometry.

F8 toggles the corner HUD, F9 its details, F10 fullscreen markers, and F11 whether
freshly blocked lights are hidden in the HUD/radar. All four decimal virtual-key
codes can be changed in CrimsonDesertTelemetry.ini or disabled with 0. Hiding is
display-only: API records remain complete. Unknown/stale sources stay visible.

Known bounds
------------
Only lights present in the existing authored/rendered source union can be tested;
an absent source is not a blocked source. The exchange is bounded to the nearest
256 distinct positions. SDF results older than 1500 ms, targets outside clipmap
coverage, and sources beyond that budget report unknown. This build retains at
most 120 SDF transactions according to the supplied INI, so restart for a fresh
controlled run after acquisition ends.

Requirements: Windows x64, Steam build 25246367 / EXE 1.0.0.2850, an x64 ASI
loader, and Microsoft .NET 8 ASP.NET Core Runtime x64.

HTTP snapshot: http://127.0.0.1:27311/v1/snapshot
Schema:        http://127.0.0.1:27311/v1/schema
Source/state:  https://github.com/fabianviol/CrimsonDesertTelemetry
Test contract: https://github.com/fabianviol/CrimsonDesertTelemetry/blob/main/docs/SOURCE_VISIBILITY.md
