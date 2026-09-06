# Current checkpoint — unified telemetry, 2026-09-06, Codex/Astra

## Result and scope

User requested one telemetry product/ASI, including the former console, automatic
filtered ManyLights and the revised camera anchor. CrimsonHue is only a future
Philips Hue consumer. Candidate **1.3.0-preview.1**, implementation commit **afcc1cc**,
has passed automatic in-game startup/data and deliberate camera/player movement
checks. The physical A-B-A check of this combined version remains open.

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
not a replacement for player position. That was the previous process; see current
live result below.

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

## Live result — 2026-09-06, ~19:44–19:45 CEST

User installed through DMM, restarted, loaded and reported the success notice.
PID **27140**, start19:41:38.708. Exactly one loaded ASI, telemetry SHA256
`851856C3D78103ABEF972C1B0BD41C4C4BE9A78AE933D0A74F1285C414AF733E`, matching
the candidate; no CrimsonHueConsole loaded. Native log confirms exact-build
postfilter detour and submission-fenced capture ready. No native fault/refusal.
HostPID25296, health `playing`, schema1.4, exact supported build. One initial
duplicate-bootstrap/port-owner skip precedes the successful host start; not a
second loaded ASI or a later host failure.

Read-only API check: **40/40 available** over5.24s;40 distinct captured game frames
9243..9480, capture sequence2719..2798. Roughly15 captures/s observed (20Hz configured
target, not a measured20Hz guarantee). Data age15..78ms, mean45.95ms, malformed0.
13 authored records;119..130 current rendered contributions within the configured
100-game-unit player radius (not119..130 physical lamps). A preceding sample had
343 total active records,218 outside radius and125 published.

Blue IC glass, Twilight Glass Lamp and Twilight Crystal each matched their previously
measured world positions in all40 samples, with no position spread at API precision.
The two glass-lamp directions were downward and cone half-angle~27degrees;
crystal classified point and correctly omitted direction. Player was
(-10502.611,610.52814,-4373.8613). Camera was essentially stationary aside from
~0.0013 vertical variation, so **not a deliberate movement-pairing proof**.
No manual command, breakpoint, switch or config change was issued during this check.
Native/overlay logs show Starting→Waiting→Loading→Telemetry is ready.

## Movement result — 2026-09-06, ~19:49–19:50 CEST

User deliberately turned/pitched camera and walked forward/backward in PID27140.
Existing API recorder updated for bounded4MiB light payloads (commit73aea96).
Capture: `artifacts/light-research/unified-camera-movement-pid27140-20260906-01.jsonl`,
201489649bytes,3597 WS samples over59.909s (~60.025Hz),917 distinct rendered captures
7228..8144, native frames23018..26020. No API sequence skips/duplicates/regressions.
Camera coordinate spans13.6485/7.26307/18.4697; player spans4.671/0.15198/8.0373 game
units. This is a real movement control, not another stationary recording.

Both static glass anchors occurred in917/917 distinct captures with **identical
world positions at API precision** throughout. Maximum distance to rounded prior
references0.000413 game units. Crystal occurred in469/917, also identical position;
448 captures omitted it (even without a kind filter). Do not label that OFF;
view-dependent filtering remains the relevant distinction.

Latest envelope camera differs from paired light camera by up to1.713 game units
and13.99degrees across all available WS samples. Using that latest camera would
incorrectly shift the static lights; actual paired-camera conversion stays stable.
Thus pairing passed this real movement test.

Quality caveats: authoredavailable3597/3597; renderedavailable3585/3597. Twelve
isolated `bridge-changing` samples, max33.047ms to recovery, correctly publish no
rendered values. No stale/native-fault result. Rendered age across all samples:
median47ms,p9578ms,max94ms. On38 distinct captures one record was rejected as
malformed (not published); its raw cause is not established. These small availability/
coverage issues are retained for later hardening, not hidden as a perfect run.
No implementation/config/game change during measurement; package unchanged.

## Remaining / one next step

Stay in PID27140; no reinstall required. **A1 ON baseline captured** in
`artifacts/light-research/unified-lamp-aba-pid27140-20260906-A1-on.jsonl`:
309/309available samples,88distinct captures,frames44038..44347. Player
(-10529.909,609.15674,-4419.7046) is at known lamp2
(-10529.755,611.292,-4420.300). All four lamp regions present in every distinct
capture. Spatial test fixed in advance: horizontal distance<0.65 and vertical
distance<3 from the four previously measured positions. Lamp2 has1..2 point
contributions, summed linear luminance0.05925..0.15509 (animated, not lumens).
Next instruction to user: switch this lamp OFF once, stay there, report AUS;
capture B before requesting ON again. Cold-start availability and camera pairing are measured, not merely a host-test
claim. User prefers DMM; never replace game files with the optional helper without
a new request. Do not modify ASI loader/other mods.

Do not claim the remaining switch test completed. Console/explorer is
integrated but disabled in this package; its startup debug capture was not exercised
in the combined real game yet. Native automatic compatibility beyond the exact
supported EXE remains a separate follow-up, not a reason to guess offsets.
