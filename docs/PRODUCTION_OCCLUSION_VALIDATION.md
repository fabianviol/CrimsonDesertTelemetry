# Production occlusion checkpoint — 2026-09-12

Ambient Occlusion passed the controlled live open/enclosed/open route in OFF
v2.1.9 on game build 25246367. Per-light production visibility is implemented
and synthetically tested; real visible/blocked/visible, behind-camera and frame
cost acceptance are still pending. This is not a final release.

## Current private test package

`CrimsonDesertTelemetry-v2.1.10-source-visibility.3-ModManagers.zip`

| Exact file | Bytes | SHA-256 |
| --- | ---: | --- |
| ASI | 1,303,040 | `76B0D0B1DAFA15F233D94DAD92BE11FE4FD01DE2DDB20162423F314932E9605D` |
| ZIP | 811,399 | `C00F23DF90910C9ACF211A920A52B32F5E446BE40FFF9D1E2806F4F4B71D2E74` |

Expanded folder: `artifacts/mod-manager/v2.1.10-source-visibility.3-20260912-150632-956-f22a0e90/CrimsonDesertTelemetry`.
The new package fixes the negative-port host/HUD mismatch and separates the
production INI from research. All production feature switches are enabled,
including the private overrides ShowDetails=1 and HideOccluded=1. Numeric values
retain their normal bounded defaults. All four HUD shortcuts remain configurable
or disableable. No files were installed by Codex; old ZIPs remain intact.

OFF now ignores legacy Research/Console/Explorer/OcclusionTest configuration at
its entry points. Previously those keys could replace normal capture, activate
research startup or silently pass ignored research parameters into production.
The research template is preserved byte-for-byte for ON. This is a demonstrated
configuration correction, not an AV hypothesis or signature change.

OFF package build, nine-file validator and ZIP/staging equality passed. All 68
managed controls passed in the preceding schema checkpoint (managed runtime code
is unchanged here). Current native suite: 26/28 pass; the two inherited failures in
research `spatial_readback.cpp` are documented in HANDOVER and remain uncommitted,
outside the OFF ASI. The acquisition fixture passes 218 WARP controls with no
GPU debug warnings/errors. Current UI tests cover fresh blocked hide/restore,
unknown/stale preservation, radar, remapping and disabled shortcuts. The final
F9 text/layout correction was checked again in five UI tests and saved images.
The preserved ON ASI also compiles; that is not research live acceptance.
The ON overlay-model test passes too. Twenty port cases cover both boundaries
and invalid values under HUD-only, marker-only, notice-only and combined startup.
New combined graphics tests pass in SDR/scRGB at 720p/1080p/4K, with independent
HUD/marker toggles and persistent notices. The first two test-fixture defects
(missing worker maintenance after resize, then expired synthetic captures during
slow 4K CPU readback conversion) were corrected; no renderer fix was inferred.
The package validator covers all offered settings, valid nondefaults, ranges,
profile separation and companions. See [INI inventory and limits](INI_VALIDATION.md).
GitHub CI exposed a pre-existing SDK8 overload ambiguity in the dense-pool test;
calling `Enumerable.Reverse` explicitly resolves it. The full solution and 68
tests pass under SDK8.0.425 as well as the normal local SDK. CI now selects OFF
explicitly; the test correction does not change the packaged binaries.
The subsequent CI API test exposed an existing schema mismatch for unavailable
Ambient: the actual producer sends a null local estimate. The schema now permits
null only for unavailable and requires the estimate status object for available.
Both branches and invalid combinations are covered. All 68 managed tests and
the full HTTP/WebSocket smoke test pass on isolated port 27312; the running game
API on 27311 was untouched. This schema change updates the managed package, not
the ASI, RGB or producer behavior.

## Exact-file VirusTotal results

Scans were requested for each private candidate and the immediate predecessor.
The original 0/75 result is historical and was not reproduced for the current
predecessor. Counts below are malicious/suspicious detections over the responding
scanner categories, excluding unsupported formats. They do not establish cause.

| File | Result |
| --- | --- |
| Immediate predecessor v2.1.9 ASI, `2B9BAE0031E96228...` | 4/71 |
| First candidate v2.1.10 ASI, `45720A18...18CD4E5` | 5/70 |
| First candidate ZIP, `2BEAE903...6150BD5` | 0/68 |
| `.1` / `.2` identical ASI, `A3722B05...DAD2AAE1` | 5/70 |
| Preserved `.1` ZIP, `4F364F7D...08BAF988` | 0/68 |
| Preserved `.2` ZIP, `A3E95666...D7B4D808` | 0/68 |
| Current `.3` ASI, `76B0D0B1...32E9605D` | 5/70 |
| Current `.3` ZIP, `C00F23DF...B71D2E74` | 0/67 |

The immediate predecessor is flagged by CrowdStrike, Cynet, McAfeeD and Microsoft;
the current candidate additionally by Bkav. The current Microsoft result is
`Trojan:Win32/Wacatac.B!ml`. There is no demonstrated causal attribution to a
feature and no AV-driven obfuscation, signature tuning or feature removal.
The historical clean baseline is not restored; final release clearance remains
open. A clean ZIP verdict does not override the embedded ASI result.

Limited product review of the exact `.1` ASI versus v2.1.9: +44,032 bytes, identical
imported DLLs/functions, no new import surface. OFF sources exclude general
`spatial_probe.cpp`/`spatial_readback.cpp`; new SDF acquisition uses three necessary
barrier/lifecycle callbacks, one pending readback and one current volume, without
capture history or dump files. Existing optional console components remain common
to the ASI. This review does not prove that scanners are wrong or establish a
reason for their verdicts. The comparison tool's historical `clean` argument label
does not make the 4/71 predecessor clean.

Raw VT responses, exact hashes, native CTest results and synthetic UI images are
preserved under `artifacts/validation/v2.1.10-source-visibility-20260912/`.
The `.3` package/profile/full-native-test logs, exact hashes and both full VT
responses are in `artifacts/validation/ini-profiles-20260912/`.
Old packages are immutable. No GitHub/Nexus release has been created.

## Remaining work

- Controlled production fireplace visible/blocked/visible, plus behind-camera
  visibility and hide/restore. Preserve paired raw/smoothed streams with
  `scripts/Capture-LightStreams.ps1`; unknown/absent sources do not prove blocking.
- Continue evaluating the AV product/build regression without signature gaming.
- Test each supported INI function and the all-production-functions configuration
  live. Boundaries, dependencies and known research exclusions are recorded in
  INI_VALIDATION; synthetic parsing/graphics checks do not complete this matrix.
- Establish a reliable menu-state signal before implementing automatic HUD/marker
  hiding in all menus. Current `playing/loading` describes data availability only.
- Diagnose the separate public 2.0.2 DMM/graphics report when logs, fault details,
  GPU/driver and DMM version are available; see [compatibility reports](COMPATIBILITY_ISSUES.md).
- Update final acceptance/package/scan evidence only for the exact files actually
  validated; do not equate code completion, a clean ZIP, or Ambient alone with DONE.
