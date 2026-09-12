# Stable feature release — 2026-09-12

## Scope and status

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
| Managed build/tests and HTTP/WS | PASS: Release build, 69/69 managed tests and HTTP/WebSocket smoke on separate port27312. |
| ON preservation build/model | PASS: ON ASI/overlay targets build; overlay-model CTest passes with legacy controls retained. |
| Immutable ZIP / full payload equality | PASS: nine-file production payload, exact expanded/ZIP equality and profile/companion controls. |
| Packaged ASI bootstrap | PASS: isolated payload copy, all offered switches ON, only port changed to27312. Host starts from CFG companions; health/schema/smoothing respond. The smoke EXE is correctly rejected for native game hooks; this is not a game graphics-start pass. |
| Exact ASI and ZIP VirusTotal | Completed: ASI **5/70**, ZIP **0/67**. Same five detecting vendors as prior .4; Microsoft label changed from B!ml to C!ml. This is not a clean ASI verdict. |
| Local Windows Defender | Exact ASI and ZIP: no threats, exit0. Custom scans with remediation disabled; hashes unchanged. Signature1.459.170.0, last updated2026-09-12 02:59:22 CEST. This does not cancel the VT findings. |
| Nexus publication preflight | Read-only dry run PASS against existing file7891854; latest public version2.0.2. No upload, changelog mutation or release publication performed. |
| DMM clean deployment | Pending: user imports/enables, all six runtime files must match. |
| All-enabled game cold start/live | Pending on user's RTX 3080; no new compatibility claim yet. |
| F8/F9/F10, menu behavior, quit/restart | Pending user-operated checks. Automatic menu hiding is not implemented. |
| Live HDR / other GPUs / frame generation | Not validated. Automated coverage is not hardware/game acceptance. |

Ambient alone passed a controlled OFF v2.1.9 open/enclosed/open route. The new
candidate still needs its own live regression check. Per-light occlusion remains
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
Camera-sky wording/orientation and automatic menu visibility remain queued.

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

Next required step: user closes the game, disables the previous package and imports/
enables this ZIP through DMM. Verify all six deployed hashes before the first
all-enabled cold start. Then test progressing raw/EMA/ambient data, F8–F10,
menus and quit/restart. Do not publish while required live checks remain open.
Never replace an existing versioned archive.
