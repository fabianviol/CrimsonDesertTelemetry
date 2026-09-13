+# Current compatibility reports

## Two current Nexus reports — 2026-09-13

Source: <https://www.nexusmods.com/crimsondesert/mods/3374?tab=posts>.
All nine visible comments and their timestamps were checked; the two newest reports
are the current evidence.

At 05:04, WHOLE reported 2.1.10 on Steam build 25246367, RTX 4090 and NVIDIA
driver 616.92. Streamline DLSS/DLSS-RR 2.11.1 fails
`linkSwapchainToCmdQueue` and `CreateSwapChainForHwnd` with
`0x80070005 (E_ACCESSDENIED)` immediately before a `0xC0000005` crash at
`CrimsonDesert.exe+0x3D0FEA6`.

At 08:49, jimos87 linked the DMM support package
<https://drive.proton.me/urls/V1AX5VHNV0#sIhaw6yiTPQJ>. Its game logs reproduce
the same Streamline error on an RTX 4080 SUPER with driver 616.92 and show that
the failure is intermittent. The dump has the same exception offset and proves
that the loaded Telemetry module is the exact public 2.1.10 ASI (timestamp
`0x6AA59B26`, image size `0x17C000`). Steam's overlay DLL, Streamline,
DesertLinkCore and MasterLooter were also loaded. This evidence supersedes the
earlier unverified stale-package theory for these crashes.

The owner also uses driver 616.92, on an RTX 3080, and has never reproduced this
crash. Thus all three relevant systems share the driver, while only the two external
RTX 4080 SUPER/4090 systems currently reproduce the failure. In jimos87's dump the
faulting game instruction dereferences `rcx == 0`; the crash thread retains
CrimsonDesert/Streamline/NVIDIA-driver frames and no Telemetry frame. The immediate
game exception is therefore the consequence of the missing graphics object after
the logged swapchain failure, while Telemetry remains the cause under investigation
for that earlier failure.

The first concrete unsafe ordering is in the overlay factory callback: it installed
five hooks on the just-created swapchain before NVIDIA Streamline's outer factory
wrapper had finished linking that chain to its command queue. The 2.1.11-rc.2
candidate defers only that tracking/hook step by 500 ms on the existing worker.
NVIDIA's official Streamline v2.11.1 tag confirms the call order: the base
`CreateSwapChainForHwnd` returns before Streamline runs its after-hooks and
`setupSwapchainProxy`. Telemetry 2.1.10 called `Track()` on that inner return path.
The public 2.1.10 release remains unchanged. Live multi-start validation on driver
616.92 is pending; do not state that the external crash is fixed until it passes.

## Historical report relayed on 2026-09-12, posted around 08:30

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
load. The owner then reproduced a new shader compilation with the mod disabled,
closed the game during compilation, activated the mod and restarted directly into
the game without another compilation. In this observed A/B sequence the compile
was initiated without Telemetry and activation did not initiate one. A second
modded start did not reproduce the capture failure. Release2.1.10 therefore waits for the
host's first valid player/render-camera `playing` state before arming light/sky
GPU capture and separates command-list submission timeout from GPU-fence timeout.
This local cause is distinct from the unresolved external2.0.2 crash report.

Windows Explorer/taskbar has also displayed “not responding” while the local game
continued normally. During the report, `IsHungAppWindow` was false and the process
reported `Responding=true`; no actual application hang was established.
