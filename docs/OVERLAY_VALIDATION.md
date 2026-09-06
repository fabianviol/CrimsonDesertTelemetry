# In-game overlay validation

## Current: 1.3.0-preview.2 light visualization, 2026-09-06

Adds a compact oblique light radar to the corner HUD and an independently toggled
fullscreen world-marker layer in the same ASI. F8 HUD, F9 diagnostics, F10 markers.
Both views enabled in this preview's INI; missing configuration remains opt-in.
Filtered rendered records only, no scene-depth test, no invented persistent IDs.

Release build and six native overlay/client/notification test targets pass.
Real D3D12 fixture readback verifies projected markers, spot direction, behind/near
clipping, changing radar heights, independent and initially-hidden toggles,
stale/missing clearing, Present/Present1, both resize paths and 4K. The aimed-at
center label's collision gap was corrected after visual inspection. Small/4K
combined and marker-only images are in `build/light-overlay-*.bmp` (synthetic test
data, not game screenshots). Reader/projection also accepts the real A2 JSONL:
337 records, 178 in front of its recorded camera, 77 in the viewport.

**Next live check:** install through DMM with the game closed; verify marker
alignment at a known lamp, pan/turn/walk, then F8/F10 independently. Projection
uses latest published camera, not a frame-synchronous Present camera; motion
latency remains unmeasured. HDR/frame-generation/recording coverage remains open.
Light-source reconstruction/capture itself is unchanged from the tested preview.1.

## Historical: 1.0.0 release preparation

The user confirmed working in-game behavior after restarting with preview.7.
Version 1.0.0 keeps that camera backend and makes the HUD opt-in: `[Overlay] Enabled=0`
by default, including missing INI/section/key. The disabled path skips HUD workers,
graphics hooks, module pinning for the HUD, hotkeys and its WebSocket connection;
the telemetry bootstrap still runs independently. Configuration is read at startup,
not hot-reloaded. Existing customized `Enabled=1` settings are preserved.

Verification of the prior 0.1.0 package: all 30 managed checks and all three native CTest
targets passed. Native tests cover absent/disabled/opt-in/initially-hidden HUD
configuration, skipped graphics installation and no WebSocket connection while
disabled; opt-in rendering and networking still pass. The nine-file release ZIP
passed payload validation and a check for Server Enabled=1 / Overlay Enabled=0.
Final packaged in-game acceptance remains pending for v1.0.0.

## Historical preview.7 preparation

Preview.7 replaces the camera backend with the native engine source, including a
successful cold-start/direction test without upscaling. Packaged HUD acceptance
still needs a fresh in-game test. The prior preview.6 HUD was reported smooth by
the user with DLSS enabled, but its old camera source froze with upscaling off.

## Historical preview.6 preparation

Status: preview.4 appeared in the actual game, but the HUD was too small at 4K and
the user reported camera-arrow lag/backsteps. Preview.6 includes readability
improvements and a temporal camera-selection fix verified by production-code replay.
Complete actual-game / NVIDIA recording acceptance is **pending**. Do not describe
the graphics smoke test as an in-game test.
Light-source research is paused and is not compiled or packaged in this release.

## Implementation

- Dear ImGui 1.91.9b, MinHook 1.3.4 and JSON for Modern C++ 3.12.0 are pinned by
  official archive URL and SHA-256 in the native CMake project. They are statically
  linked; full notices are generated from their source licenses into the package.
- Uses the locally installed Segoe UI font, with embedded ProggyClean fallback.
  No Windows font file, game assets or memory dumps are distributed.
- Automatic resolution scaling uses render height relative to 1080p (2x at 2160p).
  The font atlas is baked at the matching size, not just enlarged as a bitmap.
  `[Overlay] AutoScale=0` disables this; `Scale` remains a user multiplier. The HUD
  is clamped to fit small windows and secondary text has increased contrast.
- An optional `[Overlay]` module in the existing ASI draws an English passive HUD.
  The host is still an external, read-only game-memory reader. The HUD **does hook
  DXGI code**; the previous blanket "ASI has no hooks" description no longer applies.
- Hooks factory CreateSwapChain/CreateSwapChainForHwnd, Present/Present1,
  ResizeBuffers/ResizeBuffers1 and SetColorSpace1. The actual D3D12 device and
  presentation queue come from swapchain creation, never from an arbitrary queue.
- Graphics resource initialization, font loading/upload and shader compilation run
  outside Present. Present only tries locks and skips busy GPU/upload-ring slots.
  Each submitted draw has a fence. Resize waits up to two seconds for owned GPU
  work; on timeout resources are retained, which can cause DXGI to reject that
  resize. A restart is then recommended instead of risking an in-flight release.
- Telemetry comes from `/v1/stream` on configured IPv4 loopback. Redirects/proxies
  are disabled. The dedicated WinHTTP worker parses bounded complete messages.
  Its blocking WebSocket receive never blocks rendering or graphics maintenance.
- The render thread reads the latest coherent snapshot without waiting for the
  network. Source timestamp plus local monotonic age controls staleness. Disconnect,
  malformed data and loading clear live values. No player direction is invented
  from camera direction, movement, or body animation.
- No WndProc replacement, input capture or game-memory modification. Foreground-only
  key-edge polling controls visibility/details. Config is read before launch.
- The graphics-enabled ASI is pinned for process lifetime. **No hot-unload**. All
  changes/install/uninstall require a closed game. Overlay-disabled startup creates
  no graphics hooks. Graphics smoke test executables are not part of the mod ZIP.

## Automated verification

Verified locally on 2026-08-30:

- Managed Release build: zero warnings/errors; all 22 checks passed, including nine
  new camera-selection checks. The 3600-sample production replay has 803 direction
  changes, zero backwards steps and zero unavailable samples.
- All three native CTest targets passed (model, D3D12, WebSocket).
- Hidden-window D3D12 output visually inspected; the scaled 3840x2160
  output is at `build/overlay-scaled-4k.png` (synthetic fixture, not the game).
- Expanded package and ZIP validator passed; exactly nine expected payload files.
- The preview.6 packaged ASI started the real host from an isolated path containing spaces on
  port 27329; health/schema responded. The host stopped when its loader exited.
  No loose JSON was created beside the ASI.
- Preview.6 ZIP SHA-256: `B90A97FAE2C965D4115385482758995D5E48A0E7A636776AEEB9682EBB4980F9`.
- Preview.6 ASI SHA-256: `161510E515A0F7F57A291D22E64CCF314C1B864EC466D1DC49002F7AFC879916`.

These are automated/local tests, not the manual in-game checklist below.

Run from the repository root after building the native targets:

```powershell
ctest --test-dir build/native-package -C Release --output-on-failure
```

`overlay-model` checks independent headings, vertical projection, unavailable
orientation, loading invalidation, timestamps/staleness, invalid schema/axes and
1080p/1440p/2160p/fixed-size/small-window scaling.

`overlay-d3d12` uses a **hidden** window with the Windows WARP D3D12 adapter. It
checks actual Present/Present1 hooks, upload/allocator reuse, hidden/test-only
presentation, both resize methods, recovery and a 3840x2160 resize/draw. An optional
first argument to `CrimsonDesertTelemetryGraphicsSmoke.exe` saves the initial
960x720 BMP; an optional second argument saves the 4K BMP. Its data
are explicitly synthetic test fixtures, not a Crimson Desert recording.

`overlay-websocket` uses a local ephemeral-port WebSocket fixture server to test
WinHTTP upgrade, fragmented JSON, loading, disconnect/reconnect and invalid data.

Also run the existing managed tests, bootstrap smoke test and package validator.
The manager payload now contains **nine files**, including THIRD-PARTY-NOTICES.txt,
and no loose JSON or EXE. This does not replace manager acceptance tests.

## Required manual acceptance

1. Close the game. Import preview.6 with DMM; enable/deploy it and launch normally.
   Ensure there is only one enabled telemetry ASI and preserve customized INI settings.
   Verify the deployed ASI hash: library import alone previously left the old game
   copy installed. Disabling/re-enabling the plugin with the game closed deployed it.
2. On the tested Steam build, load a save. Verify an English HUD and live values.
   Check the overlay log if absent; the ASI must load before swapchain creation.
3. Rotate only the camera: amber direction changes independently from cyan player root.
   Tilt up/down: pitch changes; near-vertical horizontal direction becomes unavailable.
4. Move/turn the character: position and cyan arrow respond. The cyan direction is
   the physics root, **not** the animated flying/rolling body pose.
5. Press F8 twice and F9 twice. Verify visibility/details and unchanged game controls.
6. Alt-tab, change resolution/window mode, load a save and teleport. Confirm recovery
   and no stale coordinates presented as live data. Compare frame timing HUD off/on.
7. Make a short NVIDIA **game** recording containing visible HUD, F8 hide and F8 show.
   Review the saved recording, not just the live screen. Frame-generation interaction
   still requires separate verification; successful SDR recording does not prove HDR.
8. Exit normally; verify the background host exits. Repeat launch. Then test disabling
   `[Overlay] Enabled=0`: telemetry works without any UI/render hooks.
9. Repeat install/deploy/start/disable/uninstall with JSON Mod Manager before claiming
   full JMM compatibility. Prior manager tests do not establish new HUD compatibility.

## Current limitations / release wording

- **D3D12, 8-bit SDR only**. HDR, alternate render APIs and multi-queue presentation
  are not supported by the first HUD renderer. Telemetry remains independent.
- Other overlays, DXGI wrappers and frame generation need live coexistence testing.
  A late-loaded or unrecognized swapchain fails without guessing its command queue.
- The camera-arrow fix passed offline tests and a recorded right turn; renewed
  in-game verification is pending. The HUD draws the received direction without
  smoothing. Low transport age is not proof of a current engine transform;
  renderer copies include historical states. See `CAMERA_LAYOUT.md`.
- Character switch, animated body pose and worldspace identifiers remain outside
  the currently validated telemetry contract. Light-source support is not included.
- Public release should remain a clearly labelled preview until the manual checklist
  is completed. Publish source, license notices and supported-build limitations together.
