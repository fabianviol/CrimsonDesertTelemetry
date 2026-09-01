# Changelog

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
