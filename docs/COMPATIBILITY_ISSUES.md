# Current compatibility reports

## Report relayed on 2026-09-12, posted around 08:30

A Nexus user reported two separate problems with the public **2.0.2 ASI package**.
The project owner supplied the post text and confirmed the download listing
`12 Sep 2026, 6:02AM ... version 2.0.2` (timezone not specified). Its direct link,
GPU/driver, other mods and crash details are not yet available. This is external
user evidence, not a reproduced local failure or proof of a particular D3D12
hook defect. It predates the new production per-light implementation.

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

Required next evidence: exact package hash and DMM version, installed companion-file
hashes, full INI, GPU/driver and other graphics mods/overlays/frame generation,
the native/bootstrap logs and faulting module/exception offset. Reproduce one
feature combination at a time before changing graphics code. Do not claim that
the current occlusion update resolves this report.

Package validation proves ZIP contents and their equality with the staged
payload; it does not prove DMM deployment on every system. This machine has also
retained older `.deps.cfg` metadata on upgrades. Preserve useful INI preferences,
and verify all six runtime files after deployment. Keep historical packages and
their evidence intact. See [mod-manager validation](MOD_MANAGER_VALIDATION.md).

Read-only inspection of the preserved local v2.0.2 ZIP on 2026-09-12 confirms that
all three reportedly omitted companions are present under its expected package
root, alongside the other six files. The reporter's downloaded ZIP hash is not
available, so equality with that preserved archive is not yet proven.

## Local stability candidate control — later on2026-09-12

rc.1 OFF started successfully on the local RTX3080/SDR setup with all offered
feature switches enabled. Raw/smoothed/ambient streams remain available across
two30-second windows; user confirms F8–F10 and menu entry/exit without flicker or
crash. The debug console is excluded from this production package. This local
result is not evidence that the external2.0.2 crash has the same cause or is fixed.

All six runtime companions in DMM's library match rc.1. Deployed bin64 files match
except deps.cfg, which still has2.1.8 version labels but the identical dependency/
runtime asset structure. The new host runs using it. This proves a local metadata
replacement discrepancy after import, not missing binaries in the ZIP. See
[the release ledger](STABLE_RELEASE_VALIDATION.md) for exact evidence and limits.

User-confirmed DMM1.9.4 deletion removes ASI/INI/logs but leaves both DLLs and CFGs.
Reimport still leaves the old deps.cfg in bin64 despite all six current library
files. The user requires clean removal. An updated-DMM test is pending; no direct
file cleanup was performed to hide this deployment behavior.

The same behavior was reproduced after updating to DMM2.8.1: package removal left
both Telemetry DLLs and both CFG files, and reimport retained the older deps.cfg.
After the user manually removed those four owned leftovers with the game closed,
a clean DMM2.8.1 import deployed all six rc.1 files with exact matching hashes.

After an NVIDIA driver update to32.0.16.1692, the first game start rebuilt shaders
and native capture hit Windows error258 (WAIT_TIMEOUT) during the long initial
load. A second start did not reproduce it. Release2.1.10 therefore waits for the
host's first valid player/render-camera `playing` state before arming light/sky
GPU capture and separates command-list submission timeout from GPU-fence timeout.
This local cause is distinct from the unresolved external2.0.2 crash report.

Windows Explorer/taskbar has also displayed “not responding” while the local game
continued normally. During the report, `IsHungAppWindow` was false and the process
reported `Responding=true`; no actual application hang was established.
