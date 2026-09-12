# Current compatibility reports

## Report relayed on 2026-09-12, posted around 08:30

A Nexus user reported two separate problems. The post text was provided by the
project owner; its link, exact plugin version, GPU/driver, other mods and crash
details are not yet available. This is external user evidence, not a reproduced
local failure or proof of a particular D3D12 hook defect.

1. DMM deployed `CrimsonDesertTelemetry.asi`, `CrimsonDesertTelemetry.Core.dll`
   and the INI, but omitted `crimson-desert-telemetry.dll` and both lowercase
   `.deps.cfg` / `.runtimeconfig.cfg` companions. Copying those three files from
   the same package removed the missing-host error and allowed host startup.
2. The game still crashed with graphics-related features enabled. The reported
   tests include `Lights=1`, `Overlay=1`, `Notifications=1`, SDR as well as HDR;
   setting only `Radar3D=0`, `ManyLights=0`, or console patch options off did not
   prevent it. The configuration with Lights, Overlay, Notifications and
   LightOverlay all disabled launched successfully. The reporter states the
   game build and .NET runtime are correct, but their actual values are unverified.

The successful server-only case narrows the affected enabled paths. It does not
identify the faulting module, hook, GPU interface or resource lifecycle, and a
single disabled option does not isolate other still-enabled features.

Required next evidence: exact package and DMM versions, installed companion-file
hashes, full INI, GPU/driver and other graphics mods/overlays/frame generation,
the native/bootstrap logs and faulting module/exception offset. Reproduce one
feature combination at a time before changing graphics code. Do not claim that
the current occlusion update resolves this report.

Package validation proves ZIP contents and their equality with the staged
payload; it does not prove DMM deployment on every system. This machine has also
retained older `.deps.cfg` metadata on upgrades. Preserve useful INI preferences,
and verify all six runtime files after deployment. Keep historical packages and
their evidence intact. See [mod-manager validation](MOD_MANAGER_VALIDATION.md).
