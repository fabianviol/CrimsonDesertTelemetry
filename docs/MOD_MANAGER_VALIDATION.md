# Mod-manager validation

## v1.2.0 engine-light release candidate (2026-09-04)

The managed Release build and all 43 regression tests passed, including the new
direct camera and direct scene/light layouts. All three native CTests, four API
example tests, and expanded-package/ZIP validators passed. The actual Release
binary was then run read-only against Steam build `25116796`: schema 1.2 reported
player, orientation, camera and available engine lights; 230 source records yielded
135 within radius 100, with zero malformed, unsupported or unavailable-walk records.

Final package: `CrimsonDesertTelemetry-v1.2.0-ModManagers.zip`.
SHA-256: `2102189439F629FF564ECFCC837C6F54F931AA701EEBC55E1C2521FC2EF87EDE`.

## v1.1.0 automatic-compatibility release candidate (2026-09-01)

The managed Release build, 37 managed regression tests, loopback HTTP/WebSocket
smoke test, all three native CTests and package/ZIP validators passed. Forced
automatic resolution of the real, locally validated game EXE independently found
the same six player/camera RVAs as its exact-hash definition. Synthetic tests also
cover relocated layouts and fail-closed behavior for missing/ambiguous code,
malformed PE data, invalid data targets and weak/ambiguous object-table fingerprints.

Final package: `CrimsonDesertTelemetry-v1.1.0-ModManagers.zip`.
SHA-256: `ED291A162400BC8BD3DA5A824554A4949776DF35708479B05383845D50C897D3`.

The candidate was then imported through DMM. DMM deployed all binaries but retained
the old `deps.cfg`; replacing that one file from the imported candidate made all six
runtime files match. Before launch, Steam updated the game to build `25050808`, EXE
`1.0.0.2692`, SHA-256
`A93336BC08F1613B609F4F331C687B13A8DB729DD1AB99D8FD09C9FBE06BC210`.
The new hash passed automatic offline resolution, finding the same six guarded RVAs.

A fresh live launch then reported `playing`, `compatibility.mode: automatic`,
reference build `24994088`, build `25050808`, no error and roughly 60 samples/second.
Player, player orientation, camera and quality were present at `1/1/1`. A camera-only
turn changed the camera while player position/heading remained exact. Subsequent
player movement changed position by approximately `(-2.71, -0.08, +1.07)` and player
heading from `228.56` to `302.94` degrees while camera heading remained `306.78`.
This constitutes local validation of the automatically recognized build. The final
package adds its exact build definition and release-version metadata; runtime data
sources are unchanged. That exact path selects the same confirmed six RVAs without
the automatic scan and does not require a redundant second movement test.

## v1.0.0 release candidate (2026-08-31)

The release package has been built locally with the MIT license, native camera
reader and HUD disabled by default. The following checks passed for this artifact:

- Release managed build and all 30 managed regression tests.
- HTTP/WebSocket API smoke test.
- Three native CTests: HUD model, isolated D3D12 renderer and WebSocket client.
- Expanded-package checks and ZIP payload comparison (nine files).
- Packaged version 1.0.0, MIT license and `[Overlay] Enabled=0` verified.

Package: `CrimsonDesertTelemetry-v1.0.0-ModManagers.zip`.
SHA-256: `4F8EEAFFA0030DDE4BD8095ACB606D3CB73F9E3A9F03D15FD14DEC5CE9F363C9`.

Final installed-file/startup acceptance passed on 2026-08-31 after correcting
two retained metadata files (see below). All six runtime files, including the INI,
matched the v1.0.0 package by SHA-256. The ASI was loaded in the restarted game;
health reported `playing`, supported build 24994088 and no error. Player position,
player orientation and camera data were present. The sequence advanced by 62 over
1.0261 capture seconds (approximately 60 Hz). With `[Overlay] Enabled=0`, health
reported zero WebSocket clients and the HUD log had no entries from this launch.

During this DMM upgrade, the library contained the correct new `.cfg` companions,
but `bin64` retained the older dependency/runtime metadata while the DLLs updated.
With the game closed, both old files were backed up and replaced from the release
package before the successful restart. This verifies the corrected installation,
not automatic metadata replacement by DMM. Preserve customized INI settings when
upgrading, but replace both `.cfg` companions alongside the binaries.

JSON Mod Manager lifecycle validation remains pending; do not advertise it as
fully tested. The preview records below are historical evidence, not additional
checks of the final archive.

## Preview.7 native-camera candidate (2026-08-30)

The managed host now uses the native engine camera, validated after a cold start
with upscaling off. The previous HUD remains, with a single-source diagnostic
label. DMM import/deployment, packaged HUD acceptance, reload/teleport, and testing
other upscalers are still pending for this package; earlier passes do not cover it.

Preview.7 build checks passed: 30 managed tests, three native CTests, loopback
HTTP/WebSocket smoke, expanded-package validation, and ZIP payload verification.
Package SHA-256: `FD893319C0B16BFF07B159701C7C4069F5CB5D2EA797A75B5C775D4C8FD42F67`.
The installed DMM/game files were not changed by this build.

## Preview.4 in-game observation / preview.6 candidate (2026-08-30)

The new optional Dear ImGui HUD has native model, isolated D3D12 and loopback
WebSocket tests. Its manager ZIP adds THIRD-PARTY-NOTICES.txt (nine payload files).
The preview.4 HUD appeared in the actual game, but was too small at 4K and the user
reported camera-arrow lag/backsteps. Preview.6 includes resolution scaling and a
temporal camera-selection fix verified in offline replay; renewed game acceptance,
recording and manager lifecycle tests remain pending. Importing the
ZIP into DMM updated its library but initially left the old ASI in game `bin64`.
With the game closed, disabling/re-enabling the plugin deployed the matching ASI.
Verify deployed file hashes after upgrades; the DMM status badge alone is insufficient.
See
[OVERLAY_VALIDATION.md](OVERLAY_VALIDATION.md). The preview.2 results below are
historical evidence for the telemetry bootstrap, not proof of the new render hooks.

## Historical preview.2 record

Date: 2026-08-30. Package: `0.1.0-preview.2`.

This record is separate from the validated external telemetry tests. DMM deployment
and an actual in-game startup pass; full lifecycle and JSON Mod Manager validation
are not yet complete.

## DMM metadata collision and fix

DMM 1.9.4 interpreted `crimson-desert-telemetry.deps.json` as a JSON game mod and
reported `invalid type: map, expected a sequence`. The file was valid .NET JSON,
but not DMM's game-patch schema. The runtimeconfig JSON was also incorrectly
added to the manager's mod load order.

The package now ships `.deps.cfg` and `.runtimeconfig.cfg`. The bootstrap uses
`dotnet exec --depsfile` for the dependency metadata. A direct runtime `.cfg`
argument failed on the installed .NET host: hostfxr normalized the requested name
to `.json`. Therefore the bootstrap caches only the runtime text under
`%LOCALAPPDATA%\CrimsonDesertTelemetry\Runtime\runtime-<SHA256>.json` and supplies
that absolute path via `--runtimeconfig`. Cache contents are verified before use.
No JSON or executable is generated in the mod library.

## Completed checks

- Native ASI and managed host build successfully.
- All 12 managed tests pass, including the embedded build definition.
- Package tests reject either old JSON filename, a missing companion, and a loose EXE.
- ZIP and expanded payload contain exactly the same eight expected files.
- An isolated loader process successfully starts the actual ASI from a path with
  spaces, without adjacent `.json`, definition, or schema files.
- HTTP health and embedded schema endpoints respond from that host.
- Host terminates when the isolated loader process exits.
- No `.json` appears in the package directory after startup.
- After replacing the old library folder and restarting DMM, its 09:34:44 log
  reports one ASI plugin and `winmm.dll`; the new startup scan has no invalid-JSON
  error. Older log entries retain the original error as historical records.

The old test folder and archive were backed up outside DMM's mods directory.
The user's INI settings and existing ASI loader were preserved.

## DMM deployment and in-game startup

Verified with DMM 1.9.4 and package `0.1.0-preview.2` on 2026-08-30:

- After the user enabled the plugin, DMM listed it in `activeAsiMods`. All six
  runtime companions in `bin64` matched the library files byte-for-byte (SHA-256):
  the ASI, INI, core DLL, host DLL, dependency CFG, and runtime CFG.
- The user started the game and loaded a save. Game PID 16600 started at 09:43:20
  local time. Its loaded modules included the game-directory `winmm.dll` and
  `CrimsonDesertTelemetry.asi`.
- The bootstrap log recorded starting host PID 1720 at 09:43:22 on loopback port
  27311 at 60 Hz. No separate manual telemetry launch was used for this test.
- HTTP health and snapshot returned 200, state `playing`, supported build
  `24994088`, and no health error. Discovery found 39 copies in 12,580.8 ms.
- Player and camera positions, the camera basis, FOV, and aspect ratio were
  present. A subsequent WebSocket check received 121 valid snapshots over
  1.9952 capture seconds (60.15 Hz), with no sequence gaps and no missing player
  or camera objects. All samples reported `playing`.
- Reported capture duration over those 121 samples averaged 126.9 microseconds
  (minimum 96, maximum 494).

DMM's UI simultaneously showed `NOT MOUNTED`, one active ASI, and an available
loader. The badge's cause was not investigated; the file hashes and loaded-module
check establish deployment and loading independently of that ambiguous display.
This startup check does not replace movement, reload, or shutdown lifecycle tests.

## Still pending

- DMM: clean disable/uninstall and game shutdown with owned-host cleanup.
- JSON Mod Manager 9.9.4: import, deploy, startup, and uninstall the same archive.
- Repeat the game lifecycle tests through the ASI bootstrap, including failure
  paths for missing runtime, an occupied port, and unsupported game builds.

Do not describe the preview as a fully validated release until these pass.
