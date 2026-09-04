# Changelog

## 1.2.1-preview.2 - 2026-09-04

- Stop dropping player orientation while the player moves fast. The player global
  and the physics object are written at different points within a frame, so a
  sample taken between those writes sees them disagree by one frame of travel.
  Measured while riding: 3.6% of samples exceeded the previous 0.15-unit
  agreement tolerance, peaking at 0.28 units, which showed up as the orientation
  flickering out several times a second. The tolerance is an identity check, not a
  freshness check, and now says so: two units still rejects any unrelated object
  by a wide margin, while an orientation that is one frame old is not worth
  discarding.
- Read the player position and orientation through one `ReadPose` call so the two
  cannot drift apart between call sites. An immediate re-read on rejection was
  tried and removed: measured against eighteen rejections it rescued none, because
  the window between the two writes is wider than a retry takes.


## 1.2.1-preview.1 - 2026-09-04

- Correct the player position for Steam build `25116796`. The globals used since
  1.2.0 hold the renderer camera position, not the player's; they were bit-identical
  to `camera.position` and about 6.5 units from the player. The genuine player
  global was found by searching the executable for its structural shape (an
  8-byte RIP-relative store paired with a 4-byte store into the same global plus
  eight) instead of an exact byte pattern, which no longer matched after the
  update. Exactly one candidate is chunk-aligned to the physics root; it was
  live-validated over 27 units of player movement.
- Withdraw the SQT player-transform claim from 1.2.0. `owner+0xE08` resolves to a
  zeroed object holding neither a position nor a rotation. The genuine transform
  is the unchanged `basis-v1` physics layout; only the physics link moved, from
  `owner+0x298` to `owner+0x2B8`. Confirmed by the unchanged physics-update code
  pattern, its callers, an execute-breakpoint capture and a live turning test.
- Publish player orientation again for `25116796`. Verified live: over 22 samples
  the player heading swept 355 degrees while the camera held still for seven of
  them, so the two are demonstrably independent sources, and the root basis
  stayed upright in every sample.
- Report why an orientation is missing instead of returning a silent null. The
  chain names the failing hop and its offset, RTTI mismatches report expected and
  actual type, and quaternion or position rejections include the values. `snapshot`
  writes the reason to stderr, leaving JSON on stdout unchanged.


## 1.2.0 - 2026-09-04

- Add opt-in, read-only nearby engine-light telemetry for the exactly validated
  `25050808` and `25116796` executables. The module reads the CPU source array, re-resolves and
  double-checks its complete pointer walk per snapshot, retries once, and fails closed.
- Publish verified position, point/spot kind when known, linear color, record/renderer
  flags, and conditional renderer scale/final linear RGB. Do not claim range, lumens,
  generic enabled state, direction, fire/effect lights, or durable source identities.
- Keep schema 1.1 and its payload unchanged while lights are disabled; opt-in light
  snapshots use additive schema 1.2 and separate diagnostic counters.
- Restore exact player, orientation, camera and light support after Steam build
  `25116796` / EXE `1.0.0.2760`, using the newly validated direct scene/camera
  root and SQT player transform. Unknown builds continue to fail closed.

## 1.1.0 - 2026-09-01

- Keep exact SHA-256 matches as explicitly tested builds, while allowing a different
  game EXE only when the complete reference layout can be relocated unambiguously.
- Resolve and validate player/static globals, both camera globals and two independent
  multi-slot object-table fingerprints from the executable. Retain all live pointer,
  RTTI, basis, projection, coherency, freshness and proximity checks; failures remain closed.
- Add health `compatibility` metadata that distinguishes `tested` from `automatic`,
  reports the actual EXE identity and names the manually validated reference layout.
- Add the offline `check-compatibility <exe>` diagnostic and synthetic regression
  coverage for relocation, ambiguity, malformed images and invalid section/target classes.
- Steam build `25050808` / EXE `1.0.0.2692` was accepted automatically from the
  original reference layout, then locally validated with a fresh launch and controlled
  independent camera/player movement; it is now also an exact known build.

## 1.0.0 - 2026-08-31

- First public release package: player position/root orientation, native render-camera
  position/basis/projection, loopback HTTP/WebSocket API and JSON Lines output.
- Product version 1.0.0 retains the existing HTTP v1 endpoints and JSON schema 1.1.
  No payload or endpoint migration is required for consumers of the development builds.
- Native camera reads now use guarded game globals, independent of DLSS/Streamline
  camera copies. Cold-start/upscaling-off and in-game restart tests passed on the
  development NVIDIA setup; other GPU hardware remains untested.
- Dear ImGui HUD is opt-in: `[Overlay] Enabled=0` by default, including when the
  INI/section/key is missing. No HUD hooks, hotkeys or HUD client start in this mode.
  `[Server] Enabled=1` remains the default; telemetry runs without the HUD.
- Set `[Overlay] Enabled=1` and restart to enable the HUD. `InitiallyVisible` and
  F8 control visibility only after opt-in. Existing customized INIs are not overridden.
- Light-source research is excluded. Unknown game builds are rejected. The optional
  HUD supports D3D12 / 8-bit SDR; HDR, frame generation and recording remain unverified.

### Development preview history (superseded where noted above)

- Preview.6: replace majority-only tracking with temporal activity selection over
  renderer copies, keeping the newest observed update among regularly changing
  sources. Retain learned sources during stillness; rediscover if they are lost.
- Preserve raw camera turns, reversals, position and projection. No display smoothing
  or fixed process addresses. Quality copy counts describe the selected state.
- Add nine targeted selection checks and optional production-code replay. The local
  controlled 3600-sample right-turn trace yields 803 direction changes without
  backwards steps or unavailable samples; renewed in-game acceptance remains pending.

- Preview.5: resolution-aware HUD size, matching font-atlas resolution, stronger
  secondary text contrast and configurable `AutoScale` (enabled by default).
- Add automatic 4K/resize graphics coverage and HUD scale calculation tests.
- Add a read-only stream recorder for controlled camera-lag investigations.
- Document Streamline provenance and unverified GPU/DLSS coverage.

- Preview.4 candidate: optional English Dear ImGui in-game HUD (D3D12 / 8-bit SDR),
  independent camera/player-root directions, position, FOV and diagnostics.
- Add configurable passive HUD hotkeys, source-age validation and unavailable-data states.
- Add pinned native dependencies and bundled third-party license notices.
- Add native parser tests and an isolated, hidden-window D3D12 graphics smoke test.
- Light-source research remains excluded; the HUD is visible in-game, but complete
  in-game/recording acceptance is pending.

- Add schema 1.1 player physics-root orientation (`forward`, `up`, optional heading) with per-frame availability semantics.
- Serialize concurrent ASI bootstrap startup so duplicate loader instances cannot launch competing telemetry hosts.

- Added a preview ASI bootstrap and mod-manager packaging for the existing read-only host.
- Fixed DMM misclassifying .NET dependency/runtime JSON as mod patches: the manager
  package now uses .cfg companions selected explicitly by dotnet exec.
- Added exact package-content and embedded-resource regression checks.
- Added read-only support for Crimson Desert Steam build `24994088`.
- Added version-bound static player-position discovery.
- Added address-independent render-camera structural discovery.
- Added redundant-copy consensus and automatic rediscovery.
- Added the versioned community JSON contract and JSON Lines CLI output.
- Added loopback HTTP snapshot/health/schema endpoints and a WebSocket stream.
- Added configurable 1–240 Hz shared sampling and automatic game-process reconnects.
- Classified the observed origin/height-1000 startup sentinel family, including floating-point noise,
  as loading state.
- Added offline validation and local API integration tests.
