# Mod-manager preview validation

Date: 2026-08-30. Package: `0.1.0-preview.2`.

This record is separate from the validated external telemetry tests. It does not
claim that full DMM/JSON Mod Manager deployment and in-game operation are complete.

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

## Still pending

- DMM: verify every binary/config file after leaving the mod enabled, then test
  actual game startup and clean disable/uninstall.
- JSON Mod Manager 9.9.4: import, deploy, startup, and uninstall the same archive.
- Repeat the game lifecycle tests through the ASI bootstrap, including failure
  paths for missing runtime, an occupied port, and unsupported game builds.

Do not describe the preview as a fully validated release until these pass.
