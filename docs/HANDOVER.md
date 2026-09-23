# Current checkpoint — handover to Codex, 2026-09-23, Claude

Nine days with no commits. This replaces four stacked checkpoints from 13–14
September with one statement of where everything actually stands, verified today
rather than recalled. Codex's own 13 September checkpoints stay below as history;
the fire-visibility one still holds the live test that was never run.

## Verified today

| | state |
|---|---|
| Nexus | `2.1.11` is `main`, uploaded 09-13 17:54, `2.1.10` archived. Nothing newer uploaded. |
| Git | `origin/main` == local, nothing unpushed, last commit `b80e22e` on 09-14. |
| Machine | **The mod is not installed.** `bin64` holds only leftover `crimson-desert-telemetry.deps.cfg` and `.runtimeconfig.cfg` — the known DMM removal behaviour, visible again. Game not running. |
| `C:\CrashDumps` | **empty.** WER has been armed for `CrimsonDesert.exe` since 09-13 and the unexplained local crash has not recurred in nine days. |

## The crash is done

`2.1.11` fixes a proven ordering violation: Telemetry installed its HUD swapchain
hooks synchronously inside DXGI's `CreateSwapChainForHwnd`, before Streamline had
associated the new swapchain with its command queue, which is exactly where the
users' `E_ACCESSDENIED` came from. Verified against NVIDIA's published Streamline
v2.11.1 source.

**jimos87 confirmed it externally** on 09-14 09:57: "It is now working for me :D ty"
(RTX 4080 SUPER, 616.92, DMM 2.8.1). That is one user, not the three consecutive
starts the acceptance gate asked for, and his own evidence had shown the failure was
intermittent. WHOLE (RTX 4090) never reported back. Public wording says one of two
confirmed and should not be tightened further without WHOLE.

Two things remain open and are recorded rather than solved: nobody ever answered how
many monitors the affected users run, and the owner's own 09-13 19:20 crash has no
dump and no explanation. Both of the owner's local crashes happened on a two-monitor
extended setup during an output change — a configuration the users may not share, and
a different signature (runtime, `+0x3D05303`) from theirs (startup, `+0x3D0FEA6`).
Do not merge those two.

## Goal 1 is done; goal 2 is explained, not solved

**Ambient / sky occlusion works and ships.** Measured 0.3916 on open ground, 0.0476
in an open-fronted stable, and **exactly 0** in a closed interior. Its one product
consequence: `sky × visibility` drives a lamp to black indoors, so the consumer needs
a perceptual curve with a floor. That is not optional.

**Per-light occlusion: the blocker is now explained.** See
[SDF_BAND_LIMIT.md](SDF_BAND_LIMIT.md). The field stores a thin signed band around
surfaces, not solid interiors: probing down through ground the player stands on, the
negative region is 0.6 gu wide (room) and 0.3 gu (camp), reaching −0.181 and −0.092,
and immediately below it the value returns to **+0.53027**, the positive clamp. So
"inside" is not a persistent state, and a ray registers a nonpositive value only when
it crosses that one-to-two-cell band in a way trilinear interpolation preserves —
which the room case proves it often does not.

**Therefore `value <= 0 along the segment` cannot be made reliable here.** Not by
smaller steps, denser sampling or a larger tolerance. Tracing, addressing and
acquisition are all fine; offline traces reproduce the native `closestApproach`
values exactly and the geometry is present as negative texels.

Two repairs are ruled out with evidence, not argument:

- **Counting solid texels beside the path** — refuted on the controlled same-source
  A/B: blocked pose touched 4, the three exposed poses touched 3. A room always has
  walls and a floor nearby.
- **Raising the hit tolerance** — it looks tempting because within the traced range
  the room separates by a factor of thirty (blocked 0.005/0.016, clear 0.529/0.517 at
  the clamp), but that is fitted to one room. Note the regression table's 0.160 and
  0.125 for the clear controls are **complete-path** minima landing on the lights' own
  housings inside the end margin; `left-3.0` has length 7.733 with its minimum at
  exactly 7.733.

Still worth fixing separately: the forced 0.05-gu minimum step skipped a genuine
negative interval on `near-box-b` (dense sampling finds −0.0073 at t = 13.25 against a
traced verdict of clear). A sphere trace is only valid stepping by at most the sampled
distance. It recovers that one case and no other.

## The decision that blocks everything

A swept query — "does anything come within radius r of the segment" — is the only
remaining candidate on this data, because it uses the distance rather than relying on
the sign surviving interpolation. Checked at the room minima: both blocked paths have
their nearest surface to the side, not the floor, so it would not merely be detecting
ground. It is untested and inherently conservative.

**Before building it, the product question should be answered:** the consumer is a set
of Hue lamps, not a renderer. Does it need per-source geometric truth, or roughly how
much light reaches the player and from where — which the working ambient feed already
answers? The user has not yet been asked this. Asking is cheaper than building for
either answer.

## Untested and waiting

`v2.1.12-fire-visibility.2` is built, scanned (ASI 4/71, ZIP 0/68) and **never
installed**. Its exact next test is in Codex's own checkpoint below and has not been
performed: one known fire, `visible -> blocked -> visible`, F11 in show-blocked mode
first, then rotate the camera without moving to confirm direction does not change the
verdict. Do not claim candles, lamps or the all-source requirement from one pass.

## Housekeeping, recorded not done

26 untracked throwaway files sit in the repository root (`capture_aba*.py`,
`parse_aba*.py`, `vt_*.py`, `test.cpp`, `debug_test.cpp`, `set_ini.py`, `temp_zip/`)
plus `native/.../src/spatial_readback_stub.cpp`, which is in no CMake list. They are
not mine and may hold the only copy of an analysis, so they were left alone.

## One next step

Ask the user how much per-light fidelity the lighting product actually needs. If the
answer is "geometric truth per source", the swept query needs a live controlled test
with `r` chosen on physical grounds. If "a conservative approximation is fine", the
existing narrow fire path plus ambient may already suffice and the remaining work is
in the consumer, not the renderer.

---

# Previous checkpoint — narrow rendered/fire visibility restored, 2026-09-13 late, Codex

## User decision and scope

The user ended the feature pause after 2.1.11 publication and asked for the earlier
live-working fire visibility behavior back **without new renderer research**. This
checkpoint restores only that small production path. Do not treat it as completion
of the all-light/player goal, and do not replace it with the broader research bridge
unless a later controlled failure requires that work.

Public 2.1.11 remains unchanged on Nexus. A matching GitHub release now also exists:
<https://github.com/fabianviol/CrimsonDesertTelemetry/releases/tag/v2.1.11>.
Its tag points to stable commit `a8c5ed87925a688532b50b00102d90a46ee69d46`
and its uploaded asset is the exact existing stable ZIP, SHA-256
`CB7DD68FFAA548A53F2A5303870C012F9CD0CC44944A46A0F59BF274457C5052`.
The new visibility candidate below has **not** been published there or on Nexus.

## Concrete production restoration

`CDT_RESEARCH=OFF` now:

- compiles `sdf_acquire.cpp`, which retains one fenced current R16 SDF volume;
- links `sdf_visibility.cpp` as `cdt_sdf_fire` with the exact earlier Variant A
  classifier: 0.6 start skip, 1.0 end margin, `max(distance, 0.05)` steps,
  nonpositive sample = blocked, at most 400 iterations;
- classifies only current renderer-selected records in `render_bridge.cpp`, from
  their capture-paired camera position. Camera direction and projection are not
  inputs, so a retained behind-camera record is still eligible;
- reads `[SourceVisibility] Enabled`, publishes additive metadata and restores
  `HideOccluded` plus configurable/default F11 in both light views;
- leaves raw and smoothed records, positions and RGB unchanged. Unknown/stale
  records remain displayed even when hiding is active.

The host creates the broad managed player/all-known-source query mapping only when
the ON ASI passes its private `--research-source-visibility` flag. OFF does not run
that client or compile the native result bridge, history, diagnostic series, dump
or trace infrastructure.
This is the same architectural family that previously produced working fire
verdicts; candles, lamps, authored-only sources and a complete 360-degree registry
are not claimed.

## Exact candidate and verification

Private package:

`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.12-fire-visibility.2-ModManagers.zip`

Expanded folder:

`artifacts/mod-manager/v2.1.12-fire-visibility.2-20260913-235738-037-8b9b65b8/CrimsonDesertTelemetry`

| Exact file | SHA-256 | VirusTotal | Microsoft Defender |
| --- | --- | --- | --- |
| ASI | `03989712E34BF418BB326A835A998CAC206863775D8EB182BA98A2C55ADAC1D7` | **4/71**: CrowdStrike, Cynet, McAfeeD, Microsoft | No matching detection |
| ZIP | `B2CF14D58F9F31F6238C617D77B66499CE13BCF3D88A55C12170CF1CA719C0BD` | **0/68** | No matching detection |

The VT result is equal to/better than the immediate v2.1.9 predecessor (4/71) and
earlier narrow visibility candidates (5/70–5/71, ZIP 0/67–0/68). It does **not**
reproduce the later broad research-bridge regression (ASI 11/71, ZIP 7/68).
This directly answers the user's AV question: restoring the narrow path did not
damage the measured scan profile. The known deterministic Bitdefender family came
from commit `6937fa9`'s bounded repeated diagnostic readback series, which remains
compiled out of OFF; see `ANTIVIRUS_FINDINGS.md`.

Verification completed against this production build:

- all **30/30 native CTests** pass;
- all **71/71 managed tests** pass;
- the new `release-fire-visibility` test covers first-volume waiting, clear/blocked
  fields, sources in front of and behind the camera, disabling, and byte-for-byte
  raw scene/light/counter preservation;
- the production package validator accepts all **34** offered settings, rejects
  research-only settings, verifies all nine files and verifies ZIP/staging equality.

## One next step

With the game closed, install the exact private ZIP above cleanly. Use one known
fire and perform `visible -> solid wall/building blocked -> visible`, keeping F11 in
show-blocked mode first so wrong verdicts cannot disappear. Rotate away if useful:
camera direction must not change the verdict while the renderer record remains in
the feed. If that passes, repeat with F11 hiding, record the live result here and
decide whether this limited fire restoration is releasable. Do not claim candles,
lamps or the final all-source product requirement from that one pass.

---

# Previous checkpoint — 2.1.11 PUBLISHED, 2026-09-13 evening, Claude

> Supersedes the "One next step" of the 18:46 checkpoint below, which still asked for
> external validation BEFORE publishing. The user overrode that and released.

## What changed after 18:46

**`v2.1.11` is live on Nexus.** Published 19:54, verified through the authenticated
API: `2.1.11 main 09/13/2026 19:54:00 id=38521561693168`, with **2.1.10 archived** at
the user's explicit instruction and the page version moved. Changelog appended from
`docs/releases/v2.1.11.md`. The exact ZIP `CB7DD68F…5052` and ASI `06EE760E…1B63`
were re-hashed from the published archive itself before upload, not trusted from the
documentation.

**The publish tool was broken and is fixed (`380eaa9`).** The first attempt died with
a bare 403 on the presigned storage PUT. `tools/nexus/NexusMods.psm1` deliberately
left `Content-Type` unset, with a comment claiming it is not signed. That comment was
wrong. Reading the storage provider's own error body showed
`X-Amz-SignedHeaders=content-disposition;content-md5;content-type;host`, and Cloudflare
R2 rebuilding the canonical request with an empty content-type. Nexus signs for
`application/octet-stream`; `application/zip` is rejected with the same 403. The lesson
worth keeping: a presigned PUT's 403 body carries the whole diagnosis, and the module
was discarding it.

**`docs/NEXUS_DESCRIPTION.bbcode` corrected in two places.** Its "Validation and
integrity" section still said the package needed the live second-monitor/HDR
replacement test -- that ran today and passed, so it now states the live validation
actually performed and leaves external confirmation as the only open item. And "What
the measurements mean" now says the linear HDR values are unclamped, routinely exceed
1.0 and are not 0-255 colours; the owner asked exactly that question, so users will.
Everything else of Codex's 2.1.11 rewrite is untouched, including the schema 1.5
statement, which matches `lightsSchemaVersion = "1.5"` in Program.cs.

## New evidence: a local crash the fix did NOT prevent

At about 19:20 the game ended after 2 h 15 min with 2.1.11 installed, following an
HDR on -> off change. **Telemetry's replacement path had completed successfully**
moments earlier -- `overlay.log` ends at 19:19:51 with the full sequence including
2.1.11's new `Existing HUD swapchain released for replacement.` line, then
`Overlay ready`. No `CreateSwapChainForHwnd` failure, no `E_ACCESSDENIED`, no
exception in any Telemetry log. Logs preserved under
`artifacts/crash-reports/local-20260913-1919-hdr-off/` with a CONTEXT.txt stating
what they do and do not establish.

**There is no dump for it.** The game's own handler did not write `last_crash.dmp`,
and Windows Error Reporting was configured per-application only, without
`CrimsonDesert.exe`. So the process vanished without either mechanism catching it,
which is itself a difference from the 16:17 crash that did produce one.

**WER is now armed** (user ran it elevated, verified):
`HKLM\...\Windows Error Reporting\LocalDumps\CrimsonDesert.exe`, DumpFolder
`C:\CrashDumps`, DumpType 2 (full), DumpCount 10; the folder exists. The next
unhandled crash produces a full dump automatically. This does not cover a hard
termination or a driver-level TDR.

**Do not merge this with the users' crash.** Codex's earlier warning was right and I
briefly violated it. The user then raised the decisive point: their machine now has a
**two-monitor extended setup with HDR on one screen**, and reaching the Windows
display settings requires leaving the game. Both local crashes happened during an
output change in that configuration. The users' crashes are at **startup**, with a
different signature. Whether the two overlap is unknown -- nobody has asked the users
how many monitors they run.

Retests tonight after the crash, all passing, all with 2.1.11: HDR switch at the main
menu (no HUD swapchain to release, so the new path did not even run), then HDR switch
in game (the release path did run, logged cleanly, no crash). So the crash is
**intermittent**, matching jimos87's report of one good run followed by a bad one. The
only obvious difference in the crashing run is uptime: 2 h 15 min versus ~20 min.

## Replies drafted for both Nexus users

Full text given to the user for posting, not yet posted. Both credit the specific
evidence that made the diagnosis possible -- WHOLE's Streamline log and exception
offset, jimos87's minidump and his observation of intermittency -- state plainly that
the cause was ours, and explicitly do NOT claim the crash is fixed for them. Both ask
for three consecutive starts and, for the first time, **how many monitors they run and
whether HDR is active**. jimos87's additionally covers the DMM leftover cleanup.

## Verified machine state

Installed and running 2.1.11. Five of the six deployed files match the published ZIP
byte for byte; only `crimson-desert-telemetry.deps.cfg` differs, carrying older
version labels with identical runtime assets -- the known DMM behaviour, now measured
rather than assumed.

## One next step

Post the two replies and wait for an affected user. Until one of them reports three
clean starts, 2.1.11 is a diagnosed and locally verified fix, not a closed report --
that wording is in the description and the changelog and should not be tightened
without their evidence. If a local crash recurs, `C:\CrashDumps` will now hold a full
dump: compare its exception offset and loaded modules against the two known cases
(`+0x3D0FEA6` external startup, `+0x3D05303` local runtime) before touching code.

---

# Previous checkpoint — Nexus crash compatibility, 2026-09-13 18:46, Codex

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
tests (RTX 3080, 4090 and 4080 SUPER respectively). Parsing jimos87's exception
context against the exact local game executable (matching PE timestamp
`0x6AA22ABB` and image size
`0x16B0E000`) proves that the faulting instruction is `mov rax, [rcx]` with
`rcx == 0`. The crash thread's retained stack contains CrimsonDesert, `sl.common`,
`sl.reflex`, `sl.interposer` and `nvwgf2umx`, but no Telemetry frame. This supports
the sequence shown by the game log: swapchain creation fails first, then the game
dereferences its missing graphics object. It does not by itself absolve Telemetry
of causing the preceding swapchain failure.

Disassembly of the exact game image now identifies that missing object precisely.
The fault is in the routine beginning at RVA `0x3D0FE00`; it loads the member at
`[rbx+0xA8]` and calls vtable offset `0x120`, which is method slot 36,
`IDXGISwapChain3::GetCurrentBackBufferIndex`. The same member is used earlier with
vtable offset `0x50` and fullscreen arguments, matching
`IDXGISwapChain::SetFullscreenState`. Therefore the access violation is the game's
direct null dereference of its swapchain member after Streamline reported that the
swapchain could not be created/linked. HDR is not implicated: WHOLE reproduced the
same failure in SDR.

The exact shipped `sl.dlss_g.dll` (SHA-256
`DAE3F24A690F3DEE3FBD89BDDE47FBEB6BF480CEE376ECFD0242A50E0BB6E6C6`) was also
disassembled. The `linkSwapchainToCmdQueue` error path at DLL RVAs
`0x4B735`/`0x4BC31` logs the negative HRESULT returned directly by its internal
factory `CreateSwapChain`/`CreateSwapChainForHwnd` call. Combined with the official
v2.11.1 factory order, this places the failure in DLFG's post-create linking work:
the base swapchain has been returned, but the Streamline after-hook has not finished.
That is the precise interval in which 2.1.10 installed five shared swapchain-method
hooks and which 2.1.11 now leaves untouched. The external startup failure remains
distinct from the owner's later runtime display-transition crash until affected-user
tests establish whether both were fixed.

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
steps. The deferred tracking therefore removes a demonstrated ordering violation rather
than merely tuning a timeout after the crash.

The first rc.2 attempt still retained the newly returned swapchain in a pending
`ComPtr`. Microsoft documents that a flip-model HWND can have only one swapchain;
keeping the old one alive can make replacement creation fail. The owner then
reproduced that exact DXGI failure locally at 16:17:07 by switching the game's
output/HDR state for a newly connected second monitor. The game logged
`CreateSwapChainForHwnd failed: -2147024891` twice and crashed with `0xC0000005`
at `CrimsonDesert.exe+0x3D05303`. This is a runtime replacement path and a different
downstream crash routine from the users' startup RVA `0x3D0FEA6`; do not merge the
two acceptance claims.

The local DMM support ZIP and its 17,409,728-byte minidump are preserved under the
Git-ignored `artifacts/crash-reports/local-20260913-161707`; dump SHA-256 is
`290587A6193989B6AE8D8E23A4A7DF71BB7774498D3A9A59F3CF178310D2DF68`.
Disassembly of the local fault shows a null/invalid structure at `mov rsi,[rax+30h]`
inside the game's swapchain/backbuffer rebuild routine, not the external users'
`GetCurrentBackBufferIndex` null call.

The final narrow fix records each pending HWND and, before either hooked DXGI
factory create function runs, releases only Telemetry's pending or active
swapchain/renderer/queue references for that same HWND. It waits for submitted HUD
GPU work before releasing active resources. The worker still waits 500 ms before
installing presentation hooks. No research hook, tracing system or new production
feature was added.

The completed fix is commit `a8c5ed8` (`Release swapchain replacement compatibility
fix`). It is pushed to both `origin/main` and
`origin/codex/streamline-swapchain-fix`; local `main` is at the same commit. The
implementation was prepared in `C:/DEV/CrimsonDesertTelemetry-hotfix`. Paused
source-visibility commit `f750974` is preserved on branch
`codex/source-visibility-paused` and was not included.

Exact final-version package used for the local acceptance test:

- `artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.11-ModManagers.zip`
- production `CDT_RESEARCH=OFF` ASI: 1,276,416 bytes, SHA-256
  `06EE760E33E499252AF072711C28D55D2D65127E50B98A3E4FD26B39ADEC1B63`
- ZIP: 800,147 bytes, SHA-256
  `CB7DD68FFAA548A53F2A5303870C012F9CD0CC44944A46A0F59BF274457C5052`

Build, 71 managed controls, 30 native CTests and package self-test pass. All eight
D3D12 HUD/notification/light-marker SDR/scRGB smoke modes now create a replacement
immediately while the first chain is pending and again after the HUD accepted the
next chain. Microsoft Defender found no threats in either exact 2.1.11 file.
VirusTotal reports 0/68 for the exact ZIP and 4/71 for the exact ASI. The ASI
result is not a regression from public 2.1.10's 5/71 result. Local Defender found
no threat in either exact file.
The source and release documentation are on GitHub. Before telling users that
2.1.11 is available on Nexus, verify the Nexus file entry itself; the last
independently confirmed public Nexus binary in this checkpoint was 2.1.10.
Repeated starts by an affected 616.92 user are still required before calling the
external startup crash fixed because their evidence proves the old failure is
intermittent.

### Local live evidence

On 2026-09-13 at 15:03, the owner launched the exact rc.2 ASI on an RTX 3080 with
NVIDIA driver 616.92. That superseded ASI's SHA-256 was
95D733491FB8C09CCD7D80C5800F87C6379BFD6B178299C1902D6F0F89541EEF. The
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
control on the affected driver. At 16:17 the later monitor/HDR switch reproduced
the separate runtime replacement crash described above.

The owner then installed the exact final 2.1.11 production package and started the
game at 17:04 on the same RTX 3080 / NVIDIA 616.92 system. The installed ASI hash
was verified as
`06EE760E33E499252AF072711C28D55D2D65127E50B98A3E4FD26B39ADEC1B63`, exactly
matching the final package. At about 17:16 the owner repeated the same output/HDR
switch to the connected TV which had caused the 16:17 crash. The game remained
responsive. `CrimsonDesertTelemetry.overlay.log` recorded a second complete
replacement sequence:

```text
Game D3D12 swapchain created; waiting for graphics wrappers to finish.
Overlay ready: D3D12 / SDR.
```

The current game log contained no `CreateSwapChainForHwnd`, `E_ACCESSDENIED`, crash
or exception entry after the transition, and `/v1/ambient` continued returning
fresh data on port 27311. This is a passed one-way local reproduction test of the
runtime replacement crash. The reverse switch to the original output was requested
but had not been performed when the user asked for this handover. External RTX
4080/4090 startup acceptance remains separate and decisive for the Nexus reports.

## One next step

If desired, complete the local symmetry check by switching once back to the original
display and confirming another `Overlay ready` line without a game-log swapchain
error. Do not delay external validation for that check. Send the exact unchanged
2.1.11 ZIP and hashes above to at least one of the two affected users for three clean
starts with Overlay, Notifications, Lights and LightOverlay enabled. One
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
