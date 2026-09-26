# In-game light control — 2026-09-24, Codex

Latest entry: [WORLD_BUILDER_RESEARCH.md](WORLD_BUILDER_RESEARCH.md). Native
create/enable, server gimmick and UUID-lookup leads now exist, with offline
current-EXE checks and the exact known lamp prefab in upstream's catalog.
No native lamp toggle or RGB write is proven. Observation-only object capture
is the next bounded step; older "next" actions below are preserved alternatives.

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
complete control of every lighting branch. A guarded live A/B must check the
image, neighbors and restoration; the owner does not want a replay project.

The existing product hook is already post-filter, but its readback does not
change pipeline bindings. An extra compute pass WOULD: restore exact PSO/root
signature/root bindings/descriptor heaps and proper resource barriers. Never
inject a dispatch assuming the engine rebinds all state afterward; the captured
next pass reuses most of it. Never edit a previous frame's numeric light slot.
The old capture proves this old frame's chain, not current-build addresses.

## ManyLights input for off-screen coverage — 2026-09-26

Current priority is acquisition of off-screen lights, NOT light switching.
The public raw feed is raw POST-filter output, not raw input to ProcessManyLights.
Absence there does not establish absence in the input. Existing captured PSO475
tests frustum planes and HiZ before appending output; brightness/distance affect
selection extent. This is evidence for a concrete mechanism, not proof that the
current build rejected each missing source through one particular branch.

Exact current EXE SHA256:
`57da440d72f4db974f25fef047cf84c4dadd999a88cb2a3c5af4c9bd67fde1e7`.
Unwind-aligned function0x143DA8210..0x143DAB737 fully decoded by existing
`artifacts/light-research/preset-static-20260924.py --function 0x143DA8210`.
Saved listing: `artifacts/light-research/manylights-input-boundary-25477059-20260926.json`.

| Input provenance in current EXE | Evidence |
| --- | --- |
| Owner | RCX saved into R13 at0x143DA823E; same owner in existing Render bridge |
| Input outer wrapper | `mov rsi,[r13+0x628]` at0x143DA9406 |
| Binding name | RIP-relative literal `g_manyLightsDataBuffer` at0x143DA9452 |
| SRV bind | RSI passed in R8 at0x143DA94BC, virtual binder+0x4C0 at0x143DA94C5 |
| Later dispatch | Dispatch(512,1,1) at0x143DA97D4, existing hook at0x143DA97DA |
| Input resource wrapper | outer+0x30 -> inner; inner+0xC0 stride, +0xC4 count, +0x168 native resource |

Read-only live confirmation PID24696, bridge sample74395/frame48659/age47ms:
owner0x4B1AC67DC00, input outer0x4B18A88D200, inner0x4B18B2F2840,
stride48,count32768,native resource0x133430C90; bridge OUTPUT is0x133436AC0.
Live load bytes `498BB528060000` and live binding-name literal match disk.
These are process-local diagnostic addresses, NOT future anchors. Reads used
existing Render mapping's seqlock and Core.ReadOnlyProcess; no remote function
calls, GPU copy, breakpoint or writes. GPU contents were NOT read in this step.

Next bounded diagnostic: copy this input alongside output/counter and paired
scene using the existing fence-completed readback infrastructure, after the same
dispatch, preserving the input's actual resource state. Compare known camp
positions in both. Do not install stale console addresses or use its historical
fixed-delay completion. Input validity is a separate question: prior producer
captures had repeated groups and counter[0] represented free slots, not a live
prefix (CODEX_HANDOVER_FIRE.md, September6 producer checks). Post-filter counter
DWORD1 cannot be applied to this input. Its RGB also precedes the captured color
conversion. Presence upstream would locate the loss; it would not by itself
prove a complete, current, ready-to-publish360 light inventory.

Private implementation `2.1.15-manylights.1`: opt-in Research.ManyLightsPair,
explicit named-event request, max8 transactions,5s eligibility timeout. Uses
existing post-filter callback and queue-fence completion; does not add another
hook or change output/API bytes. Input access round-trip is enhanced BUFFER
COMPUTE_SHADING/SHADER_RESOURCE -> COPY/COPY_SOURCE -> original, whole resource.
Old captured input213 acquire/release corroborates this state in
CommandLists_000.cpp at GlobalId88 / later release; current exact input load and
R8 binder bytes are checked before enabling. Unsupported enhanced barriers,
wrong wrapper dimensions, aliased source/counter or wrong device refuse only
the diagnostic. A failed/in-flight capture retains resources until process exit.
File ABI: manylights_pair.h,128-byte header,2816-byte scene,1572864-byte output,
256-byte output counter,1572864-byte input. Header carries frame,bank,pid/process
start,resource identities,timestamps and fence. Partial files are rejected.
No interpretation of input occupied slots as live lamps is built into capture.

Owner explicitly distinguishes renderer view-selection from camera-position
geometric line-of-sight, including sources behind the camera. Neither implies
on-screen membership or physical ON/OFF. This diagnostic compares renderer
selection; existing physics estimates geometric line-of-sight. Off-screen
candidate completeness and freshness must be demonstrated before extending any
consumer stream. Existing PIX CSVs (including manylights.csv with569 PI-marked
slots and sceneconstants.csv) remain historical reference, not a valid-count or
current-frame proof; the later raytracing SRV name alone does not identify input.

### First live paired readback: camp behind camera

2026-09-26, private2.1.15-manylights.1, PID2252; installed ASI SHA256 matches
the package recorded in HANDOVER. One explicit request completed at frame35571,
fence7092,captured/completed tick2350765. Input resource0x25A3EEDD0, output
0x25A3F4370. Paired camera(-10533.81836,612.61902,-4424.75732), forward
(-0.580948,-0.193983,-0.790488). Output valid count61;13 behind camera.

INPUT xyz are WORLD positions in this capture, not camera-relative. Exact known
blue and Twilight Glass coordinates expose the initial decoder mistake of adding
the camera twice. Correct analysis uses explicit `--input-space world`; OUTPUT
still uses camera addition. Regression test covers this difference (6/6 tests).

| Known anchor (0.45gu match tolerance) | Input candidates | Output matches |
| --- | ---: | ---: |
| L1 / L2 test / L3 / L4 | 80 / 79 / 79 / 79 | 1 / 1 / 1 / 1 |
| Shrine bowl | 221 | 0 |
| Blue IC White Pavilion | 3 | 0 |
| Twilight Glass | 1 | 0 |

All matching input positions are behind the paired camera. Blue slot4 and
slots5737/15558 share exact position(-10510.69238,611.63318,-4371.43750), with
different raw colors. Twilight Glass slot2 is exactly
(-10493.73438,611.61084,-4364.25391). These upstream known-position matches
are absent in the simultaneous output: positive evidence for recovering missing
sources upstream, NOT a complete current-light inventory. In particular the
four lanterns DO have downstream matches here. The18922 PI-marked input slots
(16739 behind,13419 zero slots,427 other-marker slots) must not be counted as
physical lights; repeated/historical/generated-slot validity and color remain
open. No blind deduplication or publication of all input records.

Evidence directory:
`artifacts/light-research/manylights-pair-2252-20260926-camp-behind/`.
Original `manylights-pair-2252-2350765-1.bin`, log and INI preserved.
`decoded-world.json` supersedes initial `decoded.json`, which is invalid for
input positions because of the wrong coordinate assumption. Instrument/game
code was unchanged; only offline decoder corrected. Next: same player location,
camera facing camp, second paired capture to compare known-source membership.

### Live rotation comparison,13:56 to13:59 (same process)

Second pair `manylights-pair-2252-3931140-2.bin`: frame55620,fence34188,
camera(-10508.72949,607.93359,-4461.79443),forward(0.121950,-0.152331,0.980777).
Third pair `manylights-pair-2252-4116015-3.bin`: frame352,fence37401,
camera(-10507.34277,608.14697,-4450.25195),forward(-0.373713,-0.188034,-0.908285).
Owner was asked only to rotate and reported ready. Paired files do not include
player position; camera moves11.627gu on its third-person orbit, direction
changes155.197deg. These poses are184.875 seconds apart, not time-frozen data.
Each input/output snapshot uses one completed submission fence.

| Known anchor | Input candidates, both poses | Output facing | Output away |
| --- | ---: | ---: | ---: |
| L1 | 80 | 1 | 0 |
| L2 test | 79 | 1 | 0 |
| L3 | 79 | 1 | 0 |
| L4 | 79 | 1 | 0 |
| Shrine bowl | 220 | 2 | 0 |
| Blue IC White Pavilion | 3 | 1 | 0 |
| Twilight Glass | 1 | 1 | 0 |

All matched input positions change from front to behind. Total valid output
count100 ->23 (2 rear); these are whole captured output counts, not HUD counts.
Upstream known-position candidates persist while downstream matches disappear.
This supports engine view selection, not a forgotten35gu HUD/API cutoff. It does
not justify publishing every upstream slot: away input contains18943 PI slots,
with repeated records and unproven lifetime/color semantics. RawColor.w sign
alone cannot label fire entries inactive; their negative-w front counterparts
also coexist with matching valid output contributions.

Evidence directories `artifacts/light-research/`:
`manylights-pair-2252-20260926-camp-facing-1356/` and
`manylights-pair-2252-20260926-camp-away-1359/`, each with original binary/log/INI
and explicit world-input decode. Next is bounded offline input validity and
conversion analysis from saved pairs/known producer code, not another rotation
or arbitrary restoration of output tails. No public feed changed.

## PIX revisited for control parameters, not playback

Bounded offline check, 2026-09-24. Extracted captured PSOs 21562, 21564,
21574 and 21575 to `light-control-pix-20260924/`; no builds or live writes.
The time-accurate binding resolver proves:

- PSO 21562 `InjectEmitterLodLightCS`, event 779: t32/space37 is
  `gpuLodLightInfoList`, resource 15397; t20/space37 is
  `worldPositionSpawnBuffer`, resource 15359. Output u38/space39 is the
  UNFILTERED ManyLights resource 213; counter u19/space39 is resource 234.
- Its 80-byte `LodLightInfo` record explicitly names RGB at +0, packed
  flags at +12, position at +16, and `_injectLightCoefficient` at +76.
  The 16-byte `InjectEmitterLodLightRootSignature` has `_lightCount`,
  `_worldLightCount`, `_lodLightScale` (+8), and flags. Shader lines 399-405
  and 622-628 actually multiply RGB by `_lodLightScale` in both branches.
  This is real arithmetic evidence, not just a suggestive field name.
- Event 685 copies 1120 bytes (14 records) from resource 107 +86944 to
  resource 15397. Resource 107 is a 1 MiB UPLOAD heap buffer. Thus THIS
  branch has a precise CPU-upload route worth revisiting, not a heap search.
- Root CBV `GetGpuva(21583,4352)` decodes as counts 14/114, scale 1.0,
  flags 0x40000000. These are different domains, not 128 separate physical
  lamps: the second branch uses spawn transforms and indexed LOD entries.

Scope and pitfalls: this is LOD lighting, NOT proof it controls the nearby
fire's detailed/pulsating components. Entries may be shared by instances;
their position can be local or world space. `_injectLightCoefficient` also
changes a non-RGB output term, so it is NOT a clean brightness-only knob.
Setting a LOD scale to zero would not establish gameplay OFF or all-light OFF.
The extracted `lod-light-info-15397.bin` holds INITIAL resource bytes; event
685 overwrites its first 14 records. Do not label those initial values as
event-779 input without following the upload source. Output/initial-state
distinction matters even though both are readable offline.

The captured `GPUSpawnPointUpdateCS` (PSO 21575, events 771/775) also binds
u38 ->213 and u19 ->234. Its inspected ManyLights stores initialise zero
RGB/group headers, not a demonstrated final color builder. Its reflected
`GPUEmitterSimulationConstants` names `_injectLightIntensity` (+208) and
`_particleLightColorScale` (+272); `CommonUpdateRootSignature` names
`_globalParticleLightScale` (+60). These are CANDIDATES only until their
actual uses and target-lamp association are proven. Related names were
already catalogued in `research/light-source-tests/LIGHT_BUFFER_MAP.md` §12;
do not present the strings as a wholly new discovery or adopt that section's
superseded subsystem conclusions.

Decision: keep post-filter RGB modulation as the first bounded proof for the
known lamp (all captured contributions, frame-paired position). Preserve the
LOD-upload branch as a concrete earlier alternative, not a reason to switch
tracks or assume it covers all lights. No light has yet been controlled.

## Existing physical-source evidence and current-build check

### Preset route relocated offline, 2026-09-24

Owner recalled `Let There Be Light` (local XML under `research/Let There Be
Light/files/0011/miscellaneous/lightpreset.xml`). Prefer checking its upstream
preset route before building persistent renderer modulation. The mod's XML
does not establish how its author researched it, hot reload, or per-instance
control. `NATIVE_PRESET_TRACE.md` already proved MarkerA value 73.125 in both
native Candlelight map/compact entries on an OLD build; do not redo that search.

For exact EXE hash 57DA440D... above, disk-only findings:

| Role | Current VA / path |
| --- | --- |
| XML path string | 0x145D01208, `.xdata` |
| Loader, containing validated XML xref at 0x143994449 | 0x143994310..0x143995311 |
| Setup loader call | 0x143BAF1B6, RCX from `[RDI+0x778]` |
| Parent global published from same RDI | module+0x6C8E978 |
| Direct preset owner published at 0x143DC9DB2 | module+0x6C8E9D0, from `[RCX+0x778]` |
| Compact-table builder | 0x143995630..0x1439958EF |

Parent member moved **+0x760 -> +0x778**. Map family remains owner+0x90,
stride 0x20; builder still uses 24-byte compact elements and copies node
value/color/temperature +0x10/+0x14/+0x18 to +8/+0xC/+0x10. Loader reads
group scale +0x124/+0x128. These are code/layout observations, NOT current
live pointer/table validation or proof of effective brightness units.

Six unwind-aligned direct reads of the new owner global feed calls to
0x143995B30 / 0x143995C00, all with group 2 (Nit), mirroring the historical
material branch. Do not mistake these for Lumen/individual-lamp consumers.
Two further raw xref candidates at 0x15416E0C0 and 0x154173A10 have no enclosing
unwind function and remain UNVALIDATED; packed/unpacked code needs live bytes.
The owner-publication instruction is decoded from an unwind start, but its
whole larger function does not decode to its end. Never claim a complete CFG.

Evidence/helper: `artifacts/light-research/preset-static-20260924.py` and
`preset-static-25477059-{loader,owner,consumers,publisher,direct-readers,lookup-callers}.json`.
Helper only reads disk, searches specific references and disassembles bounded
unwind-covered functions; regex hits without boundaries stay candidates.

Next bounded live action: after owner starts the game, READ and compare
`*(module+0x6C8E9D0)` with `*(*(module+0x6C8E978)+0x778)`, validate named
Lux/Lumen/Nit tables and save their current layout/values. Read the two exact
unvalidated code windows if needed. No heap scan, no toggle request, no write
or stale-address breakpoint. Only then decide how to trace a particular
preset's consumers; do not assume modifying a shared table updates existing
instances. The game is currently closed, no trace/hook is armed.

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
