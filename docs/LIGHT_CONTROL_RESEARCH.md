# In-game light control — 2026-09-24, Codex

Owner's goal: an in-game music/light show, no physical Hue output. Investigation
and reversible experiments are authorized. No individual light has yet been
programmatically switched. No plugin/game-memory writes were made in this step.

## Two different outcomes

- A real object interaction could switch flame, emitted light and gameplay state.
  Its callable entry, arguments and required game-thread context remain unknown.
- Renderer RGB modulation could dim/tint the illumination of selected sources,
  including non-interactive ones. Flame particles and emissive fixture materials
  may remain visible. This is a viable light-show goal, not proof of full-object
  switching. Do not promise either outcome before a controlled visual test.

## Old PIX capture reused, not recaptured

Capture: `artifacts/light-research/pix-captures/CrimsonDesert_lantern_2026-09-05_2346.wpix`.
Existing export: `artifacts/light-research/pix-provenance-20260909/cpp`.
Extracted actual captured PSOs 471, 474, 475, 479 via
`Map-PixExportShaders.py --extract`; disassembled with `dxc -dumpbin`.
Outputs: `artifacts/light-research/light-control-pix-20260924/`.

Time-accurate `Resolve-PixExportBindings.py` results:

| Captured step | Proven binding |
| --- | --- |
| Event 93, PSO 475 `ProcessManyLightsCS`, Dispatch(512,1,1) | t18/space37 = resource 213; u13/space39 = 217; u2/space39 = counter 230 |
| Event 147, PSO 479 `BuildLightTreeLevel0CS`, ExecuteIndirect | t18/space37 = resource 217; u14/space39 = tree 248 |

PSO 475 writes a 48-byte output record: position at +0, RGB and a fourth
non-RGB component at +16, packed directions at +32/+40. The captured shader
performs a color matrix conversion before storing RGB (listing lines 1086–1105).
Do not feed arbitrary display RGB into it as if its working color space were
proven. Scalar RGB multiplication is a simpler first experiment.

**Candidate intervention:** after event 93 and before light sorting/tree work,
scale only RGB of all contributions at one known camera-paired world position;
leave geometry, direction, fourth color component and live count intact. This
lets the downstream tree read edited colors. Earlier bounds/average calculations
(events 89/91) have already run: selection thresholds/normalization may therefore
remain based on originals. This is a bounded candidate, not established safe or
complete control of every lighting branch. A replay A/B should check the image,
neighbors and restoration before any live hook is added.

The existing product hook is already post-filter, but its readback does not
change pipeline bindings. An extra compute pass WOULD: restore exact PSO/root
signature/root bindings/descriptor heaps and proper resource barriers. Never
inject a dispatch assuming the engine rebinds all state afterward; the captured
next pass reuses most of it. Never edit a previous frame's numeric light slot.
The old capture proves this old frame's chain, not current-build addresses.

## Existing physical-source evidence and current-build check

Reuse `research/light-source-tests/CODEX_HANDOVER_FIRE.md`, section
`CURRENT CHECKPOINT - 2026-09-06, source-to-render join and active child`.
The persistent lamp Source was `pa::SceneObjectClient`, prefab
`/object/cd_gimmick/effect_gimmick/gimmick_fire_spark_lamp.prefab`, position +0xA0.
Its active-child count +0x238 changed 0/1 with ordinary OFF/ON. Counts +0x120,
+0x218 and +0x238 are containers, NOT writable toggle flags. The old source
address and +0x2ECF350 sourcewatch code anchor must not be reused blindly.
Earlier LightSwitch handler/pool heap and registry searches were negative;
do not repeat them merely because that class name exists.

Current patch 2.03.02 / build 25477059, EXE SHA256
`57DA440D72F4DB974F25FEF047CF84C4DADD999A88CB2A3C5AF4C9BD67FDE1E7`:
the RTTI tools missed COLs/vtables because these moved to `.xdata`, while names
remain in `.rdata`. Both resolvers now include `.xdata`. Offline fixture tests
cover old/new section layouts, exact addresses, absent names and missing sections.
Static results: SceneObjectClient tables 0x14557E0F8 / 0x14557E110 / 0x14557E120;
LightSwitch data table 0x1457C3058; pool table 0x1457B8C18. These are anchors,
not callable switches. CSV evidence is `light-control-25477059-*.rtti.csv`.

User started PID 13528 at the known camp. Telemetry responds with progressing
samples and player around (-10529.91,609.16,-4419.76). Two read-only, 30-second
position-filtered scans at (-10529.755,611.292,-4420.300), tolerance 0.1,
returned zero matches but scanned only 3696/4294 MiB. BOTH ARE INCOMPLETE;
no absence/layout-invalid claim is supported. Second used SIMD signature search;
it did not materially solve scan cost. Do not repeat broad scans or ask the
owner to toggle without an armed, validated reader. Artifacts:
`light-control-lamp-on[-fast]-pid13528.instances.json`.

Research tool now has direct-position filtering, a bounded scan and explicit
completion/read-failure metadata; no generic scene-object inventory is dumped.
It requires PS7. Tests also cover exact signature alignment and position/NaN
filters. Saved-address reuse requires the same PID and process start time.

## Owner clarification and next bounded step

The owner intended PIX as access to captured data, not a scene-replay project.
Do not continue the C++ replay build or make restoring its scene a prerequisite.
The separate build was terminated; all files, original export and capture remain.
Baseline PIX playback completed, but its final image and the capture's stored
original show HUD over black. Intermediate events 15635/15637 were also black.
This limitation was already recorded in the historical fire handover; its
exposure explanation was a hypothesis, not established. No lamp was modified.
Evidence: `artifacts/light-research/light-control-replay-20260924/`.

Use the proven captured bindings/layout to prepare a private, bounded and
reversible one-lamp RGB intervention at the current post-filter hook. Confirm
current resource bindings/state and target matching before writes; an old capture
does not validate current runtime addresses. Visual validation belongs in the
live game, with explicit original/modified/restored states. Keep the physical
object interaction route available without making it a prerequisite for the
light-show goal. The game is closed. Published packages and live plugin remain
untouched.
