# Current checkpoint — light HUD, 2026-09-06, Codex/Astra

## Result / one next step

**1.3.0-preview.2 is built, tested and ready for DMM installation.** It adds the
requested 3D corner radar and independently toggled fullscreen light markers
to the existing single ASI. The game was confirmed closed during preparation;
nothing was installed or changed in the game directory.

Next: user installs the new ZIP through DMM, replacing/deactivating preview.1,
then loads near known lamps. Visually check stationary marker alignment, turn/
pitch/walk and independent F8/F10 toggles. **This visualization has not yet been
validated in the real game.** Do not confuse its GPU fixture tests with live proof.

## Package / controls

Immutable ZIP:
`artifacts/mod-manager/CrimsonDesertTelemetry-v1.3.0-preview.2-ModManagers.zip`
SHA256 `861EDB458198BC2707FC2B306E4CC5AE896B5BB32B485700641D5A952FF3DC4C`.
Expanded:
`artifacts/mod-manager/v1.3.0-preview.2-20260906-202637-330-d5111e42/CrimsonDesertTelemetry`.
ASI SHA256 `2B6949AA85E4305A127BC1BFAAFC2F202425CB26EA5572D1D51151B1D440B714`.
Previous preview.1 ZIP remains unchanged for rollback. Never overwrite releases,
replace the ASI loader/other mods, or install directly instead of the user's DMM.

- F8: corner HUD; F9: diagnostics; F10: fullscreen light markers.
- Package enables both views. `[Overlay] Radar3D=0` restores the old compass.
- `[LightOverlay]`: Enabled=1, InitiallyVisible=1, ToggleKey=121, Radius=35,
  MaxMarkers=512, MaxLabels=6. Configurable bounds: 2048 markers / 16 labels;
  label placement examines at most 64 candidates. Units are game units, not metres.
- Missing config defaults UI modules off. Overlay/LightOverlay/Notifications must
  all be disabled to skip UI hooks/client. HUD-hidden does not stop light capture.
- Existing notices and single ASI/host/config architecture remain. Console/explorer
  stay disabled. User wants DMM; settings require restart, no hot-unload.

## Implementation / limits

`native/CrimsonDesertTelemetry.Asi/src/overlay_*.{h,cpp}`:
strict bounded rendered-record parsing; immutable shared record storage avoids
copying arrays every Present. Optional invalid/missing feeds clear records, not
core telemetry. Render freshness includes producer age, transport, parsing and
time since receipt, capped at 500ms. No historical markers kept as live.

Corner HUD retains XYZ/root/camera numbers; oblique player-centered radar adds
colored contributions, schematic height stems, root/camera yaw and view guide.
Fullscreen rings/labels show measured XYZ, linear HDR RGB/luminance, distance,
sample-local index and spot cone/direction when available. Center-priority labels
avoid HUD/reticle/other rings. Fixed-length arrows are schematic, not light range.

Only current **filtered rendered** records are used; do not sum authored+rendered.
Radar can show behind-camera records only if the feed retains them, not complete
360-degree coverage. No scene-depth test: markers may show through walls. HDR
swatches are SDR visualization, not the game's tone mapping. No physical lumens,
stable object IDs, generic OFF field or sun/sky/emissive completeness claimed.

World coordinates already use capture-paired reconstruction; screen projection
uses the latest published camera basis/FOV/aspect, rejecting near/behind/invalid
points. It is not a Present-synchronous camera: fast-motion latency/alignment is
the primary live-check risk. Drawing still requires D3D12 / 8-bit SDR.

## Verification this change

Release build and **6/6 native UI/client tests pass**: overlay-model, overlay-d3d12,
notifications-d3d12, light-overlay-d3d12, light-overlay-only-d3d12, overlay-websocket.
Real D3D12 readback tests cover height-sensitive radar, projected rings/spot arrow,
behind/near clipping, stale/missing clearing, initially-hidden then shown,
independent toggles, Present/Present1, both resize paths, 4K and center detail card.
The center card initially failed visual QA because its placement gap intersected
the reticle margin; fixed and protected by a dedicated pixel regression.

Visually inspected small/4K test images: `build/light-overlay-*.bmp` (synthetic).
WebSocket test covers marker-only startup, >64KiB fragmented light payload,
immutable shared storage and loading invalidation. Actual recorded A2 JSONL
accepted by `CrimsonDesertTelemetryOverlayTests --snapshot <file>`: 337 records,
178 in front of its camera, 77 inside viewport. ZIP/expanded nine-file payload,
configuration and no-loose-JSON/no-second-ASI validation passed.

## Established baseline / preserved research

Previous detailed integration checkpoint is preserved in Git:
`git show 0d7ac9b:docs/HANDOVER.md`. Implementation `afcc1cc`; recorder fix
`73aea96`. That preview.1 passed cold start, real camera/player movement and
physical lamp A-B-A in PID27140 (now closed): target88/88 → 0/87 → 89/89;
three controls always present, B/A2 same view. 900 API rows, max rendered age79ms.
Artifacts: `artifacts/light-research/unified-lamp-aba-pid27140-20260906-*.jsonl`.

Movement artifact `unified-camera-movement-pid27140-20260906-01.jsonl` in the same
folder: 917 distinct captures, two static anchors stable throughout; filtered
crystal omissions are not OFF. Twelve transient bridge-changing rows and38
captures with one rejected record remain bounded reliability follow-ups.
Light capture/source itself is unchanged in preview.2.

Exact native-supported Steam build25116796, EXE SHA256
`4D99C15C58BD20A94D354D10AE395D1FAC777D59EF52CBA8080DC3FC8DC6F454`.
Native instrumentation fails closed on other hashes; automatic relocation and
combined real-game console startup validation remain separate tasks.

Product is this repository. `research/` is the preserved independent Git repo
(migration commit af5485b); `external/`, `artifacts/`, and original workspace under
`archive/crimsonhue-workspace-20260906/` are preserved/ignored. CrimsonHue is only
the future Hue consumer. No original files were deleted. Research entry points:
`research/light-source-tests/CODEX_HANDOVER_FIRE.md`,
`GPU_LIGHT_LAYOUTS_25116796.md` beside it, and
`research/console-enabler/HANDOVER.md`. Do not restart resolved research paths.
