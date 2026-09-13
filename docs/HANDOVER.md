# Current checkpoint — Nexus crash compatibility, 2026-09-13

> This section supersedes the older source-visibility checkpoint below. The user has
> explicitly paused all occlusion research until the current Nexus crashes are fixed.

## Current external evidence

The Nexus posts page contained nine comments when checked on 2026-09-13. In
chronological order:

- jimos87, 2026-09-10 11:40: initial crash report.
- WHOLE, 2026-09-12 06:45: 2.0.2 package/deployment report and graphics-feature
  crash matrix; backend-only operation worked.
- fabianviol, 2026-09-12 10:51: replied to both threads, acknowledged the
  failures and requested native log/crash evidence from WHOLE.
- jimos87, 2026-09-12 17:18: current download still crashed.
- fabianviol, 2026-09-12 19:38: requested a clean 2.1.10 install and exact logs.
- fabianviol, 2026-09-12 19:40: announced 2.1.10 and requested the same controls.
- **WHOLE, 2026-09-13 05:04 (current):** build 25246367, RTX 4090, NVIDIA
  616.92, all six files copied manually, reported overlays off. Streamline
  DLSS/DLSS-RR 2.11.1 logs `linkSwapchainToCmdQueue` /
  `CreateSwapChainForHwnd` failure `0x80070005 (E_ACCESSDENIED)`, followed about
  one second later by `0xC0000005` at `CrimsonDesert.exe+0x3D0FEA6`.
- **jimos87, 2026-09-13 08:49 (current):** 2.1.10 sometimes reaches the game but
  still fails; supplied a DMM support package:
  <https://drive.proton.me/urls/V1AX5VHNV0#sIhaw6yiTPQJ>.

The support ZIP is preserved read-only under
`%TEMP%/nexus3374-support-jimos87-20260913`; no downloaded binary was executed.
Its SHA-256 is
`6062BD48244BE56220C75C14EED06FE55D66070BC0B3A0DF186DE44CA242DBE0`.
It proves DMM 2.8.1, RTX 4080 SUPER, driver 616.92, game build 25246367 and an
intermittent pattern: one enabled run exited normally, while the next enabled run
logged the same Streamline `E_ACCESSDENIED` and crashed.

The minidump proves the loaded Telemetry ASI is the exact public 2.1.10 build:
PE timestamp `0x6AA59B26`, mapped size `0x17C000`, matching local SHA-256
`8028E9EE43E846F79075618F9B7A522F5F66E2EB1784FFB48AF178AAFC0C0A78`.
It also contains Streamline, Steam's `gameoverlayrenderer64.dll`,
`DesertLinkCore_v1.0.7.asi` and `MasterLooter.asi`. The exception is the same
`CrimsonDesert.exe+0x3D0FEA6` reported independently by WHOLE. This is no longer
an unverified stale-file or packaging hypothesis.

The owner, WHOLE and jimos87 all used NVIDIA driver **616.92** in the relevant
tests (RTX 3080, 4090 and 4080 SUPER respectively). The shared driver is therefore
a compatibility condition worth retaining, but it is not sufficient to reproduce
the crash: the owner has never seen it. Parsing jimos87's exception context against
the exact local game executable (matching PE timestamp `0x6AA22ABB` and image size
`0x16B0E000`) proves that the faulting instruction is `mov rax, [rcx]` with
`rcx == 0`. The crash thread's retained stack contains CrimsonDesert, `sl.common`,
`sl.reflex`, `sl.interposer` and `nvwgf2umx`, but no Telemetry frame. This supports
the sequence shown by the game log: swapchain creation fails first, then the game
dereferences its missing graphics object. It does not by itself absolve Telemetry
of causing the preceding swapchain failure.

## First concrete defect and narrow fix

`overlay_graphics.cpp` hooked Present/Present1/Resize/SetColorSpace synchronously
inside the real DXGI `CreateSwapChainForHwnd` detour. At that point the inner DXGI
call has returned, but NVIDIA Streamline's outer wrapper has not yet associated the
new swapchain with its command queue. The code therefore modified the swapchain
implementation during Streamline's still-active creation path, exactly before its
`linkSwapchainToCmdQueue` failure.

This ordering is confirmed by NVIDIA's official Streamline **v2.11.1** source
(tag commit `019994e18d256a3e92347888deb527feb7f58bc0`), the exact Streamline version
reported by both users. Its `IDXGIFactory2_CreateSwapChainForHwnd` calls the base
factory first, then invokes Streamline's after-hooks, and only afterward calls
`setupSwapchainProxy`. Telemetry 2.1.10 ran `Track()` while that base call was
returning, so its five swapchain hooks were installed before those two Streamline
steps. The rc.2 delay therefore removes a demonstrated ordering violation rather
than merely tuning a timeout after the crash.

The hotfix retains the returned COM objects without inspecting or patching them.
The existing overlay worker calls `Track()` 500 ms later, after the wrapper stack
has completed. No research hooks, tracing system or new production feature was
added. The public 2.1.10 artifact remains unchanged on Nexus.

Work is isolated on branch `codex/streamline-swapchain-fix` in
`C:/DEV/CrimsonDesertTelemetry-hotfix`, based on public `origin/main` commit
`a245299`. Paused source-visibility commit `f750974` is preserved on branch
`codex/source-visibility-paused` and was not included.

Candidate package (rc.1 was an equivalent intermediate build and is superseded):

- `artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.11-rc.2-ModManagers.zip`
- production `CDT_RESEARCH=OFF` ASI: 1,275,392 bytes, SHA-256
  `95D733491FB8C09CCD7D80C5800F87C6379BFD6B178299C1902D6F0F89541EEF`
- ZIP: 799,538 bytes, SHA-256
  `3EE1F93368E673A876218C9F7C89DB2C47EF37962FA64F103E9EA1D0D8F07226`

Build, managed tests and package self-test pass. All eight D3D12 HUD/notification/
light-marker SDR/scRGB smoke modes pass after being updated to assert the deliberate
post-create delay. Microsoft Defender found no threats in either exact rc.2 file.
VirusTotal is intentionally deferred until a live-validated final artifact exists.
Repeated starts by an affected 616.92 user are still required before calling the
crash fixed because their evidence proves the old failure is intermittent.

### Local live validation — pass 1 of 3

On 2026-09-13 at 15:03, the owner launched the exact rc.2 ASI on an RTX 3080 with
NVIDIA driver 616.92. The installed ASI hash matches the candidate above. The
overlay log records the intended sequence:

```text
Waiting for the game's D3D12 swapchain.
Game D3D12 swapchain created; waiting for graphics wrappers to finish.
Overlay ready: D3D12 / SDR.
```

After five minutes the game process was still responsive. Native light capture
was armed after the playable-world signal, recurring rendered-light capture was
ready, `/v1/health` reported `playing`, and player/camera, authored lights,
rendered lights and Ambient were all live. This is the first successful cold-start
control on the affected driver. The owner has never reproduced the reported crash
during development, so repeating local starts would add little evidence. External
RTX 4080/4090 acceptance by the affected users is the decisive test.

## One next step

Send the exact 2.1.11-rc.2 ZIP to at least one of the two affected users for three
clean starts with Overlay, Notifications, Lights and LightOverlay enabled. One
affected user reproducing three successful starts is enough for the first external
acceptance gate; the second user is useful confirmation, not a prerequisite for
learning whether the ordering fix works. Ask whether the HUD/markers initialize and
whether the Streamline `E_ACCESSDENIED` remains. If a run still crashes, preserve the
new game log and dump before changing code; compare the exception offset and loaded
modules to this checkpoint.

---

# Previous checkpoint — per-light direct visibility, 2026-09-12 (paused)

## Do not confuse the Nexus release with the research package

The public Nexus release is still the unchanged **2.1.10** production package,
uploaded on 2026-09-12 at 21:28. Nothing built after that upload was sent to
Nexus. The authenticated Nexus API confirms 2.1.10 is the active main version.

Exact published 2.1.10 artifacts:

| File | Bytes | SHA-256 | VirusTotal |
| --- | ---: | --- | --- |
| production ASI | 1,267,712 | `8028E9EE43E846F79075618F9B7A522F5F66E2EB1784FFB48AF178AAFC0C0A78` | 5/71 |
| production ZIP | 791,357 | `1A8DD7641E9431AA0FE383E68706D9519A0B93E2ED5E6A70129C4C4552DEBD24` | 0/68 |

Nexus reports that public file as safe. Do not remove, replace, warn about or
otherwise change the Nexus 2.1.10 release because of the later research scan.

The higher **ASI 11/71 / ZIP 7/68** results belong exclusively to the superseded
local `2.1.11-source-visibility.2` `CDT_RESEARCH=ON` package built around 23:18.
That package adds SDF readback, a player-to-light IPC bridge and other research
instrumentation. It was never uploaded to Nexus and must remain private. GitHub
`main` contains its source and documentation for continued development; this does
not alter the immutable production binary already hosted on Nexus.

## Previous priority (paused)

Focus only on this question for each local light already known to the project:

```text
Does solid world geometry block the direct segment from the player to this source?
```

Camera direction, on-screen state, Ambient, image brightness and bounced light are
outside this test. Fire, torch, candle, lamp, point and spot sources use the same
geometry rule. Stable release 2.1.10 and its hotfix/packaging work are complete;
do not reopen them unless they directly block this task. Do not work on CrimsonHue.

The production goal still requires both Ambient Occlusion and per-light Source
Occlusion in `CDT_RESEARCH=OFF`. Ambient already passed its controlled
open -> enclosed -> open test. Per-light visibility is not complete until research
and production each pass controlled live visible -> blocked -> visible tests,
including a retained source behind or outside the camera view.

## Current implementation

The previous camera-origin, renderer-only zero-crossing trace was rejected for two
measured reasons: it did not cover all known sources or the requested player
receiver, and filtered t233 wall samples can remain slightly positive.

The new research candidate consists of:

- `SourceVisibilityClient`: publishes player position + `(0,1,0)` and the spatially
  deduplicated union of authored and rendered light positions;
- `source_visibility_bridge`: a separate seqlocked native query/result mapping;
- `sdf_visibility`: quarter-cell segment sampling, near-surface threshold
  `0.5 * cellSize`, and blocked after one complete cell of connected samples;
- schema 1.5 metadata on both authored and rendered records, position-matched back
  without changing positions, RGB, raw records or smoothed contributions.

No camera direction or projection exists in the new bridge. The native worker
traces every new query against one immutable SDF volume. Unknown/stale results do
not hide or attenuate API records. The old render-capture visibility writer is
explicitly disabled while this research bridge is active.

The bridge is bounded to the nearest 256 distinct source positions, merges within
0.1 game unit, rejects a response after the player receiver moved more than
0.75 game unit, and expires results after 1500 ms. Sources absent from both input
feeds cannot be classified and must never be called blocked.

Implementation and API details: [SOURCE_VISIBILITY.md](SOURCE_VISIBILITY.md).
Preserved live failures/captures: [SOURCE_VISIBILITY_REGRESSION.md](SOURCE_VISIBILITY_REGRESSION.md).

## Evidence before live test

The exact native classifier reproduces all 17 preserved labelled controls:

- room: two blocked and two clear sources;
- same room source: three fresh clear-pose captures;
- Serkis: clear -> blocked -> clear;
- Warspike: two clear -> three blocked -> two clear.

This is offline replay of real retained volumes, not live acceptance. Relevant
automated checks pass:

- complete managed test executable, including the schema 1.5 player/query bridge;
- native SDF clear/blocked/freshness/context controls;
- native direction-independent query/result bridge;
- 384 WARP/debug-layer readback controls;
- research ASI compile and expanded/ZIP package validation.

The JSON schema parses successfully. `AGENTS.md` and `CLAUDE.md` are byte-identical.
The first `.2` game start exposed one compatibility defect: the managed host
correctly emitted new schema 1.5, but the ASI HUD parser still accepted only
through 1.4 and therefore labelled the entire stream incompatible. `.3` adds 1.5
to that strict allowlist and its additive-schema HUD regression. This was not an
ASI/DLL file mix and has no effect on Nexus production 2.1.10.

## Exact package for the next tester

Use only:

`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.11-source-visibility.3-ModManagers.zip`

This is a private `CDT_RESEARCH=ON` package for Steam build 25246367, not a public
release. It contains its own research README and a ready test INI:

- `SourceVisibility.Enabled=1`
- `Overlay.ShowDetails=1`
- `LightOverlay.HideOccluded=0` so wrong verdicts remain visible
- `Research.SignedDistanceReadbackIntervalMs=1000`
- up to 120 automatically started SDF transactions

Exact artifacts:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `CrimsonDesertTelemetry.asi` | 1,591,296 | `7C9B007E8DA4F73FDAF8047F5E9C74478E70005A226EBB82D6C6D9B2A37CCC02` |
| test ZIP | 943,190 | `96AB18A4E9BFF21694F9173CFB50A41F1CCE83A81405A3807DDDA8ED3FD7112E` |

Local Microsoft Defender custom scans passed both exact files with no threats.
Completed VirusTotal results for these exact `.3` research artifacts are
**ASI 11/71** and **ZIP 7/68** (the ZIP has 60 undetected and one analysis
failure; the ASI has one timeout and one failure). This is a
significant regression from the stable production build and blocks using this
architecture as a release candidate. It does not invalidate a private functional
test, but it must not be published or promoted to `CDT_RESEARCH=OFF` unchanged.
Investigate it later as a product/build regression without obfuscation or signature
gaming. The earlier `.1` and schema-incompatible `.2` artifacts are superseded
and must not be tested.

## One next step

With the game closed, install the `.3` ZIP cleanly through DMM. In the known room,
hold one player position where two identified sources are physically clear and two
are behind solid wood. Keep F11 in show-blocked mode. Verify that all four records
receive the correct status. Rotate the camera without moving the player and verify
retained verdicts do not change. Then perform visible -> blocked -> visible on one
identified fire/candle/lamp.

If the live result fails, capture the exact source positions, player position,
verdicts, `closestApproach`, `volumeSequence`, `contextFrame` and ages before tuning
anything. If it passes across relevant source types and behind-camera coverage,
move only the minimal bridge/classifier/acquisition pieces into `CDT_RESEARCH=OFF`
and repeat the same production test.

## Worktree care

Do not stage the unrelated root-level capture/VT helper scripts, JSON outputs or
scratch C++ files already present as untracked evidence. Do not delete prior
artifacts. Commit only the explicit implementation, tests, package tooling and
documentation for this candidate.
