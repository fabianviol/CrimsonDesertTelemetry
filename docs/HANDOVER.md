# Current checkpoint — unified telemetry, 2026-09-06, Codex/Astra

## Result and scope

User requested one telemetry product/ASI, including the former console, automatic
filtered ManyLights and the revised camera anchor. CrimsonHue is only a future
Philips Hue consumer. Work is implemented in candidate **1.3.0-preview.1**; native
live validation remains mandatory, not inferred from the previous manual captures.

- Single ASI source: `native/CrimsonDesertTelemetry.Asi`; imported console0.3.17
  commands retained under `src/console`, original research/releases preserved.
- Native recurring postfilter capture: exact EXE hash + callsite signature,
  scoped detour, D3D12 copy on the actual command list and completion fence from
  its submission queue. Readback paired with a copied full SceneConstantBuffer.
- API schema1.4: existing authored `lights.sources` plus `lights.rendered.sources`
  with world position, linear HDR RGB/luminance, recognized point/spot and spot
  direction/cone. They overlap; do not sum them or invent physical IDs/OFF states.
- Camera source was already direct, contrary to the old camera handover premise.
  Current scene is `*(module+0x6B4EFB8)`; **dereference scene+0x428** for the full
  0xB00-byte buffer. Own frame+0x20, viewPos+0x80. Decoder validates full structure,
  projection and matrix consistency independently of player distance.
- Native bridge `Local\CrimsonDesertTelemetry.Render.<pid>` validates PID +
  process-start FILETIME, protocol, seqlock, paired frame and fence flags; reject
  samples older than500ms. Native capture default20Hz, API60Hz. No stale success reuse.
- Lights default on in candidate; HUD and research console/explorer default off.
  Old CrimsonHueConsole.asi is a conflict, not a second required plugin.
- User additionally requested in-game startup/loading/ready/error notices independent
  of the full HUD. `[Notifications] Enabled=1` default, ready visible6s, errors
  remain until recovery. UI reuses D3D12/SDR rendering, not a second ASI. Full HUD
  remains optional; parser updated to accept schema1.3/1.4 too.

## Preserved workspace

Product Git history remains in `C:\DEV\CrimsonDesertTelemetry`.
Research Git history moved intact from `C:\DEV\CrimsonHue\research` to `research/`.
963 capture/artifact files moved without file collisions into `artifacts/`.
`external/` checkouts moved intact. Old root source/docs/tests/definitions,
standalone copies, bundled runtime and old root ZIP moved to
`archive/crimsonhue-workspace-20260906/`; no original files were deleted. Empty
directory layouts also retained there. These large/private trees are Git-ignored;
the research repository still commits independently. Historical absolute Hue
research/artifact paths map to this product root. Hue now only has placeholder
README and synchronized assistant rules. Product rules are synchronized too.

## Evidence and limits

Camera live check in old-plugin PID28640:30/30 valid samples,30 distinct source
frames, source0x3852A770C00,3840x2160. `_renderingOriginPos` was ~1.03 above player,
not a replacement for player position. Game now closed by user for DMM migration.

Existing measured light A-B-A, packed direction, source and consumer evidence:
`../research/light-source-tests/CODEX_HANDOVER_FIRE.md` (latest experiments at end)
and `GPU_LIGHT_LAYOUTS_25116796.md` in that folder. The camera handover has been
marked with the verified newer result. Do not restart resolved research paths.

No promise of every game's lighting path, physical lumens/range, persistent IDs
or generic enabled state. Filtered contributions are current renderer outputs;
absence also includes culling/loading. Sun/sky/emissive coverage is not solved.
Update hardening: basic player/camera guarded relocation remains; new native code
uses exact known executable **25116796**, SHA256
`4D99C15C58BD20A94D354D10AE395D1FAC777D59EF52CBA8080DC3FC8DC6F454`.
Different EXEs refuse hooks. Automatic native relocation is a later task.

## Built and tested

- 49 managed tests,4 API client/example tests,8 native CTests pass. Native tests
  cover seqlock coherence, delayed real D3D12 queue/fence completion and repeat
  capture,80,000 multithreaded detour calls preserving registers/flags/XMM, loader-safe
  SHA vectors and exact installed EXE, HUD/client and HUD-independent notices.
  Final missing-requested-feed correction passed rebuilt overlay-model suite.
- Packaged ASI bootstrap started its real managed host from `.cfg` metadata;
  schema1.4 health/schema endpoints responded. Test host exited; no game files changed.
- Notification-only D3D12 render/resize tests passed,4K image visually inspected
  (`build/notice-preview-4k.ppm`). Optional direct migration/restore helper tested
  against a temporary fixture only. Release ZIP overwrite guard tested.
- Research commit **af5485b** preserves migration pointers and camera correction.

Immutable DMM ZIP:
`artifacts/mod-manager/CrimsonDesertTelemetry-v1.3.0-preview.1-ModManagers.zip`
SHA256 `A22A9254DCA5D3E6A3946E6CCFC4D1C6AA08AE831F8EAA168695686190E613CA`.
Expanded package:
`artifacts/mod-manager/v1.3.0-preview.1-20260906-193800-829-148b0879/CrimsonDesertTelemetry`.
Source is the unified implementation commit containing this checkpoint; no source
changes are pending before the user's first in-game test.

## Remaining / one next step

ZIP delivered to user. They prefer DMM; **do not directly install with the optional
helper**. They were instructed to deactivate both previous packages before enabling
the unified package. Do not modify ASI loader/other mods. Next: one cold-start test,
**without any preparatory lamp toggles**. Bridge must reach available while game
frames advance, move/turn to validate paired world positions, one lamp A-B-A and
console smoke if enabled. Do not call that completed before actual observations.

Only claim successful game integration after that live test. Console/explorer is
integrated but disabled in this package; its startup debug capture was not exercised
in the combined real game yet. Native automatic compatibility beyond the exact
supported EXE remains a separate follow-up, not a reason to guess offsets.
