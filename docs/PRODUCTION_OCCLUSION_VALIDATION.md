# Production occlusion checkpoint — 2026-09-12

Ambient Occlusion passed the controlled live open/enclosed/open route in OFF
v2.1.9 on game build 25246367. Per-light production visibility is implemented
and synthetically tested; real visible/blocked/visible, behind-camera and frame
cost acceptance are still pending. This is not a final release.

## Current private test package

`CrimsonDesertTelemetry-v2.1.10-source-visibility.1-ModManagers.zip`

| Exact file | Bytes | SHA-256 |
| --- | ---: | --- |
| ASI | 1,438,208 | `A3722B05408464BA95BB6C1AFED1A1B27B9D6795A43E9311A37B4240DAD2AAE1` |
| ZIP | 875,062 | `4F364F7D4824D58F1FAF69914A9DABE50388178DC47678C8E2D467C908BAF988` |

Expanded folder: `artifacts/mod-manager/v2.1.10-source-visibility.1-20260912-133951-019-5457cc14/CrimsonDesertTelemetry`.
Both occlusion switches are enabled, research switches and legacy OcclusionTest
are off. HUD hiding starts off and can be toggled with F11; all four HUD shortcuts
are configurable/disableable. No files were installed by Codex.

OFF package build, nine-file validator and ZIP/staging equality passed. All 68
managed controls passed. Native suite: 24/26 pass; the two inherited failures in
research `spatial_readback.cpp` are documented in HANDOVER and remain uncommitted,
outside the OFF ASI. The acquisition fixture passes 218 WARP controls with no
GPU debug warnings/errors. Current UI tests cover fresh blocked hide/restore,
unknown/stale preservation, radar, remapping and disabled shortcuts. The final
F9 text/layout correction was checked again in five UI tests and saved images.
The preserved ON ASI also compiles; that is not research live acceptance.
GitHub CI exposed a pre-existing SDK8 overload ambiguity in the dense-pool test;
calling `Enumerable.Reverse` explicitly resolves it. The full solution and 68
tests pass under SDK8.0.425 as well as the normal local SDK. CI now selects OFF
explicitly; the test correction does not change the packaged binaries.

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
| Current `.1` ASI, `A3722B05...DAD2AAE1` | 5/70 |
| Current `.1` ZIP, `4F364F7D...08BAF988` | 0/68 |

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
Old packages are immutable. No GitHub/Nexus release has been created.

## Remaining work

- Controlled production fireplace visible/blocked/visible, plus behind-camera
  visibility and hide/restore. Preserve paired raw/smoothed streams with
  `scripts/Capture-LightStreams.ps1`; unknown/absent sources do not prove blocking.
- Continue evaluating the AV product/build regression without signature gaming.
- Diagnose the separate public 2.0.2 DMM/graphics report when logs, fault details,
  GPU/driver and DMM version are available; see [compatibility reports](COMPATIBILITY_ISSUES.md).
- Update final acceptance/package/scan evidence only for the exact files actually
  validated; do not equate code completion, a clean ZIP, or Ambient alone with DONE.
