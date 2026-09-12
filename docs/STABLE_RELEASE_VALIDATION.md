# Stable feature release — 2026-09-12

## Final 2.1.10 package and current gate

Final production package prepared: **2.1.10**, `CDT_RESEARCH=OFF`.
ZIP: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.10-ModManagers.zip`,
791,357 bytes, SHA-256
`1A8DD7641E9431AA0FE383E68706D9519A0B93E2ED5E6A70129C4C4552DEBD24`.
Expanded payload:
`artifacts/mod-manager/v2.1.10-20260912-204830-358-a7c66c8d/CrimsonDesertTelemetry/`.
ASI:1,267,712bytes, SHA-256
`8028E9EE43E846F79075618F9B7A522F5F66E2EB1784FFB48AF178AAFC0C0A78`.

After NVIDIA driver32.0.16.1692 installation, the first rc.1 start rebuilt shaders
and capture stopped with Windows error258/WAIT_TIMEOUT during the long initial
load. A later no-mod control start also launched an in-game shader compilation,
showing that the rebuild trigger is independent of Telemetry. The second modded
start did not reproduce the capture failure. Final2.1.10 waits for the host's
first valid player/render-camera `playing` state before arming native light/sky
capture. Recorded-but-not-submitted work now has a separate60-second bound; the
post-submit GPU fence retains5seconds. The timeout stage is logged explicitly.
The named-event gate is covered by native and managed controls.

Final automated result:70/70 managed controls and27/27 selected native controls
PASS. Package validation confirms the31-setting production profile, nine exact
files and expanded/ZIP equality. Exact final VirusTotal: ASI **5/71**, the same
five detecting vendors as rc.2 and no new vendor; ZIP **0/66**. Local Defender
custom scans return0 for both exact files with remediation disabled and unchanged
hashes. The final package still requires one user-run DMM/game acceptance before
Nexus publication.

## Historical rc.1 scope and acceptance

Candidate built: **2.1.10-rc.1**, production **CDT_RESEARCH=OFF**.
Not published or accepted for release yet. The user paused occlusion work to
prioritize a stable package of working features. The longer-term ambient and
per-light goals remain preserved in [HANDOVER](HANDOVER.md).

Included: raw/authored/rendered lights, grouping/EMA, player/render camera,
ambient, HUD/radar, fullscreen markers, notifications and configurable/disabled
F8–F10 keys. The 31-setting INI enables every offered boolean feature, including
ShowDetails. Numeric defaults remain bounded; all enabled does not mean every
possible numeric combination or maximum load has passed.

Excluded: per-light SourceVisibility acquisition/tracing, HideOccluded and F11.
Live concealed-source tests failed; removing those controls reflects failed
functionality, not an AV hypothesis. OFF omits sdf_acquire.cpp and links an
unavailable SDF implementation. Old diagnostic INIs cannot activate those paths.
Raw/EMA records remain intact; additive metadata is unknown/disabled with no
attenuation. Research sources and the original 60-setting ON template are preserved.

## Current validation

Evidence: `artifacts/validation/stable-release-20260912/` (local, not committed).

| Check | Result / limit |
| --- | --- |
| OFF native Release build | PASS, including release-source-disabled control. |
| Selected native CTests | 27/27 PASS; all UI owners together in SDR/scRGB, model, acquisition, guard, client and compositor checks. Software GPU controls, not a live game pass. |
| Research-only CTest exclusions | Existing spatial-texture-readback and spatial-paired-readback failures excluded from this run; no change or pass claim for those paths. They are not linked into the OFF product. |
| INI/profile validation | PASS: 31 production / 60 research keys; source/HideOccluded/F11 rejected in OFF; range and companion negative controls. |
| Legacy source switches | PASS: forcing enable cannot activate tracing, publish a volume or alter raw scene/light bytes; metadata remains disabled. |
| Managed build/tests and HTTP/WS | rc.1 PASS: Release build, 69/69 managed tests and HTTP/WebSocket smoke on separate port27312. Final2.1.10:70/70 after adding the playable-world signal control. |
| ON preservation build/model | PASS: ON ASI/overlay targets build; overlay-model CTest passes with legacy controls retained. |
| Immutable ZIP / full payload equality | PASS: nine-file production payload, exact expanded/ZIP equality and profile/companion controls. |
| Packaged ASI bootstrap | PASS: isolated payload copy, all offered switches ON, only port changed to27312. Host starts from CFG companions; health/schema/smoothing respond. The smoke EXE is correctly rejected for native game hooks; this is not a game graphics-start pass. |
| Exact ASI and ZIP VirusTotal | Completed: ASI **5/70**, ZIP **0/67**. Same five detecting vendors as prior .4; Microsoft label changed from B!ml to C!ml. This is not a clean ASI verdict. |
| Local Windows Defender | Exact ASI and ZIP: no threats, exit0. Custom scans with remediation disabled; hashes unchanged. Signature1.459.170.0, last updated2026-09-12 02:59:22 CEST. This does not cancel the VT findings. |
| Nexus publication preflight | Read-only dry run PASS against existing file7891854; latest public version2.0.2. No upload, changelog mutation or release publication performed. |
| DMM deployment | All six runtime files present. Five exact matches; deps.cfg retains2.1.8 version labels with identical runtime assets. Exact metadata replacement remains open; do not call this an exact six-file match. |
| All-enabled game cold start/live | First local SDR start/live PASS: PID7260 since19:26:27 CEST, supported build25246367, all offered INI feature switches1. RTX3080. Two30-second capture windows have no unavailable/empty light frames; details below. This does not resolve the external crash report. |
| F8/F9/F10 and menus | User confirms twice toggling each key and menu entry/exit work without flicker/crash. Automatic menu hiding explicitly cancelled by the user; do not pursue. |
| New Ambient route | PASS: user-labelled8-second open/enclosed/open windows, mean0.477526 ->0 ->0.490643,1442/1442 available samples; details below. |
| Quit and DMM deletion | User confirms exiting/deleting package. Game/host absent, port27311 free. ASI/INI/logs removed; both DLLs and CFGs remain. No orphan host observed; this is not complete file cleanup. |
| Reimport in DMM1.9.4 | Reproduced: library6/6 correct, bin64 still5/6 exact; deps.cfg stays2.1.8. Second game start deferred while testing current DMM. |
| Live HDR / other GPUs / frame generation | Not validated. Automated coverage is not hardware/game acceptance. |

Ambient passed a controlled OFF v2.1.9 route and the current candidate's new
open/enclosed/open regression. Per-light occlusion remains
unfinished and is not part of this scoped release.

## Deployment and external report

The external Nexus 2.0.2 graphics crash is unresolved. A local game pass cannot
prove it fixed on the reporter's system. Missing companion deployment and the
subsequent graphics crash are separate observations; see
[compatibility issues](COMPATIBILITY_ISSUES.md).

Local portable DMM executable reports 1.9.4. Earlier DMM installs retained old
deps.cfg version labels although runtime assets matched. For this candidate,
verify exact hashes for ASI, Core DLL, host DLL, INI and both CFG companions
before accepting installation; do not infer deployment success from ZIP validation.

## Preserved unfinished work

Occlusion checkpoint is committed as 2579399. Uncommitted inherited work was
backed up under `artifacts/recovery/occlusion-pause-20260912-165120/`.
The labelled room sources, blocked/free paired volumes, trace comparisons and
limits are in [SOURCE_VISIBILITY_REGRESSION](SOURCE_VISIBILITY_REGRESSION.md).
The return-to-blocked step was cancelled; no completed ABA test is claimed.
Camera-sky wording/orientation remains queued. Automatic menu visibility was
explicitly cancelled by the user during this run and must not be pursued further.

## First candidate game run

Game PID7260, start2026-09-12 19:26:27 CEST; ASI-managed host PID37428 has that
game as parent. The loaded ASI path and current file hash match this candidate.
SDR overlay/native startup report ready with no subsequent errors in captured logs.
Bootstrap also logged one suppressed duplicate-launch attempt. Current module/
process enumeration finds one telemetry ASI and one host, not two active hosts;
the initiating duplicate attempt was not identified and is not assigned as a
crash cause.

Evidence folders: `live-all-enabled-01/`, `streams-all-enabled-01/` and
`streams-all-enabled-controls-01/` under the validation directory. Raw logs/JSONL,
installed INI/deps metadata and a reusable read-only analysis script are preserved.

| 30-second window | Raw available | Smoothed available | Ambient available | Unique light captures |
| --- | --- | --- | --- | --- |
| First | 1799/1799 | 1800/1800 | 1800/1800 | 446 |
| Second | 1797/1797 | 1797/1797 | 1797/1797 | 441 |

Both windows:9–13 raw lights, zero empty available light frames, zero malformed
records, zero sequence regressions. Maximum light age110ms; maximum publication
gap below36ms. All33816 compared smoothed contributions exactly match original
raw records from the same capture; none missing and no raw-sum errors.
Source metadata is consistently unknown/disabled with null attenuation.
Each ambient window advances60 sky captures; maximum sky age547ms, visibility
age47ms, no unavailable/stale local estimates. No world-space occlusion claim
follows from this availability control.

The retained deps.cfg becomes structurally identical after substituting only
2.1.8-diagnostic-final/2.1.8.0 version labels with2.1.10-rc.1/2.1.10.0. Actual
dependency/runtime asset sets are unchanged, and the new host is running. This
explains the scope of the mismatch, not why DMM retained it or the Nexus crash.
All six files in DMM's own library match the candidate ZIP, so import is complete;
the version-label mismatch is in the subsequent bin64 deployment.

## Candidate Ambient route and shutdown

The user confirmed each phase before its8-second capture; raw/smoothed lights and
ambient were collected together. No game input or Computer Use was performed.
Evidence: `ambient-route-open-01/`, `ambient-route-enclosed-01/`,
`ambient-route-open-return-01/` and `ambient-route-complete.json`.

| User-labelled phase | Mean exposure | Available ambient | New sky captures | Max visibility age |
| --- | --- | --- | --- | --- |
| Open | 0.477526 | 480/480 | 16 | 47ms |
| Enclosed, camera indoors | 0 | 481/481 | 16 | 32ms |
| Open return | 0.490643 | 481/481 | 16 | 47ms |

Visibility frames progress in every phase, including the zero-valued enclosed
phase. No stale/unavailable local estimates or lost raw/smoothed availability.
Return mean is2.75% above the first open mean; return camera differs by about2.5
game units, so this is a reversible environment route, not an identical-pose or
orientation-invariance test. The separate camera-orientation complaint stays open.

User-confirmed normal exit and DMM package deletion were followed by absence of
PID7260/host37428 and no port27311 listener at19:43:05 CEST. The observer started
after process exit, so no exact cleanup latency is claimed. DMM leaves both DLLs
and both CFG companions in bin64. With ASI absent, no plugin/host is active;
full file cleanup and metadata update are separate deployment concerns.

The user reimported and activated the same ZIP with the game closed. The library
again matches all six files, while bin64 retains the old deps.cfg. Thus deleting
and reimporting with1.9.4 does not resolve the mismatch. User explicitly confirms
DMM's UI version1.9.4 and expects all owned runtime files to be removed.

After updating to DMM2.8.1, deletion still left both Telemetry DLLs and both CFGs;
reimport retained the older deps.cfg. The user removed those four owned leftovers
with the game closed. A subsequent clean DMM2.8.1 import deployed all six rc.1
runtime files with exact hashes. Release instructions preserve this manual upgrade
cleanup instead of claiming the manager now removes/replaces every owned file.

## Exact candidate files

ZIP: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.10-rc.1-ModManagers.zip`.
Expanded payload: `artifacts/mod-manager/v2.1.10-rc.1-20260912-171147-979-5c7eb161/CrimsonDesertTelemetry/`.

| File | SHA-256 |
| --- | --- |
| CrimsonDesertTelemetry.asi (1,267,200 bytes) | `54DE954F07ADF0D5FC6C9C408E2B07C63C5D51D744B906D68CC069C5BFBB849C` |
| Release ZIP (790,008 bytes) | `C478BD1C54B8916E294BE1E1EDAFE6469C7051AFE9585443622A79152DE1B7E1` |

Raw scan reports, Defender outputs and all nine payload hashes are preserved in
the evidence directory above. VT detections:

| Vendor | ASI finding |
| --- | --- |
| Bkav | W32.Malware.B4442FF5 |
| CrowdStrike | win/malicious_confidence_60% (D) |
| Cynet | Malicious (score: 100) |
| McAfeeD | ti!54DE954F07AD |
| Microsoft | Trojan:Win32/Wacatac.C!ml |

[ASI report](https://www.virustotal.com/gui/file/54de954f07adf0d5fc6c9c408e2b07c63c5d51d744b906d68cc069c5bfbb849c)
and [ZIP report](https://www.virustotal.com/gui/file/c478bd1c54b8916e294be1e1edafe6469c7051afe9585443622a79152de1b7e1).
No new detecting vendor versus .4 is observed. Equal counts do not establish
benignness; no AV attribution, obfuscation or signature-oriented rebuild was made.
The clean ZIP/local Defender checks must not be represented as a clean VT ASI.

Next required step: verify updated DMM version, repeat owned-file removal/import
and exact payload comparison, then second cold start and bounded live stream check.
Do not publish while required live checks remain open.
Never replace an existing versioned archive.
