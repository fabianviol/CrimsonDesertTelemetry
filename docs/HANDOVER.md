# Current checkpoint — production occlusion, 2026-09-12, Codex

## Mandatory product goal and working rules

Production `CDT_RESEARCH=OFF` is complete only after BOTH controlled live tests
pass on supported build 25246367:

1. Local Ambient / Sky Occlusion: open -> enclosed -> open, with a clear and
   reversible difference under a solid roof/in a building/cave.
2. Per-light Source Occlusion: camera/player -> individual fire/lamp, visible ->
   blocked by geometry -> visible. Include off-screen/behind-camera coverage;
   screen-space visibility alone is insufficient.

Preserve raw and smoothed light records. Visibility is additional metadata, never
silent removal/filtering. Do not touch CrimsonHue until both goals pass. Reuse the
preserved SDF findings; no new PIX/renderer or AV research without a concrete need.
Keep production minimal; research instrumentation/history stays in the ON path.
Scan the EXACT final production ASI and release ZIP before release. A significant
new AV regression blocks release; investigate the product/build, never obfuscate
or game signatures. The user's earlier 0/75 result is a historical baseline, not
an assurance about a new binary.

**User controls the game and installs all packages through DMM. No Computer Use,
no direct installation or edits in the game directory.** Prepare configured ZIPs
and give one short, concrete manual step at a time.

## Immediate result — input defects fixed, live acceptance pending

Read Gemini's handover, DecodeReference, current v2.1.8 diagnostic state and log,
then acquisition in the requested order. The diagnostic at 11:06:34 CEST showed:

`L1 Y: wrapped=4000, relative=1, scale=1, origin=1000, cell=4001, bounds=[969,1031)`.

This did NOT prove an update-induced offset shift. The first defect was the
producer's input: it captured only 256 GI bytes and constructed
`gi[:256] + gi[:256] + scene[:512]`. The decoder needs ONE contiguous 768-byte GI
block at `owner+0x20`. Before/after GI snapshots are compared, not concatenated.

| Decoder field | Required bytes | v2.1.8 actually supplied |
| --- | --- | --- |
| inverse 0x10 | GI 0x10 | GI 0x10 |
| wrapped 0x130 | GI 0x130 | GI 0x30 |
| L1 origin/scale 0x150 | GI 0x150 | GI 0x50 |
| L1 relative 0x250 | GI 0x250 | scene 0x50 |
| UV 0x2E0 | GI 0x2E0 | scene 0xE0 |

Independent replay of 47 stable CPU contexts from 12 preserved capture folders:
full GI selects clipmap 1 in all; the v2.1.8 merge fails in all. This establishes
CPU layout, not GPU pairing; some source runs had rejected GPU pairing.

`spatial_acquire.cpp` now retains the complete `ConstantBytes` CPU block through
submission completion and passes it directly to the sampler. Removed the unused
scene snapshot and obsolete GPU GI-buffer resolution/copy/allocation tail. Scene
is still used separately to obtain the frame number. All decoder offsets/math
are unchanged; no offset was guessed and no synchronization research was reopened.

A second concrete input-layout defect was found before asking for a live test:
the mapped 64x32x264 R8 volume has 256-byte rows, but production sampled with a
64-byte stride. The sampler now accepts row pitch and samples mapped rows directly,
without another volume buffer. Packed research callers keep their 64-byte default.
The temporary absolute-path logger is removed and the existing null-input guard
is restored. Original diagnostic source/log and all inherited changes are preserved
under `artifacts/recovery/codex-ambient-takeover-20260912-111004/`.

## Verification and evidence

- OFF package build and package validator passed. No native compiler errors;
  CMake reports the existing third-party MinHook deprecation warning.
- 37 native sampler controls pass, including malformed GI assembly, padded XYZ
  data, poisoned padding, negative wrap, shifted world position and invalid stride.
- `Verify-NativeSampler.py`: 72 exact comparisons across nine preserved captures,
  zero mismatches. These are historical/offline results, NOT new live acceptance.
- Full CTest: 23/25 pass. Two RESEARCH-source tests fail on inherited edits in
  `spatial_readback.cpp`: `Records`/`LatestRecord`/retention were disabled before
  takeover. Failures: `spatial-texture-readback` check 37 and
  `spatial-paired-readback` check 281. That source is excluded from the OFF ASI;
  its uncommitted changes are preserved, not silently overwritten or included in
  this fix. Do not call the full suite green.
- Current diagnostic live API control before fix progressed sequence 24450 ->
  24527 and sky captures 935 -> 937; visibility stayed null throughout.
  Saved at `artifacts/light-research/ambient-input-fix-20260912/` alongside log.
- Current game EXE BCBF623AD5690147DC462AEAED5B4F97BD73296BA0D6AB54663586E7088B1C0E
  is already preserved at `artifacts/recovery/20260911-build-25246367/`.

## Package for the next controlled test (not a final release)

`artifacts/mod-manager/CrimsonDesertTelemetry-v2.1.9-ambient-input-fix-ModManagers.zip`

- ZIP SHA256 `69AB40274750A8B104677F5B520EC3465E5C525487D561009B81D53AC83E179F`.
- ASI SHA256 `2B9BAE0031E962280A92874B318F97E4D8758489C7DC287C08C162F4574F8F17`.
- ASI 1,394,176 bytes; ZIP 846,788 bytes. `CDT_RESEARCH=OFF`, build 25246367.
- INI ready: Ambient.Enabled=1, Overlay.ShowAmbient=1; SpatialProbe,
  SpatialReadback, SignedDistanceReadback, AmbientProbe and OcclusionTest all 0.
- Package/ZIP equality passed; existing v2.1.8 ZIP unchanged
  (`DEAD49F6FD561833E0E929662E0D1BFDC89A1F24142549C1B1C9B41E61D5F2DE`).
- Not installed by Codex, not publicly uploaded, no new AV scan yet.

## Live A-B-A in progress — user installed v2.1.9 through DMM

Game PID 31832, started 12:30:33 CEST. Read-only hashes verify ASI, both DLLs,
INI and runtimeconfig against v2.1.9. DMM retained v2.1.8 `deps.cfg`; only version
labels differ, with identical runtime target, DLL asset names and dependency
structure. Record this discrepancy; full installation equality is not claimed.
The running new native/managed code now supplies valid, fresh visibility.

Outside phase recorded at 12:48 CEST:
`artifacts/light-research/ambient-input-fix-20260912/live-aba-20260912-124843/01-outside.json`.
User screenshot saved beside it. 16/16 fresh samples, 16 distinct visibility
frames, age <=31 ms, API sequence 59727 -> 60190. Visibility 0.24005465..0.38915721,
mean 0.33465750. Player unchanged at (-11407.612,665.6101,-4225.9883); camera
(-11405.418, approximately 667.83,-4228.837), with <0.006 vertical idle movement.
This is an outdoor reference near the building, not a full acceptance result.

Covered doorway phase saved as `02-covered-doorway.json` plus user screenshot in
the same live-aba folder. Same PID 31832, 16/16 fresh samples/16 distinct visibility
frames, age <=16 ms, sequence 77904 -> 78368. Visibility 0.00578101..0.00736786,
mean 0.00668928 (about 98% below the outside mean). Player stationary at
(-11407.994,665.5804,-4219.4927), camera approximately (-11404.833,668.522,-4219.8784).
The covered and outside windows are clearly separated; reversal is still pending.

**Next:** user goes fully inside, stops with camera inside, and says ready.
Record `03-inside.json`, then ask for the original outside position/view and record
return-open. The current signal samples CAMERA location, so distinguish camera
coverage from only the player's body crossing under the roof. Read APIs only;
user operates game and provides scene context. Do not mark Ambient complete until
the enclosed and return-open phases establish the controlled live reversal.

User additionally confirms the HUD must include per-light visibility. Current
OFF package still shows raw light markers without per-light occlusion; add that
HUD presentation with the second product goal, without dropping original API data.

After Ambient passes, proceed directly to per-light geometry visibility. The OFF
build currently links `cdt_sdf_disabled`; research SDF/HUD tests passing is not
production per-light completion. Relevant preserved implementation is
`src/sdf_visibility.cpp` and research acquisition in `src/spatial_probe.cpp`.
Neither mandatory live acceptance nor final release readiness is established yet.
