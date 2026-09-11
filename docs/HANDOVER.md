# Current checkpoint — takeover, 2026-09-11 end of day, Claude

Written for someone taking over who has read the repository but was not here today.
Eighteen commits landed; this replaces the running notes they were written as.

## Where this stands

The game updated to Steam build `25246367` this morning and broke everything. All of
it is working again, both product goals are measured live on the new build, and a
compatibility release is uploaded to Nexus. Nothing here is waiting on a decision
except the two items under "What is actually open".

| | state |
|---|---|
| light/player/camera telemetry | working on 25246367, verified live |
| ambient (goal 1) | working, measured from 0.3916 outdoors to exactly 0 indoors |
| occlusion (goal 2) | confirmed at two sites, by two independent implementations |
| public release | `v2.0.2` uploaded, Nexus virus scan still running |
| CrimsonHue | untouched, consumes neither feed |

## The machine right now

- **Installed and running:** private package `2.0.3-ambient.2`, ASI `C2DD4CBD…`. This
  is a RESEARCH build, not the release.
- **The installed INI is a test configuration**, edited by hand during the session:
  `SpatialProbe=1`, `SpatialReadback=1`, `SpatialVisibilitySeconds=1800`,
  `SignedDistanceReadback=1`, `[LightOverlay] OcclusionTest=1`. Both `bin64` and the
  DMM mods folder carry it, so a redeploy will not silently revert it.
- **`bin64` holds 313 R16 payloads, about 5.9 GB.** They accumulate at 17 MB each and
  nothing deletes them. The ones worth keeping are already copied into
  `artifacts/light-research/`. Clearing the rest is safe and overdue.
- **125 commits are unpushed.** Pushing publishes the whole research history; that is
  the user's call and was never made. The Nexus page links `blob/main/docs/API.md`,
  so that link currently shows a pre-update document.
- `.env` holds working VirusTotal and Nexus API keys. Git-ignored; packages are built
  from an explicit nine-file list, so no key can reach a release.

## What broke and what fixed it

The update moved code. **Four native anchors, all by exactly +0x21C0**, which is itself
the evidence that a region shifted rather than functions changing:

| where | constant | old | new |
|---|---|---|---|
| `ambient_probe.h` | `AmbientHookRvas[0]` | `0x3849BB7` | `0x384BD77` |
| `ambient_probe.h` | `AmbientHookRvas[1]` | `0x384CBA3` | `0x384ED63` |
| `spatial_probe.cpp` | `DispatchRva` | `0x37B4360` | `0x37B6520` |
| `spatial_probe.cpp` | `ExposureReturnRva` | `0x35450A4` | `0x3547264` |

Plus two managed gates that had nothing to do with the update and everything to do with
duplicated build identity:

- `SkyAmbientReader.SkyProducerRva` was the old `AmbientHookRvas[0]` as a literal.
- `Program.cs` constructed the sky reader only when `GameBuild == "25116796"`, so ambient
  stayed dead even after the native relocation. **That cost an entire debugging round:**
  the endpoint reported `unsupported-build`, which is also what it reports when the
  feature is simply switched off, so the message actively misled. It is a blanket
  fallback for a null reader, not a statement about the build.

`ProcessManyLights` had already been relocated by Codex before the session
(`0x3CB65CA` → `0x3CB89DA`).

**The lesson that became rule 3a:** the ambient hooks were recoverable from their own
signatures because those happen to be unique. The spatial pair was not — its anchor is
an ordinary function prologue occurring **1390 times** in the image. It was recovered
only by reading 32 bytes of context at the old address in the PREVIOUS executable.
`scripts/Backup-GameExecutable.ps1` now preserves every executable, idempotent by
content. Without the old binary that relocation was not possible, and an overwritten
executable cannot be got back.

## Goal 1: ambient — working and measured

Three positions on the new build, one session, monotone:

| where | visibility | local estimate |
|---|---|---|
| open ground, Serkis Estate | 0.3916 | 0.003436 |
| stable with an open frontage | 0.0476 | 0.000144 |
| enclosed interior, Warspike Spearmaker | **0.0000000** | `[0, 0, 0]` |

0.39 outdoors sits inside the 0.27–0.39 band measured on 2026-09-10, so the feed means
what it meant before.

**The enclosed value is exactly zero**, checked unrounded. That settles a product
question: a lamp driven by `sky × visibility` goes black indoors and nothing recovers a
signal from zero. The perceptual curve with a floor is mandatory, not polish.

Weaker evidence than the 2026-09-10 barn traverse, and recorded as such: the sky term
also fell (0.00887 → 0.00302) because game time advanced into dusk. What carries the
result is that the visibility term is independent of the sky by construction, not the
comparison.

**Sampling limit worth acting on:** the combined estimate was null in two of four
consecutive samples in one place and fresh five times running in another. The freshness
bound is 1500 ms against a 0.31–0.48/s acquisition rate, so the paired value flickers
intermittently rather than predictably. A consumer must hold the last good value.

## Goal 2: occlusion — confirmed twice, by two implementations

Second site: Warspike Spearmaker, a fireplace point light, the user stepping behind a
partition and back. Thirty copies traced offline with the parameters fixed on
2026-09-10 and nothing re-tuned: clear ×10, blocked ×3, clear ×17.

The native HUD marcher (`OcclusionTest=1`, Codex's code, never run live before) ran at
the same time and reported `CLEAR +0.45148` and `BLOCKED −0.03038` on that light. Both
land inside the offline bands (+0.301…+0.496 and −0.0039…−0.0400). Native C++ marching
a volume in memory, Python marching a file on disk, sharing the calibration and not the
code.

**The thin margin is systematic, and it has a visible consequence.** A lamp two game
units from the traced one read `CLEAR +0.00618` from one pose and `BLOCKED −0.01891`
from the other. Anything consuming this needs hysteresis or it will flicker on
borderline geometry.

Stopping rule unchanged: no t224, no raymarch reconstruction, no coverage model, no
DXR, unless A visibly fails on a concrete case.

## The release, and the antivirus work behind it

Nexus rejected `2.0.1`. What was then measured rather than guessed is in
[ANTIVIRUS_FINDINGS.md](ANTIVIRUS_FINDINGS.md); the two conclusions matter for every
future release:

- **Microsoft `Wacatac.B!ml` is not ours.** Proven by rebuilding the v2.0.0 source with
  today's toolchain: identical source, now flagged, where the September build was not.
  It also flickers across builds. No feature removal fixes it. **Do not trade
  functionality for it** — that was nearly done before the control was run.
- **Bitdefender `Barys.73277` is ours and is pinned** to commit `6937fa9`, the bounded
  repeated readback series. Releases build with `CDT_RESEARCH=OFF`, which excludes it,
  and the 2.0.2 ZIP scans 0/67.

`CDT_RESEARCH` derives from the version: off for releases, on for prereleases. Nothing
was deleted; the research sources and their tests are untouched, and two stand-in
translation units satisfy the same interfaces when it is off.

One correction worth carrying: `category: main` from the Nexus API was briefly written
down as "passed the scan". It is not. **The v3 API exposes no scan state at all** — only
the mod page does.

## What is actually open

1. **CrimsonHue.** It consumes only `/v1/stream`, neither ambient nor occlusion. Both
   feeds are now proven on the current build. The perceptual curve with a floor and the
   occlusion gate with hysteresis belong there. This is the next real step and it has
   been deliberately untouched for two days.
2. **The Nexus scan on 2.0.2.** Nothing to do but watch the page.
3. **Housekeeping:** 5.9 GB of payloads in `bin64`; the installed INI is a test
   configuration; `docs/API.md` still describes the pre-update state and its link on
   Nexus points at unpushed `main`.

**One next step:** open CrimsonHue and wire the ambient estimate to lamp brightness
through a curve with a floor. Everything it needs is measured, and the floor is not
optional — the indoor value is exactly zero.

---

# Previous checkpoint — build 25246367 test candidate, 2026-09-11, Codex (history)

**ESTABLISHED:** the previously installed plugin failed closed on the update: Steam
build `25246367`, EXE `1.0.0.2850`, SHA256
`BCBF623AD5690147DC462AEAED5B4F97BD73296BA0D6AB54663586E7088B1C0E`.
Health reported `unsupported-build`, zero native copies and no hook installation.
The new EXE is preserved privately under
`artifacts/recovery/20260911-build-25246367/`.

Offline and bounded read-only live recovery are complete. Camera/player anchors
relocated, scene vtable is `0x5C04768`, the RTTI-guarded player chain and all known
object offsets still validate, camera frames progress, and 18 authored-light records
decoded with zero malformed records. `ProcessManyLights` moved by `0x2410`; its
13,607-byte body has the same 2,890 normalized instructions, register lifetime and
binder/Dispatch contexts. New hook RVA is `0x3CB89DA`. The update checker formerly
reported this hook missing because it compared a relocated `call rel32` literally;
the offline finder now anchors it to the unique dispatch context while runtime
preflight remains byte-exact.

**READY, NOT INSTALLED:** `artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-build25246367.1-ModManagers.zip`,
ZIP SHA256 `57C7DD5E5E6142A38259977A92BE3EC453C8D084F80B3E02F3F16959E5583E98`,
ASI SHA256 `55DA4A5A32C794B8520132F5C1A02032B9A91A1842BEFED95573DC874600915B`.
Its baked INI has Ambient, SDF and every Spatial research switch off; F9 details are
on. Managed tests, 25/25 native tests, package validation and the production native
preflight against the preserved new EXE pass.

**OPEN:** the new GPU light stream has not run live. Ambient/Variant A remain parked;
ambient hook bytes were deliberately not promoted. No t224, PIX, DXR or CrimsonHue work.

**ONE next step:** user closes the game, installs the ready ZIP through DMM and loads
the same save. Check native log and `/v1/health` for exact build, progressing camera
and fresh paired copies, then perform one documented lamp ON → OFF → ON control. Stop
on any preflight/copy failure; do not reopen the parked research.

---

# Previous checkpoint — PARKED after game update, 2026-09-11, Codex

**PARKED by user instruction.** A Crimson Desert update was installed after `hud.3`
was built. Everything below was measured against the previous supported game build and
must not be projected onto the updated executable or shaders. The new build number,
executable hash, native instruction contracts, resource mappings and runtime behavior
are all UNVERIFIED. Preserve all packages and evidence; do not install `hud.3`, resume
Ambient/Variant A, inspect t224 or start PIX work while this checkpoint is parked.

**Resume gate, only when explicitly requested:** identify the new game build and
executable hash, then run the existing fail-closed build/preflight checks before any
instrumented package is installed. A refusal is a compatibility result, not evidence
that the telemetry sources disappeared.

---

# Previous checkpoint — evidence review and HUD test path, 2026-09-11, Codex

**ESTABLISHED:** Ambient dims under cover. Three retained, independently reproducible
R16 payloads also separate one user-labelled visible → occluded → visible torch sequence
with Variant A. Their verdicts are clear / blocked / clear and closest approaches are
+0.3155 / -0.00047 / +0.5081 gu with the fixed parameters below.

**EVIDENCE CORRECTION:** the previous checkpoint said 19 traces, while its table listed
3 + 10 + 9 = 22. Neither total is reproducible from the retained evidence: the folder
contains exactly three payloads, one per pose. Those three establish the A-B-A separation,
but do not preserve the test plan's requested three fresh copies per pose. Claims about
the unretained copies are observations, not independently checkable evidence.

The retained Variant A case ran in PID15940 at the player home, 2026-09-10 23:51,
against a lit doorway at
`(-10403.25, 613.84, -4419.10)`, identified by world POSITION and never by the rendered
sample index. One fixed parameter set (`hit_tolerance=0.0`, `minimum_step=0.05`,
`iteration_bound=400`, `start_offset=0.6`, `end_margin=1.0`), fixed before the occluded
phase was examined, classifies all three retained poses without contradiction. Evidence:
`artifacts/light-research/variant-a-pid15940-20260910-2351/`.

**INFERENCE / limit:** the retained blocked ray GRAZES the wall at -0.00047 gu. The
reported unretained range was −0.0000 to −0.0068 gu. A tolerance sweep of the retained
poses holds the classification from −0.05 to +0.10, so there is a working band, not a margin. Thin
geometry, doorways at grazing angles, moving occluders and other materials are untested —
not known-bad. Widen the test only when a concrete case fails.

**OPEN:** reliability across repeated copies of each pose is not established by the
retained evidence. The native HUD now contains the same fixed Variant A marcher so this
repeatability and concrete failures can be observed in-game without exporting payloads.
It reports CLEAR / BLOCKED / UNKNOWN, the closest signed distance, volume age and the
CPU-bracketed context frame. `[LightOverlay] OcclusionTest=1` starts one bounded 120-copy
diagnostic run and keeps the 16.25 MiB payload in memory instead of writing every copy.
F9 diagnostics polls `/v1/ambient` and shows global sky, camera visibility and the local
estimate with their separate ages. No public occlusion API has been added.

**Stopping rule, in force:** no t224, no reconstruction of the engine's raymarch, no
coverage model, no DXR.

**CALIBRATED:** the stored value is a signed world distance in GAME UNITS, negative
inside (measured through a surface at y ≈ 606.72), gradient magnitude 0.99957 in the
unsaturated band, `cellSize(L) = 0.25 * 2^L` read from the GI constants, clamp exactly
`1.5*sqrt(2)` cell sizes on all eight levels. Toroidal wrap confirmed at 16 gu in y for
level 0. Two new tools carry this: `scripts/Decode-SignedDistance.py` samples the live
payload, `scripts/Trace-SignedDistance.py` traces and reports the closest approach so an
endpoint self-hit at the fixture is distinguishable from a wall. Both are in
[TOOLING.md](TOOLING.md).

**RUNTIME FIX — a run no longer costs a game restart.** Two separate causes. First,
`if(requested_) return false` in `SpatialReadback::Begin`; `Restartable()` replaced it
and keeps what that rule protected — a series still owing transactions is never replaced
mid-flight, and a destination the GPU or an open map may still touch is never handed to a
new series. Second, and the one that still blocked it afterwards: the dispatch hook sets
the probe's phase to `Pending` every observed frame, so it passed through `Idle` for only
a fraction of a frame and the request event was consumed and discarded almost every time.
The request now tests `observing`, and all three outcomes — started, already running,
previous series unsettled — are logged. Diagnosing the second cost a round of guessing
precisely because a refused request logged nothing.

**INSTALLED and measured:**
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-restart.2-ModManagers.zip`.
ZIP SHA256 `B7C4EBF34DFDAF27BAF365C97C19A064D4DF9A76E954369C2CFDC758829863F2`;
ASI SHA256 `09DC5B43C02BEA822C7694B5EC304C7921EC46B6326D5B17C219E1D6090FDC15`.
Built from HEAD, so it includes Codex's `a87a2ab` (distinct SDF barrier transitions) and
`27c094d` (bounded R16 acquisition) alongside the restart work. Its INI is baked in via
`-IniOverrides`: `SpatialProbe=1`, `SpatialReadback=1`, `SignedDistanceReadback=1`,
`SignedDistanceReadbackCount=120`, `SignedDistanceReadbackIntervalMs=2000`,
`SpatialVisibilitySeconds=0`, `AmbientProbe=0`. The earlier
`v2.0.1-restart.1` ZIP (`0A37F27F4059D09D68C0A8F2B8F37C2BE23A4E35A7D9731CB3165AE1E9167CAF`)
is superseded but preserved.

**BUILT, validated, not installed while the game is running:**
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-hud.3-ModManagers.zip`.
ZIP SHA256 `23108FB89030F723A1D396401E361F345590339F9BC9CACA5621A8E435204203`;
ASI SHA256 `7156C8B74FB5D5F32BCDCDEDD45D7706CBCDEB96AC240EF4EEEAE62397CB7D0B`.
The private package has `ShowDetails=1`, `ShowAmbient=1` and `OcclusionTest=1`; the old
Research keys remain off because the HUD switch starts the bounded in-memory run itself.
Package validator and all 25 native tests pass. CrimsonHue is untouched. `hud.1` and
`hud.2` are superseded but preserved; `hud.2+` removes one avoidable 16.25 MiB CPU copy
per fresh volume, while `hud.3` opens the diagnostic panel without an INI edit.

**Ambient: parked, not abandoned.** `localEnvironmentAmbientEstimateWorking` on
`/v1/ambient` is `skyMean x cameraSkyVisibility` with both raw inputs retained. Full-day
range 34500x, barn traverse 15550x on visibility with the sky term steady, value follows
camera POSITION not view direction. What is missing is the mapping to lamp brightness: a
visibly open barn still reads 0.000025 inside, so a linear product drives a lamp to
black. That curve is CrimsonHue's, not telemetry's.

**OPEN / evidence limit, unchanged:** contextBefore/contextAfter in the R16 metadata are
CPU observations around the copy, NOT GPU-bound GI constants. The measured transaction
rate is 0.31–0.48/s against a configured 1 s, because arming needs the pinned resource
identity to recur and `Discover` only re-pins while the phase is Idle; a 30 s stall was
observed once. Known, deliberately not fixed. No source-visibility verdict and no new
public API are published; the HUD result is explicitly a local diagnostic.
At 0.31-0.48 fresh volumes per second it is too slow for a responsive production
occlusion gate; the HUD is for measuring correctness and cost. A product path needs a
smaller/faster readback or GPU-side evaluation.

**ONE next step:** after shutting down the game, package/install this HUD build and repeat
the visible → occluded → visible torch sequence while aiming at the same rendered light.
Require at least three fresh SDF volumes per pose. That closes the retained-repeatability
gap without t224 or PIX.

---

# Previous checkpoint — t233 Variant A gate, 2026-09-10, Codex (history)

**Product question remains OPEN.** Ambient is parked. No t224 investigation and no
new PIX acquisition. Follow [SDF_VARIANT_A.md](SDF_VARIANT_A.md) for the bounded
test, established mapping anchors, inference limits and stopping rule. The older
checkpoints below are history, including their superseded replay-copy next steps.

**ESTABLISHED live, latest:** `sdf-probe.4` ran in supported PID1668 (started
22:46:06 CEST), triggered 22:48:33 with progressing API sequence 3563 -> 3565;
later control 26286. It recorded 13 distinct tuples for resource `139FF33E0`.
The complete COMPUTE-list release is **layout 6 -> 1, access 0x80 -> 0x80000000,
sync 0x80 -> 0, flags 0, subresources (UINT_MAX,0,0,0,0,0)**. This is exactly the
existing R8 copy guard's tuple. Readback belongs BEFORE forwarding that release;
NO_ACCESS/NONE forbids a subsequent copy in the same ExecuteCommandLists scope.
Evidence: `artifacts/light-research/sdf-transitions-pid1668-20260910-224833/`.
The game's installed ASI is still probe.4; no R16 has been copied live yet.

**Completed implementation:** private `SignedDistanceReadback=1` mode discovers
the shape-matched SDF on that compute release, requires a known list Reset and
fresh preceding CPU context, and uses the existing guarded copy/Close/Submit/fence
path. Readback now accepts exactly the existing R8 or the R16 signature and packs
their respective row bytes. Every completed R16 copy is saved immediately as
`bin64/signed-distance-PID-STARTTICK-N.bin` plus `.json`; one is retained in memory.
The package bounds the series to 120 copies at >=2000 ms, about 1.90 GiB total.
Do not run an acquisition until the intended stationary calibration scene is ready.

**Ready, NOT installed:**
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-sdf-readback.1-ModManagers.zip`.
ZIP SHA256 `911BED78B5CDEF35C929B87475109C8AD8CD537E57E5B7736D7220728C8D9F18`;
ASI SHA256 `3B2AFC835E05A12AC61AD7A14C4F18DE8F0792A92B88B05A3DC0058D85FD3056`.
Release build, five spatial CTests and package validation/negative cases passed.
The expanded WARP test verifies EVERY R8/R16 byte on DIRECT and COMPUTE queues,
unchanged engine barriers, a held GPU fence and rejection cases with zero debug
warnings/errors. The adapter test rejects graphics lists, stale context and unknown
Reset before accepting the measured compute release. These are synthetic controls,
not live R16 acquisition or occlusion evidence.

**OPEN / evidence limit:** contextBefore/contextAfter are CPU observations around
the copy, NOT GPU-bound GI constants. Metadata records the bracket, frames/ticks,
raw GI/scene, footprint, resource, release and fence. Mapping stability, borders,
sign and distance scale still need calibration. No R16 decoder or Variant A trace
has been implemented; no source-visibility verdict or new API is published.

**One next step:** after installing the ready R16 package through DMM with the game
closed, acquire its first completed live R16 snapshot at a known stationary wall/
torch scene. Check the new binary/metadata and mapping stability before tracing.
The user requested this checkpoint to conserve the remaining five-hour usage
window; no further restart/test was requested in this session.

**Previous acquisition failure:** `sdf-probe.3` was installed and triggered in supported
PID15044 (started 22:32:58 CEST); API sequence progressed 4703 -> 4736 before the
request and reached 6003 afterward. The first sighting was resource `12BE75D10`,
list `22864FEA0`: layout **1 -> 3**, access **0 -> 0x10**, sync **1 -> 0x80**.
That is GENERIC_READ -> UNORDERED_ACCESS, COMMON -> UAV, ALL -> COMPUTE_SHADING:
a write entry, not the expected SRV release. No R16 copy was made.

**Diagnostic failure isolated:** the old observer records only the FIRST barrier
of a shape-matched resource, then increments a count while discarding every later
tuple. Thus probe.3 cannot answer the pending release question when it discovers
a write entry first. This does not falsify Variant A. Evidence, including installed
hash, INI, native log and before/after telemetry:
`artifacts/light-research/sdf-access-pid15044-20260910-223544/`.

The subsequent distinct-transition observer was built to use an
actually observed complete SRV release tuple for the fenced R16 acquisition.
The code retains up to 32 distinct tuples, flags/subresources, list type, first/last
time and counts; logs new tuples immediately; and continues observing known
resources after the discovery budget ends. Contention/overflow are explicit.
Five spatial CTests pass, including a regression reproducing the live failure.

Installed and measured: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-sdf-probe.4-ModManagers.zip`.
ZIP SHA256 `FDD375F0992E0DD17CC0CE56933E3A3CD78819EB85A6843BD92D24CD6287FAEF`;
ASI SHA256 `985C0500FBE8BB7AC7E7B6CAC974B147F0047D4389A5F7D117F00C007F8B4C87`.
Release build and package validation (including negative cases) passed. Same private
INI switches as probe.3. Preserved as an immutable package.

---

# Historical cold handover for Claude — 2026-09-09, Codex/Astra

## Required read order

1. `docs/GPU_CAPTURE_FORENSICS.md` — authoritative current GPU/PIX state.
2. `docs/TOOLING.md` — use the existing tools; do not rediscover them.
3. `docs/HANDOVER.md` — product/runtime context and current checkpoints.
4. `docs/LOCAL_ILLUMINATION_RESEARCH.md` — deep evidence and history only;
   later sections may preserve superseded conclusions.

Do not continue from a historical claim when `GPU_CAPTURE_FORENSICS.md` marks it as
hypothesis, withdrawn or superseded.

The user requested a handover for Claude after a long absence. Claude built the
pairing path up to readback.4 and then ran a five-point occlusion series with it.
**Current state: the spatial sample is confirmed to respond to local enclosure
across roughly four orders of magnitude, its reference is exactly the camera
position, the cheap CPU shortcut is proven ill-conditioned, and the plateaus are amortised block refresh of the voxel
volume, and a native sampler reproduces the offline model exactly in
game, 104 of 104 values, in a 1465-byte payload that already shows the doorway
effect** (current checkpoint below). readback.4 is the ASI in
the game folder; readback.5 fixes a diagnostic counter defect and is built but not
installed. No publication or push was performed for any package.

## READ THIS FIRST if you are picking up the GPU capture work

**`docs/GPU_CAPTURE_FORENSICS.md` is self-contained and current.** It holds the
toolchain for reading a PIX capture offline, the resource and shader identities, a
ledger separating what is established from what is hypothesis from what was
WITHDRAWN, the open questions in priority order, and the traps. Read it instead of the
checkpoints below, which record how the conclusions were reached in the order they
were reached -- including several that were later retracted. Come back here for the
in-game probe, the ASI, the packaging and the occlusion history.

**As of 2026-09-10 the immediate next action is in section 4b of that document**: a
built but not yet installed package, `v2.0.1-sdf-probe.3`, whose run prints the last
unknown before the signed-distance copy path can be written. Ambient is validated and
PARKED; using the existing light feed for occlusion is a measured negative and closed.

The short version, as of 2026-09-09:

- Four scripts turn the 2.7 GB export into provable statements about the frame:
  `Read-PixExportResource.py`, `Map-PixExportShaders.py`,
  `Find-PixExportDispatches.py` (candidates only) and `Resolve-PixExportBindings.py`
  (the one to trust). 123 unit tests.
- The engine computes `saturate(1 - sample)` on the sky-visibility volume itself and
  multiplies an environment cube by it. Our decode convention is the engine's.
- The ambient light is a 1024-byte buffer of three colour channels times nine
  coefficients, with the basis order proven from the producer's own constants.
- The producer applies NO solid-angle weight, so those are moments against the L0-L2
  basis rather than spherical harmonic coefficients.
- Whether its six partial entries are cube faces or six frames is **open**, and the
  capture in hand cannot settle it because that shader did not execute in it.
- The bytes extracted from the export are a replay's serialised initial payload.
  **Do not use them for semantic validation** until their position in the frame is
  established.
- The capture now proves `t233/resource 191 -> GenerateAxisAlignedDistancePass0/1
  -> t224/resource 211`. t224 is a coarse helper derived from the SDF, not an
  independent occupancy truth.

### Current product checkpoint — ambient plus source occlusion

The private local-ambient candidate is deliberately named
`localEnvironmentAmbientEstimateWorking`: raw global-sky RGB multiplied by raw
`cameraSkyVisibility`, with both raw inputs, the derived RGB, source
frame/timestamp/age and availability/staleness retained. It is a useful product
estimate, not an exact renderer term. Do not normalise by the observed open-sky
value near 0.53 and do not import the renderer's internal `0.03125`. This is not yet
a public API schema.

For camera-to-fire/lamp occlusion, test the fine SDF alone first. The ordered
variants are A: pure t233 sphere tracing; B': the reconstructed engine structure
(t233 stepping/coverage plus the t224.w gate and t224.xyz axis distances); C: a
length-weighted custom method only if A/B' expose a concrete weakness; D: t224 alone
as a low-priority control; E: t224 as a final blocker decision, currently
unsupported. If A handles the labelled behind-wall cases robustly, stop there.

The next evidence step is a paired replay readback immediately after GlobalId 749,
the second `GenerateAxisAlignedDistancePass1_CS` dispatch, before Resource 191 is
written again. Instrument `PopulateCommandList_21556_1_2()` at that exact point,
transition Resource 191 from shader-resource and 211 from unordered-access to
`COPY_SOURCE`, copy their `GetCopyableFootprints` into readback buffers, restore the
layouts, then map after the queue fence. The event-state bytes have not yet been
acquired; `resources.bin` is not a substitute.

## What changed since the old fire/console investigation

- Product home is **C:\DEV\CrimsonDesertTelemetry**. Console/research capabilities,
  telemetry, camera, ManyLights capture and overlays now live in ONE ASI package.
  Do not restore the archived standalone console ASI or install two old plugins.
  `C:\DEV\CrimsonHue` is now the separate working Hue consumer (own repository),
  not the lighting research workspace. It uses only the neutral telemetry API.
- Public baseline is **2.0.0**, released on GitHub/Nexus and user-tested from the
  GitHub ZIP. All `2.0.1-*` packages discussed here are private development builds.
  The local-light path is no longer blocked on the old sound/emitter pool search.
- Existing API provides player/camera, authored lights and current filtered
  ManyLights contributions, including position, linear RGB/luminance and decoded
  point/spot direction/cone where present. HTTP AND WebSocket still exist. The
  fullscreen markers and larger 3D mini-radar work; they do NOT perform depth tests.
- Subsequent private builds add spatially grouped, temporally smoothed local
  lights for Hue, keeping raw contributions, and a separate global sky SH stream.
  The latest diagnostic ZIP includes these feeds; no second preview ASI is needed.
  Grouping is approximate, not engine object identity; rendered sample indices
  are not stable IDs. ManyLights is view-filtered, not a complete 360-degree census.

## What the user now needs / what is NOT solved

Keep raw and smoothed lights, but ALSO provide (1) local environmental illumination
under roofs/in caves and (2) a separately visibility-filtered light stream.
Global sky does not include local roof occlusion or separately measured direct
sun/moon lighting. Source visibility, light reaching the player, and brightness
of visible surfaces are different quantities. Missing/culled is not proven OFF.
Do not invent a local-ambient schema or make global sky times one scalar into a
validated room-brightness measurement. Do not silently filter existing streams.

Current bounded research is on the game's **spatial sky-visibility sample**:
AdaptExposureCS samples a voxel texture and uses `saturate(1 - sample)`; a GI
consumer also uses that quantity for an environment contribution. Native producer
and CPU controls identify its reference as view/camera context, NOT player root.
Fallback can yield 1 without texture coverage. This is not total local lighting
and not camera-to-lamp occlusion. The separate hiZ route is documented, not built.

**Measured in game:** readback.1 copied the actual voxel texture after a validated
release barrier and completed queue fence. A direct sample closely matched the
inverse of a CPU exposure-cache value, but that cache had unknown GPU age.
readback.2 then added GPU GI/exposure copies for the same selected native dispatch
and WAS live-tested: the selected dispatch was observed, but the required root
CBV/UAV bindings were absent inside its window, so it copied nothing. That valid
negative disproves the root-descriptor assumption, not the texture result.
readback.3 widened that window to the whole command-list recording and WAS
live-tested: it saw 20 root sets before the exposure and 98 descriptor-table
bindings, with seven tables live at the dispatch and still no root UAV or SRV.
The binding path is therefore descriptor tables, which no root-descriptor guard
can ever accept. Do not run a fourth capture of the same kind.
readback.4 dropped the root requirement, paired by submission instead, copied
whole buffers and WAS live-tested successfully: texture, GI and exposure in one
fenced transaction, with the GI window located offline at offset0 and the GPU
bytes equal to the CPU copy. The exposure window was not found, so that run has
no inverse. One outdoor night sample exists; it is not yet a controlled result.
Do not restart the old emitter, generic heap scan, broad GI or PIX search.

## Tooling

Read [TOOLING.md](TOOLING.md) before reaching for a GPU capture, a native build or
a repository script. The single highest-leverage entry there:
`pixtool open-capture <wpix> export-to-cpp <dir>` reconstructs a whole capture as
greppable C++ including every resource description and view, and answered in
minutes what shader disassembly had spent hours narrowing down.

## Short reading route — do not read all historical checkpoints

1. Read the current package checkpoint immediately below (hashes, safety, tests).
2. [LOCAL_ILLUMINATION_RESEARCH.md](LOCAL_ILLUMINATION_RESEARCH.md): first two
   sections, "GPU pairing implementation" and "First direct live texture readback".
   They contain code/native anchors, raw evidence paths and interpretation limits.
   If needed, follow "New result: an existing spatial sky-visibility sample is
   recoverable" / "Native spatial provenance" for the shader/math and CPU source;
   "Separate depth-resource route" is the deferred per-source visibility lead.
3. Only when changing public consumers, use [API.md](API.md),
   [SMOOTHED_LIGHTS.md](SMOOTHED_LIGHTS.md) and [AMBIENT_STREAM.md](AMBIENT_STREAM.md).
   Older first-package instructions are historical; use the current ZIP below.

Implementation entry points: `native/CrimsonDesertTelemetry.Asi/src/spatial_probe.cpp`
(selection, native binding observers, private report), `spatial_readback.cpp/.h`
(resource lifetime, copies/fence), `tests/spatial_pair_tests.cpp` under that native
project (real WARP/COM-detour controls), and `scripts/Decode-SpatialReadback.py`
plus `Decode-ExposureContext.py` (existing shader model). Existing public readers
are in `src/CrimsonDesertTelemetry.Core`: RenderLightReader, SmoothedLights,
SkyAmbientReader. Do not replace those feeds to add this diagnostic.

## Resume recipe — ONE test with the readback.3 ZIP

readback.2's one-shot is consumed and re-running it would reject identically, so
this recipe now applies to the readback.3 package below. Have the user close the
game and install the WHOLE ready ZIP via DMM. Never replace
an active ASI. Verify the installed ASI hash against the checkpoint, actual new PID,
native log and `http://127.0.0.1:27311/v1/health` (playing, supported, progressing).
All old process/resource addresses below are historical, not reusable pointers.
Exact supported EXE SHA256:
`4D99C15C58BD20A94D354D10AE395D1FAC777D59EF52CBA8080DC3FC8DC6F454`
(Steam build25116796). Instrumentation must fail closed on another build.

Use `[Research] SpatialProbe=1, SpatialReadback=1, AmbientProbe=0`, with existing
`[Lights] Enabled=1, ManyLights=1` and `[Ambient] Enabled=1`. Spatial defaults in
the ZIP are OFF. Request a short standstill, then run in PowerShell7.4+:
`& C:\DEV\CrimsonDesertTelemetry\scripts\Start-SpatialProbe.ps1 -ProcessId ACTUAL_PID`
Replace ACTUAL_PID with the verified PID; this signals the existing plugin event.
ONE GPU transaction per process, INCLUDING failure; repeated signals cannot retry.
Expect 20 CPU controls at up to2Hz, bounded by30s. Release the user after recording.
Last location was the partly open roofed stall between four fire lamps, at NIGHT,
with user-reported indoors brighter than outside. No toggle or movement needed
for this pairing test; it is not yet an indoor/outdoor comparison.

Preserve the NEW `spatial-binding-PID-*.json`, native log and INI from the game's
bin64 folder under a fresh `artifacts/light-research/` directory. Decode using
`scripts/Decode-SpatialReadback.py --input RAW --out DERIVED --assume-adapt-exposure-layout`.
Real Python here is
`C:\Users\fabia\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe`.
Check native completion/fence, root bindings, paired flags and progressing CPU
controls BEFORE interpreting the direct/inverse difference. Failure is a diagnostic,
not zero light. Exact pass/reject follow-up and package identities are below.

## Current checkpoint — loop closed in game, doorway effect measured, 2026-09-09

Live series in PID4760 with 2.0.1-spatial-sampler (ASI
DB90212BF66A91EA1ADFBD4690F57608E53998DF541CEDD0A61BE340E90E81B9), standing at the
player home door in a part of the world never sampled before (x about -10405
against the barn's -10530), in daylight. 8 of 8 transactions, values 0.1587..0.2693
— the mid range, where a comparison tests something.

**EVERY NATIVE VALUE MATCHES THE OFFLINE RECOMPUTATION: 104 of 104.** Thirteen per
transaction, recomputed by the decoder from the same volume and constants and
compared without tolerance; `agrees: true` in all eight, no disagreements. The
native and offline evaluations are the same computation on real data.

**The payload is 1465 bytes against 540672 for the volume**, about 369x smaller.
That is what the native sampler was for.

**THE DOORWAY PROFILE IS THE EFFECT THE PRODUCT NEEDS.** From transaction 0 at
reference 0.1587: into the building (+Z) 0.005212 at 2gu and 0.000661 at 5gu, the
opposite direction (-Z) 0.374170 and 0.317952, upward 0.314600 and 0.465113, the
ground below exactly 0.000000. A lamp mapped to +Z goes dark while one mapped to
-Z or upward stays lit, from a single capture and with no new mechanism.

Limits unchanged: offsets reuse the reference clipmap, may read voxels of differing
age, and the value is a candidate sky-visibility factor, not irradiance or room
brightness. Series spread 0.1107 against 0.0156 under open sky, consistent with a
steeper field beside a building rather than worse precision.

Evidence artifacts/light-research/series-20260909/run4-playerhome-door-pid4760/.

**INTERPRETATION, added the same day:** the quantity is most likely what its name
says — sky visibility from a point, reduced by ground, buildings, roofs, trees,
clouds and anything else in the volume. The form that fits is the fraction of the
FULL SPHERE from which sky is reachable, which explains ~0.53 in the open (the
lower half is ground), the rise to 0.562 over 10 gu of height, the exact zeros
below the surface, and 0.000661 pointing into a building. So the value not reaching
1 under open sky is correct rather than a shortfall. This is a reading that fits,
not a measured fact; nothing isolates a cloud or a tree, and confirming the
solid-angle form needs a point far from every surface, which cannot be reached by
offsetting because Y wraps.

**HARD BOUND: offsets wrap toroidally.** Y covers 32 game units, X and Z 64 each.
Sampling +15 or +20 gu in Y returns exactly 0.0000 and +30 returns precisely the
value of -2. The recorded ±2 and ±5 pattern is safely inside; the contract must
state the bound or a consumer will ask for ±20 and get unrelated space.

**CONTRACT DRAFTED, and a surprise:** docs/SKY_VISIBILITY_STREAM.md fixes the shape
and every promise before code. Default pattern is 26 directions at radii 3 and 6 gu,
52 self-describing samples, each carrying its own offset. While writing its scope
section, the assumption that per-source occlusion needs a DIFFERENT technique was
tested offline and did not hold: marching the segment from the camera to a light
8.2 gu away gave a minimum of 0.000000 along the way, against 0.285 and 0.139 for
two clear controls of the same length. A volume march may therefore answer fire
occlusion too, reusing everything already built, and unlike a depth test it works
off screen, which is exactly the product case. One example only; see "Segment
occlusion" in LOCAL_ILLUMINATION_RESEARCH for what it does not establish and for
the per-point clipmap selection that segments beyond ~32 gu would need.

**OCCLUSION IS NOW THE PRIORITY** (user's decision; publishing a stream is not the
goal here, recording state in the repo is). Two offline steps already taken:

Per-point clipmap selection is solved, which was the range blocker. `wrapped`
resolves to the world position measured from a common anchor —
`relative[level]/originScale[level]` is identical for levels1..3 — so the shader's
own cell test can be run for any point. Verified: the reference still selects
clipmap1 and reproduces 0.158708192 exactly, and lights at 71..92 gu that were
previously out of reach now select clipmap3 with plausible values.

A 100-segment survey over every light in every preserved capture is strongly
bimodal: 37 below 0.01, 57 above 0.05, only 6 between. Distance does not explain it
— an 8.2 gu segment read 0.000000 while a 90.3 gu one read 0.082124. But this shows
only that the statistic separates; it does NOT show which mode means occluded,
because no capture recorded actual visibility, and ManyLights inclusion is not
proof of visibility.

Two flaws to fix before any threshold: the march silently skips points no clipmap
covers, so a segment can be judged on partial coverage, and long segments change
resolution partway as they cross levels.

**THE A-B-A RAN AND THE TEST HELD.** A lantern on the player home wall, the user
stepping out of and back into its line of sight inside one capture, with a 5 Hz
position track alongside. The walk was smaller than intended (3.7 gu, third leg
outside the window), so the volumes were used cross-wise: march from BOTH camera
positions through EVERY volume. From camera A the segment reads 0.108..0.121; from
camera B, 4.67 gu away, it reads **0.000000 in all eight volumes**. Since the volume
refreshes between transactions, holding across all eight makes the difference
geometric rather than temporal. **The user confirmed B was the occluded position**,
so this is the first occlusion result with ground truth rather than inference.

Also measured: the lantern stayed in the RENDERED ManyLights feed for 126 of 127
track rows, including the whole time it was hidden. Feed membership is therefore
confirmed useless as a visibility signal, which the research had warned about
without a measurement behind it until now.

Not established: any threshold, false-positive or false-negative rates, partial
occlusion, doorways, foliage, thin walls, grazing angles, or anything at the
coarser clipmap levels a distant source would use — this case sat at 16..17 gu,
entirely in clipmap 1. The two survey flaws are still unfixed: uncovered points are
silently skipped and resolution changes mid-segment. The survey also used the
authored light array rather than the rendered feed and should be re-run.

**THE METHOD IS REFUTED AS A GENERAL TEST, by our own data.** An independent
review pointed out that the field is sky visibility, not occupancy, so free air
inside an enclosure can read as low as solid. It does: the camera under the barn
roof stood in FREE AIR and marching outward, still in free air, reaches 0.0001 —
below the 0.000661 measured inside a wall. The lantern case worked only because
both endpoints were outdoors. Indoors, and therefore in a cave, both ends read near
zero and a naive minimum reports "blocked" for everything.

Fixed in response: the marcher now lives once in
`Decode-SpatialReadback.march_segment`, steps at 0.5 gu instead of 0.25, excludes
endpoint neighbourhoods, COUNTS uncovered points into `unknown-uncovered` rather
than skipping them, requires a connected low run of real thickness, and returns
`unknown-enclosed` when both ends already see no sky. Re-run against the RENDERED
feed (the earlier survey used the authored array), 302 segments give 119 blocked,
78 clear and 105 honestly unknown. The validated lantern still resolves correctly.
52 Python tests, six new, one encoding the measured failure mode.

**THE MOMENT TEST IS DONE, OFFLINE, AND THE PREDICTED LAYOUT HELD.** No game run
was needed: the `export-to-cpp` tree carries the frame's real resource contents.
`resources.bin` has no index -- it is a concatenation of XPRESS blocks read in
program order -- so `scripts/Read-PixExportResource.py` reconstructs that order from
the generated source and decompresses one block. The capture holds exactly THREE
1024-byte CBVs; one is a camera (its direction squares to 1.0000), one is a list of
world-space pairs, and the third, resource 15739, is the ambient.

Its bytes fall into three channels of nine exactly as DXC's array sizes predicted
before anything was read.

**THE BASIS ORDER IS SETTLED FROM THE PRODUCER, no consumer needed.** Each lane is
multiplied by a literal constant and all six distinct constants are the textbook real
SH normalisations to float32 precision (worst deviation 1.2e-06):

```
lane 0  0.2820950                          Y00   (= 0.5*sqrt(1/pi))
lane 1  -0.4886030 * d.y                   Y1,-1
lane 2  +0.4886030 * d.z                   Y1,0
lane 3  -0.4886030 * d.x                   Y1,1
lane 4  +1.0925480 * d.x * d.y             Y2,-2
lane 5  -1.0925480 * d.y * d.z             Y2,-1
lane 6  0.9461759 * d.z^2 - 0.3153920      Y2,0     polar axis is z
lane 7  -1.0925480 * d.x * d.z             Y2,1
lane 8  0.5462740 * (d.x^2 - d.y^2)        Y2,2
```

Lane 0 is Y00 because it carries Y00's own constant -- an identification, not the
weaker "it is the largest". Several real-SH sign conventions exist, so the engine's
is simply the one listed above rather than "the usual" one.

**BUT THERE IS NO SOLID-ANGLE WEIGHT.** A review asked what weights each of the 4096
samples. Nothing does. The inner loop is: NDC point -> matrix rows 30..33 with a
perspective divide -> normalise -> textureLoad `g_texSkyInscatter` mip 0 -> clamp
non-negative -> multiply by the nine basis functions -> add. No Jacobian, no per-pixel
solid angle, no cosine. A uniform grid in projected space is not uniform in solid
angle, so these are **projection-weighted moments against the L0-L2 basis, not
canonical SH coefficients of a spherical radiance field.** The basis is exactly real
SH; the measure is missing.

**The 1/6144 is not arbitrary, and it points at cube faces.** Each entry samples
4096 directions -- proven twice: 16x16 threads with `threadId << 2` and 4x4 loops, and
the NDC scale 1/32 needing 64 steps to span [-1,1]. Six entries is therefore
N = 24576 = 6 * 64 * 64, the count of a 64x64 cube map. **The leading reading has no
pi in it: `1/6144 = (4/4096)/6`**, where 4/4096 is exactly the uniform Riemann weight
of one cell of a 64x64 grid over the area-4 square [-1,+1]^2. So the normalisation is
**exactly compatible with six equally weighted 64x64 integrals over [-1,+1]^2**; IF
the six are cube faces that is very natural, and it would explain the absent Jacobian
outright (integration in the face parameter space, not the spherical measure). A
hypothesis with an exact numerical fit, not a finding -- it presupposes what is still
open. (The factor also equals (4*pi/24576)/pi, numerically true, semantically
unsupported.) **The Lambertian reading of that 1/pi is
WITHDRAWN** -- a cosine convolution is band-dependent (1, 2/3, 1/4 after dividing by
pi), so one global scalar cannot be it, and searching all 79 listings finds NO band
factors and none of the Ramamoorthi constants. Nor does the constant prove a sphere:
1/6144 also factors as (2/3)/4096. Honest statement: **the normalisation is consistent
with six jointly evaluated 64x64 projection faces and notably compatible with a cube
map**; 6 x 1024 is refuted, and that is all. Certain about the Jacobian: every sample
in this shader carries the same scalar weight with no position-dependent correction.
IF the six are faces, that approximates a spherical integral, since a cube face's
per-texel solid angle varies by 3^(3/2) = 5.2 -- unless `g_texSkyInscatter` is already
pre-weighted, which needs its producer to exclude.

Accordingly the directional result is stated as: the red L1 band is strongly oriented
toward +y (`(-0.000601, +0.005414, +0.000620)`), by an order of magnitude. Calling it
the physical first moment of the sky radiance needs the measure to be right.

**AND THE BUFFER HOLDS SIX PARTIAL CONTRIBUTIONS.** The shader's tail was misread
before. Thread 0 scales the 27 reduced values by 1/6144 and stores them at
`_renderFlags.x * 8 + 8`; a six-iteration loop then rawBufferLoads `i*8 + 8` for
i in 0..5, sums all 27, and stores that sum UNSCALED to slots 0..6.

```
slots  0..6   the sum of the six partial entries, unscaled
slot   7      a separate summary, huge and unexplained (16366681.0, ...)
slots  8..55  SIX ENTRIES, _renderFlags.x selecting the write slot
slots 56..63  outside those six, contents unknown
```

**Cube faces or six frames is the open question, and THIS CAPTURE CANNOT SETTLE IT.**
Settling it needs `_renderFlags.x` and the six matrices, which needs the dispatches
located, which needs a PSO-to-shader mapping -- so `scripts/Map-PixExportShaders.py`
was built. `CreatePSOs.cpp` reads each pipeline state's bytecode from resources.bin
and a DXIL container keeps its entry name as a NUL-terminated string, so the same
read-order walk names them: **236 of this capture's 287 compute pipeline states**.
(The engine names entries either with a stage suffix, `RenderDiffuseCS`, or a
lowercase prefix, `csPrecomputeAmbient`; matching only the first drops the whole
atmospheric family.)

**A direct byte search across all 287 blobs finds no
`GenerateAmbientFromEnvironmentAtmosphericScatteringCS`**, while every sibling is
there: csPrecomputeAmbient 22283, csRenderAtmosphericScattering 22314 and 22337,
GenerateAtmosphericScatteringDispatchIndirectArgumentsCS 22313, SkyMaterialCS 22306,
EvaluateDiffuseRadianceCS 22408/22409, RenderDiffuseTiledCS 22565/22566. Proven
narrowly: **the producer was not executed in this capture** -- consistent with a rarer
or dirty-driven update, but it could also have run before the capture or on a region
it does not cover.

**It does NOT explain the zeros.** The capture creates **14 UAV descriptors over
resource 15739**, each `FirstElement 0, NumElements 64, StructureByteStride 16` -- the
whole 1024 bytes. `Find-PixExportDispatches.py` plus the PSO map named the one dispatch
binding a table based on them: ClearVoxelsBufferCS, pso 22274.

**Then reading that shader refuted it.** `Map-PixExportShaders.py --extract` pulls a
pipeline state's DXBC out of resources.bin -- **the shader the frame ACTUALLY ran, not
an archive variant**, which removes a standing caveat on every .ll here -- and DXC
disassembles it. PSO 22274 binds `g_voxelHeadIndexBufferUAV` (u5), 
`g_voxelGeneratedFlagsUAV` (u6) and `g_voxelIndexListBufferUAV` (u15), writing i32
zeros into voxel bookkeeping. None is the ambient buffer. The table's BASE views it,
but the shader's registers resolve elsewhere in the range -- a false positive, and what
"can reach" means in practice. Attributing a register to a descriptor needs the root
signature's range mapping, which is not done.

**THEN ROOT SIGNATURE RESOLUTION FOUND THE WRITER, in this capture after all.**
`scripts/Resolve-PixExportBindings.py` walks the real chain -- shader register ->
root parameter and descriptor range -> OFFSET_APPEND resolved against the preceding
ranges -> heap index -> the descriptor AS IT STOOD then -> resource. Time accuracy is
required: RenderFrameWorker interleaves 10307 one-descriptor ModifyDescriptors calls
with the command list population.

Regression test, ClearVoxelsBufferCS pso 22274: u5 -> resource 15789, u6 -> 206,
u15 -> 15787, none of them 15739, and at slots far from the table bases the old
heuristic matched. Inverting it over all 994 dispatches gives exactly one UAV hit on
15739: **pso 22283, UAV register 2 space 39, `csPrecomputeAmbient`** -- precisely
where the SH producer declares `g_texPrecomputedAmbientUAV`. Extracting and
disassembling pso 22283 confirms the same name at the same register. Stated exactly:
**csPrecomputeAmbient is proven against resource 15739 here; the SH producer declares
the same logical UAV name, register and layout but did not execute, so ITS binding to
that resource is not proven from this capture.**

`csPrecomputeAmbient` writes ONE float4, at **index 56**: the sky's Mie scattering
reduced over 256 threads, scaled by `4*pi/256`. **The sampler was traced rather than
assumed** -- a Hammersley set, `z = 1 - i/256`, azimuth from `bitReverse32(i) *
2*pi/2^32`, `dir = (r cos, r sin, z)`. Uniform AREA, low discrepancy, no Jacobian
missing. These directions cover only the upper hemisphere, whose natural weight is
`2*pi/256`; why this Mie-summary store uses the extra factor two is **open**. No
antipodal or symmetry operation has been demonstrated, so this does not establish
canonical spherical quadrature for the Ambient/SH producer. The Mie summary's
applied total scale is `4*pi`; the projected SH grid's is `4`, but they are different
stores and domains.

**The state that looked impossible has a plausible account, not an explanation.** Slot
56 is written here by csPrecomputeAmbient as Mie RGB, and slots 0..6 / 8..55 belong to
the absent producer. But the extracted bytes are the replay payload whose position
relative to that store is exactly what is unknown -- if it precedes the capture's
commands, the store cannot have produced them. And "slots 0..6 predate the capture"
searched compute dispatches only; copies, CPU uploads and graphics-stage writes were
not. A full write history for 15739 would settle both.

**The cache is NOT a second SH buffer** -- guess withdrawn. `u3, space39` resolves to
resource 15741, `NumElements 1024, StructureByteStride 16`, 16 KB in the same heap.
Its stores go to `threadId.x * 4`, `|2` and `|3`: a 4-float4 stride, 256 entries, one
per thread, colour triples with flags. Not a 7+1 harmonic packing.

**What the next capture must contain:** a dispatch of the ambient producer. Then per
dispatch resolve the root CBV holding GlobalPushConstants for `_renderFlags.x`, and
rows 30..33 of the bound SceneConstantBuffer to unproject. Six centre directions near
the axes are suggestive but not enough: unproject the four NDC CORNERS too, since a
real `+X` cube face gives corner rays proportional to `(+1, +-1, +-1)`, with square
aspect and a 90 degree opening following from the same rays. **Faces and frames need
not be exclusive** -- if `_renderFlags.x` cycles 0..5 across frames, the six could be
spatially the faces and temporally amortised, one refreshed per update, which is how
an engine would spread this cost. Note the export records **no
SetComputeRoot32BitConstants at all** -- GlobalPushConstants is a root CBV by address
despite the name, which is also why the `--list-cbv` count is not an inventory.

**Two retractions.** Slot 56 is NOT "a bare RGB triple corroborating the hue" -- it
lies outside the six and its meaning is unknown; that corroboration is withdrawn as a
misreading. And the extracted bytes are not a self-consistent frame: set 0 is the sum
of sets 1..6, yet those read all-zero while set 0 is populated. No executed frame
looks like that, so what `resources.bin` yields is **the serialised initial resource
payload of a replay export**, not live buffer contents, and its position relative to
the target dispatch is unestablished. **Do not use these bytes for semantic
validation until that is pinned down.** Untouched, because they come from the shader
and not the bytes: the 3 x 9 split, the basis order, the packing, the normalisation
identity.

**The hard test, needing no new capture:** extract `g_texSkyInscatter` from this same
frame, reproduce the 4096-sample projection offline with the constants above and the
same 1/6144, and compare all 27 values. Expect close numerical agreement, not bit
equality -- a parallel reduction sums in a different order. That needs texture
footprint work (row pitch, subresources, format) which the buffer path did not, so
the reader is NOT yet a general texture decoder. 123 script tests pass.

**The ambient half, in detail: 1024 bytes.**
`GenerateAmbientFromEnvironmentAtmosphericScatteringCS` (in
`artifacts/light-research/ambient-check-20260907-sky-ca7a87f5.ll`, unopened since
2026-09-07) integrates the sky inscatter LUT into **L2 spherical harmonics, RGB** —
the groupshared `g_sharedEnvironmentSH` is typed `SHColor2` and splits into 4+4+1
floats per channel, nine coefficients. It writes seven `float4` plus an eighth
summary slot into a `RWStructuredBuffer<float4>` (u2, space39). Other shaders read
the SAME bytes as `PrecomputedAmbientConstantBuffer`, declared `{8 x float4,
[56 x float4]}` with DXC annotating the handle `{13, 1024}` — **1024 bytes, eight
sets of eight float4, a CBV in space35**, the same space as the 768-byte GI constants
we already snapshot. Consumers across all 79 local listings: the two atmospheric
scattering renderers (slots 7 and 56), the indirect-args shader, `RenderDiffuseCS`,
`RenderDiffuseTiledCS` and `EvaluateDiffuseRadianceCS` (slot 7).

The SH already contains atmospheric extinction and cloud shadow (it samples
`g_texNetDensity` and `g_texCloudVolumeShadow`); what it lacks is LOCAL scene
occlusion. The layout match between producer UAV and CBV is exact but the transfer
between them is not shown — treat them as layout-compatible, not identical, until a
capture shows the copy. Unknown: coefficient order within the seven float4, what
`_renderFlags.x` selects among the eight sets, and whether the values are pre- or
post-exposure (`ExposureConstantBuffer`, 80 bytes, read by nearly every lighting
shader here including `ProcessManyLightsCS`).

**AND THE COMPOSITION IS DEMONSTRATED, in `EvaluateDiffuseRadianceCS`
(`gi-entry-b909d0e8.ll`).** It binds everything at once and names three volumes we
had only inferred: **t232 = `g_skyVisibilityVoxelsTexturesLikeUav`, t233 =
`g_signedDistanceVoxelsTexturesLikeUav`, t224 = `g_axisAlignedDistanceTextures`** —
so t233 is a signed distance field by the engine's own naming. The instruction
stream reads:

```
s   = SampleLevel(t232, u, v, w).x
vis = saturate(1 - s) * 0.03125          (fallback branch: bare 0.03125)
env = SampleLevel(t234 g_environmentColor CUBE, -d.x, d.y, -d.z, LOD 4).rgb
out = env * (vis * cb[0].w)
```

`saturate(1 - sample)` is thus the ENGINE'S own expression, not our inference, and
the polarity is fixed: s = 0 is full sky visibility. The w coordinate is scaled by
1/264 — the depth of the 64x32x264 volume that PIX identified as ApiObjectId 190,
tying binding and captured resource together. But note the radiance carrier here is
the environment CUBE, not the SH; whether the SH is ever multiplied by sky
visibility is still unshown.

**The capture now identifies the SDF and its downstream helper.** The time-accurate
resolver proves `t66/resource 191 -> GenerateAxisAlignedDistancePass0_CS ->
u14/resource 211`, followed by Pass1 updating 211 in place. The Evaluate consumer
binds 211 at t224 and 191 at t233. Resource 191 is 128x64x1040 R16_FLOAT; 211 is the
half-resolution 64x32x512 RGBA8_UINT helper.

Captured Pass0 makes t224.w classification bits from 2x2x2 SDF neighbourhoods and
character-occlusion AABBs. Pass1 packs six axis-aligned run distances, clamped to
15, into the xyz nibbles. The archive `RaymarchLocalLightsCS` uses t233 for
distance/stepping/coverage, t224.w shifted right by two as a gate, and t224.xyz as
numeric axis distances. It does not use t224 as the final hit decision, and that
raymarch shader did not itself execute in this capture.

**ONE next step: acquire both textures at the exact post-Pass1 event state**, using
the replay-copy recipe in the current product checkpoint above, then run the pure
t233 sphere-tracing baseline on labelled lamp segments. Do not compare t224 from an
older pass against a newer t233 state, and do not use the serialised initial payload
as event-state evidence.

Superseded plan, kept for context: find the resource UPSTREAM of the R8 texture. It is the result
of a geometric computation, and whatever feeds it — opacity, occupancy, a distance
field — is the right input for line of sight, where voxel-exact DDA traversal would
replace point sampling. Same targeted method as everything so far, in a system we
are already inside. In parallel, a ground-truth labeller from the depth buffer
(`g_hiZMap t15, space36`, already identified) would let competing statistics be
compared on error rates rather than on one lantern. Sky visibility stays the right
quantity for the AMBIENT feed and, for occlusion, a confidence rather than an
answer. Engine raycast, inline DXR and per-light shadow maps are recorded as
alternatives, none adopted. It must state the camera reference, the candidate-not-measurement
framing, the amortisation freshness bound, the clipmap reuse for offsets, the
mixed-age offsets, the toroidal offset bound above, and that the value does not
reach 1 under open sky (~0.53) BY DESIGN rather than by defect. Decide the offset
pattern a consumer receives and whether the plugin publishes continuously rather
than one series per process. Keep the API vendor-neutral. Independent hiZ
per-source visibility remains pending and required.

## Previous checkpoint — sampler wired into the probe, 2026-09-09

PRIVATE **2.0.1-spatial-sampler.2** built and host-tested, NOT game-tested.
ZIP artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-spatial-sampler.2-ModManagers.zip
SHA256 3A8E08D077E3C9140A25AEFFAA8B500A1F80E655D5B3ED0FAAF738E4F30BDF7A;
ASI SHA256 DB90212BF66A91EA1ADFBD4690F57608E53998DF541CEDD0A61BE340E90E81B9
(identical binary to sampler.1; only the packaged INI differs).

**Private packages now SHIP READY TO RUN.** Build-ModManagerPackage.ps1 gained
an -IniOverrides parameter, so this ZIP carries SpatialProbe=1, SpatialReadback=1,
AmbientProbe=0, SpatialReadbackCount=8 and SpatialReadbackIntervalMs=1000. A DMM
install therefore no longer resets the switches, which had already cost one wasted
game start. Two guards keep releases clean: overrides are refused for any version
without a prerelease suffix, and an override key absent from the template throws.
The repo template itself stays at 0. Earlier packages preserved.

Each transaction now records natively sampled values beside the volume, and the
decoder recomputes them. This is the payload shape a feed would carry instead of
540672 bytes.

**In the plugin.** After the fenced copy the probe locates the 768-byte constant
window inside the whole GPU GI buffer exactly as the decoder does: matching the CPU
copy of the same constants, 256-byte aligned, only when unique. Ambiguous or absent
is reported as such and nothing is sampled. With the window found it samples the
reference plus twelve offsets — six axes at 2gu and the same six at 5gu — for a few
hundred bytes of payload.

**In the decoder.** `verify_native` recomputes all thirteen values from the same
volume and constants, compares exactly, and reports
`nativeSampler: {checked, agrees, disagreements}`, listing any disagreement with
its offset and both values. No agreement is claimed when the plugin says the
samples are unavailable.

**Limits unchanged.** Offsets still reuse the reference clipmap and may read voxels
of differing age because the volume refreshes in amortised blocks. The value stays
a candidate engine sky-visibility factor, not irradiance, room brightness or
per-source occlusion; the caveat travels inside the JSON.

24/24 native CTests. 46 Python tests, four new: agreement confirmed, a broken
reference caught, a broken offset caught with its delta identified, and no
agreement claimed when unavailable. Verify-NativeSampler.py still reports 56
comparisons across seven captures with zero mismatches.

**ONE next step:** install this ZIP and take ONE series, then check that
`nativeSampler.agrees` is true for every transaction. That closes the loop in game
and is the last verification before the payload can shrink to the samples alone.
After that, design the public contract, stating the clipmap reuse, the mixed-age
offsets and the amortisation freshness bound. Independent hiZ per-source
visibility remains pending and required.

## Previous checkpoint — native sampler built and verified, 2026-09-09

The evaluation now exists in C++ as well as Python, which is what lets the plugin
turn a volume into a few numbers instead of shipping 540672 bytes. No package built
yet and nothing installed; this is source, tests and a verification tool.

`src/spatial_sample.cpp` ports the offline model exactly: same layout offsets, same
clipmap selection over levels1..7 with the same +-63/31/63 bounds, same float32
rounding of every intermediate, same slab z mapping through the DXIL constant, and
the same linear-WRAP trilinear read accumulated in the same loop order so the sums
round identically. Entry points are `DecodeReference`, `SampleAtReference` and
`SampleAtWorld`, the last being what a directional read needs.

Two limitations are in the header, not hidden: `SampleAtWorld` REUSES the clipmap
selected for the reference rather than recomputing it, valid only for small offsets
well inside the clipmap; and neighbouring offsets can fall in different amortised
update blocks, so offsets from one volume are not necessarily the same age.

**VERIFIED BIT-EXACT AGAINST REAL CAPTURES.** `scripts/Verify-NativeSampler.py`
runs the native code over every preserved capture and compares to the Python
decoder all published numbers came from: **56 comparisons across seven captures,
zero mismatches**, to all17 significant digits — including 0.54354636445595861,
0.5968498114236247, 0.45754767087123838, 0.047033219041111129 and
3.1052094153216636e-05, the exact zeros from sampling into the ground, and offsets
of +-2, +-5 and +-3 gu per axis. Captures stay out of Git so this is run by hand.

24/24 native CTests, up from 23: the new `spatial-native-sampler` test carries 22
synthetic controls covering the algorithm rather than the data — weights summing to
one, a midpoint weighting two texels equally, negative coordinates wrapping while
an interior coordinate does not, the fallback reporting one without sampling, and
guards on inverse extent, clipmap scale, null constants and a missing volume.

**ONE next step:** wire the sampler into the probe so a transaction records a small
set of sampled values — reference plus a fixed offset pattern — beside the volume,
and confirm live that the recorded natives match what the decoder derives from the
same volume. That closes the loop in game and produces the first payload small
enough to be a feed. Only then design the public contract, and state in it the two
limitations above plus the amortisation freshness bound. Independent hiZ
per-source visibility remains pending and required.

## Previous checkpoint — the drift is amortised block refresh, 2026-09-09

Offline analysis of the eight volumes in each series, no new capture. The drift
that dominated the day/night test now has a mechanism, and it explains every
plateau observed all day.

**Clipmap re-centring ruled out.** Origin, scale, relative and inverse extent are
byte-identical across all eight transactions in both series.

**The volume is continuously rewritten.** Between consecutive transactions ~1.5s
apart, about 25% of all 540672 voxels differ, mean absolute deviation ~5.4/255.
The rate is 24.7..25.8% in every step and identical by day and night. Differences
against transaction0 grow 25.7 → 51.1 → 71.7% and saturate: repeated churn, not
convergence after arrival.

**The update is amortised in blocks of 16 z-slices and rotates.** Within clipmap1's
slab (z=67..130), in every one of the seven consecutive pairs at least one 16-slice
block is BYTE-IDENTICAL while others change 11..73%, and which block is untouched
rotates. A spot-checked untouched slice has all 32 y-rows unchanged.

**This is the single cause of the plateaus.** A point's value can only change when
its block is swept, which is why every series showed stable plateaus with occasional
steps and 0.03..0.3% agreement inside a plateau. Spread and drift were never
scatter; they are one phenomenon. It also settles the day/night question
mechanically: identical churn statistics by day and night mean the wider daylight
range was more refresh events sampled, not a time-of-day effect. The cloud
hypothesis is not needed and is not supported.

**HARD CONSTRAINT FOR THE FEED.** A reading at a point can be several seconds
stale depending on its block and the sweep phase. Sub-second responsiveness at a
single point is not obtainable from this field however fast the readback runs.
Worse for the directional plan: neighbouring offsets can lie in DIFFERENT blocks,
so a multi-offset directional sample mixes voxels of differing age. The contract
must state this rather than pretend the samples are simultaneous.

**ONE next step:** measure the sweep period directly, since it sets the feed's
real latency. A series with SpatialReadbackIntervalMs at its 250ms floor and
Count=8 gives eight snapshots ~2s apart at best; better, take two series at
different intervals and compare how often a fixed block repeats. Until the period
is known, do not promise any refresh rate in a consumer contract. Independent hiZ
per-source visibility remains pending and required.

## Previous checkpoint — drift dominates the day/night test, 2026-09-09

Third live series in PID33700, same open-sky Abyss spot as the dusk run but in full
daylight (Day40 Fri 9:15AM against Day58 Tue 8:29PM), sky stream confirming real
daylight at mean radiance [19.03,23.90,38.37]. Camera 1.22gu from the earlier
reference and identical across all eight transactions — the player did not move.
8 of 8 completed, no fallback.

| | min | median | max | spread |
|---|---|---|---|---|
| dusk | 0.527982 | 0.533635 | 0.543546 | 0.015564 |
| day | 0.510542 | 0.581883 | 0.597211 | 0.086669 |

**THE QUANTITY DRIFTS AT A FIXED POSITION.** The daylight series steps downward
through three plateaus — 0.597, 0.582, 0.511 — over twelve seconds at unchanged
coordinates, with 0.03..0.07% agreement inside each plateau. Total drift 0.087,
about 14%. The instrument is reporting a changing world, not noise.

**POSITION IS NOT THE EXPLANATION.** Sampling the stored volumes offline at both
camera positions costs only 0.0058 on the night volume and 0.0356 on the day volume
across that 1.22gu, confirming the field is flat in open sky as intended.

**NO TIME DEPENDENCE CAN BE CLAIMED OR EXCLUDED.** The day/night difference at a
fixed position is 0.018..0.048, smaller than the 0.087 drift within one stationary
daylight window, and the day range contains the entire dusk range. The assumption
that geometry and sky brightness separate cleanly therefore remains untested.

Hypothesis, not a finding: a moving cloud layer is visible below the platform and
the daylight run drifts far more than the dusk one, so dynamic sky occluders may
enter this voxel field. That would make the drift real occlusion rather than
instability, and would vindicate the earlier suggestion that wind-moved geometry
can move the value. Untested.

Evidence artifacts/light-research/series-20260909/run3-abyss-DAY-pid33700/.

**ONE next step:** separate drift from a systematic time effect before designing
any feed. Two or three series minutes apart at ONE unchanged position, ideally in
visibly still weather, establish the drift's timescale and amplitude; only against
that baseline can a day/night difference of 0.02..0.05 mean anything. Until then do
not build a consumer that assumes a stable ambient value: it moves about ten
percent over seconds even standing still under open sky, so smoothing is mandatory.
Independent hiZ per-source visibility remains pending and required.

## Previous checkpoint — precision established, method cross-validated, 2026-09-09

Second live series with 2.0.1-spatial-series.1 in PID31352, eight transactions
standing under the barn roof at -10531.94/611.35/-4424.06, 1.27 gu from the point
run3 measured earlier. 8 of 8 completed, fences 1..8, no fallback, reference equal
to the camera to 0.00 gu for the fifth time, cadence 1031..1531 ms.

Values: 0.0470332190, 0.0471239112, 0.0471551429, 0.0471701582, 0.0471641521,
then 0.0375554250, 0.0375656267, 0.0375737416.

**THE INSTRUMENT IS FAR MORE PRECISE THAN THE RANGE SUGGESTS.** The naive spread is
0.0096 (20% relative), but the series is two stable plateaus with one step, not
scatter: within the plateaus the values span 0.3% and 0.05%. The Abyss series has
the same shape. What moved across twelve seconds was the world or a volume refresh,
not the measurement. Smoothing absorbs steps; it could not have rescued scatter.

**THE OFFLINE DIRECTIONAL METHOD IS CROSS-VALIDATED.** run3's stored volume, from a
different process six minutes earlier, sampled offline at today's camera position
predicts 0.0341 against today's live plateaus of 0.0470 and 0.0376 — within 10% of
one and a factor 1.4 of the other, while correctly crossing three orders of
magnitude relative to run3's own point 1.27 gu away. Sampling the stored volume at
an offset therefore predicts what a real capture at that offset reads.

**A STEEP FIELD IS A PRODUCT CONSTRAINT.** 1.27 gu changed the reading by about
1500x. Under a roof edge a lamp mapping on fixed offsets will swing hard when the
player moves a metre. That is correct behaviour, not error, but it must be
smoothed, and it means position reproducibility — not measurement noise — dominates
wherever the field is steep.

Evidence artifacts/light-research/series-20260909/run2-under-roof-floor-pid31352/.

**ONE next step:** the remaining untested assumption is TIME. Every capture so far
is dusk or night, and the model assumes this term is geometric and therefore
time-invariant, while the SH sky stream carries the brightness. Test it at a FLAT
part of the field where position error cannot dominate — open sky, ideally the
Abyss where 0.53 is already recorded at 20:29 — by returning there in daylight. If
the value moves, the separation of geometry from sky brightness is wrong and the
feed design changes. Independent hiZ per-source visibility remains pending.

## Previous checkpoint — spread measured, open-sky anchor found, 2026-09-09

PRIVATE **2.0.1-spatial-series.1** installed via DMM and LIVE-TESTED in PID9464.
ASI SHA256 51BFE1C97BAB646EDA2462189DEA04196DE8F07B7A96632583B7494C23921339
verified; the INI was written by the assistant at16:03:16 and the log opened
16:05:28 carrying "Spatial readback v5 IDLE ... 8 texture/GI/exposure
transaction(s) at >=1000ms". Config SpatialReadbackCount=8, IntervalMs=1000.
Location deliberately extreme: the **Abyss, high above the world**, player at
-10679.4/1794.8/-3687.3, fully open sky, 2 lights in range and 0 on screen.

SERIES RESULT: 8 of 8 transactions completed and decoded, fences 1..8 in order, no
failure, 25/20 CPU observations because the window correctly waited for the series.

| # | frame | fence | dt ms | sky-vis |
|---|---|---|---|---|
| 0 | 14212 | 1 | — | 0.5435463645 |
| 1 | 14302 | 2 | 1500 | 0.5279821106 |
| 2 | 14392 | 3 | 1500 | 0.5279821106 |
| 3 | 14482 | 4 | 1516 | 0.5279821106 |
| 4 | 14572 | 5 | 1484 | 0.5383259607 |
| 5 | 14662 | 6 | 1500 | 0.5383259607 |
| 6 | 14752 | 7 | 1500 | 0.5383259607 |
| 7 | 14842 | 8 | 1500 | 0.5289446066 |

**REPEAT SPREAD = 0.0156** (min0.52798, median0.53364, max0.54355), about2.9%
relative at this level. All eight are `texture-sample`, none fell back. The
reference equalled the API camera position to 0.00gu for the fourth time.

**THE OPEN-SKY ANCHOR IS ABOUT 0.53, NOT 1.0.** With nothing whatsoever overhead or
nearby, the quantity reads ~0.53. It does not approach 1. Earlier readings must be
re-read against this ceiling: the 0.386 and 0.458 measured beside the barn are
roughly 73% and 86% OF THE OPEN MAXIMUM, not "half of full sky". Do not treat this
value as a fraction of visible sky.

**THE OCCLUSION SERIES SURVIVES THE NOISE FLOOR.** Against a spread of0.0156: open
0.458 versus wall0.118 is a gap of0.34, about22x the spread; wall versus under-roof
0.000031 is0.118, about7.6x; and even the two open points beside the barn differ
by0.071, about4.6x. Every distinction drawn on 2026-09-09 is comfortably above
noise. Caveat: the spread was measured at ~0.53 and may differ in other regimes,
in particular near the0.00003 floor where quantisation dominates.

**CADENCE IS ~1500ms, NOT THE CONFIGURED 1000.** Frames advance by exactly 90 per
transaction at 60fps. The interval is a minimum, as documented, and the real
limiter is elsewhere — the probe's own 500ms `lastAttempt` throttle plus exposure
dispatch selection. Any future live feed wanting a faster rate must revisit that
throttle; do not assume the configured interval is achievable.

Evidence artifacts/light-research/series-20260909/run1-abyss-open-sky-pid9464/
with the raw 12453950-byte JSON, derived.json, pre/post snapshots, native log,
INI and notes. Raw captures stay out of Git.

**ONE next step:** measure the spread once more in the MID range, for example at
the wall position that read0.118, because the noise floor there is what a
directional lamp mapping actually has to clear. Then the directional feed contract
can be designed against a known floor. Independent hiZ per-source visibility
remains pending and required.

## Previous checkpoint — repeated measurement built, 2026-09-09

PRIVATE **2.0.1-spatial-series.1** built and host-tested, NOT installed or
game-tested. ZIP artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-spatial-series.1-ModManagers.zip
SHA256 BDE65E2F29C28F4A2B34BD1CE8E6E1DF7DD8AF9626610C267CD7D74988CA9439;
ASI SHA256 51BFE1C97BAB646EDA2462189DEA04196DE8F07B7A96632583B7494C23921339.
Spatial defaults remain OFF. Earlier packages preserved. The game folder still
holds readback.4, so a live series needs a shutdown and this ZIP.

WHAT CHANGED: the one transaction per process becomes 1..8, configured by two new
keys, `[Research] SpatialReadbackCount` (default1) and `SpatialReadbackIntervalMs`
(default1000, accepted250..10000). Out-of-range values fall back to the proven
single transaction. The interval is a MINIMUM, not a sample rate: a transaction
still only arms on a selected exposure dispatch, so the real cadence is whatever
the game offers. The copy itself is unchanged — same validated barriers, same
whole-buffer copies, same release-barrier texture copy.

SAFETY. One readback destination is reused for the whole series and is never
re-armed before the previous fence completed AND its map finished, so the
destination is never in flight. Each transaction signals its own increasing fence
value. Any failure ends the series immediately and is reported as the last record;
a series never continues past an unexplained state. A consumed series cannot
restart in the same process. Everything else still fails closed: same list, source,
reset generation and recording thread, exactly one direct Dispatch(2,1,1), device
and heap and width validation.

REPORT AND DECODER. Format is `private-spatial-readback-v5` with a `transactions`
array, each entry carrying its own CPU observation, plus `requestedTransactions`,
`completedTransactions` and the interval. The latest transaction is also repeated
at the old `textureReadback` location so existing readers keep working. The decoder
gained `decode_series`, which decodes every entry independently and reports min,
median, max and spread; an entry that cannot be decoded is reported as
`undecodable` with its reason rather than dropped, and identical inputs produce a
spread of exactly zero rather than an invented average.

TESTS. 23/23 native CTests. 290 WARP and native-detour checks with zero
debug-layer warnings, including a new three-transaction series on its own queue
that proves each pass refreshes the reused destination with ITS OWN texture and GI
bytes rather than a stale copy, that fence values advance per transaction, that the
series ends exactly at its budget, and that a consumed series cannot restart.
42 Python tests including the new series decoding, the undecodable-entry case and
unchanged v1..v4 handling. All synthetic; no game evidence for the series yet.

**ONE next step:** install this ZIP, set `SpatialReadbackCount=8` with
`SpatialReadbackIntervalMs=1000`, and take ONE series standing still. That single
run closes the repeat-spread gap that has been open since the occlusion series,
because the eight transactions are eight measurements at one place. Only after the
spread is known should the directional feed contract be designed, since the spread
sets how large a directional difference has to be to mean anything. Independent
hiZ per-source visibility remains pending and required.

## Previous checkpoint — CPU shortcut rejected, direction required, 2026-09-09

No new capture. This checkpoint records an analysis of the existing five captures
and a product requirement that changes what the feed has to look like.

**The CPU exposure-cache route is ill-conditioned. Do not build a stream on it.**
It was tempting because it needs no GPU readback and no one-shot: the same
candidate is recoverable algebraically from `exposureCacheHex` through
`infer_visibility(lanes[8], lanes[5])`. Measured against the paired GPU texture in
the same captures, it tracks well in the open (CPU max0.386272 vs GPU0.386534) and
at the wall (CPU min0.117770 vs GPU0.117666), but collapses under a roof, ranging
0.000000..0.032105 where the GPU says0.000031.

The reason is conditioning, not noise. Across all 20 samples of a run the raw lanes
differ every time and vary continuously, so the cache is NOT stale; the plateaus
seen in the derived values are quantisation inside the decoder's inversion, not in
the data. But `lanes[8]` is essentially identical between open (~0.000174) and
under a roof (~0.000176) and therefore carries no visibility information, while
`lanes[5]` differs by only about5% (-6.12 vs -6.43) across a real difference of
more than a factor100 in the result. Inverting a near-exponential exposure relation
turns a five-percent input change into orders of magnitude at the output.
`lanes[5]` also behaves like an exposure/EV term — it varies MORE under the roof
than in the open, which fits auto-exposure adaptation rather than geometry, so a
signal built on it would react to where the camera looks, not only to structures.

The sound quantity remains the DIRECT texture sample, which is geometric and not
mediated by exposure. Making it usable therefore means lifting the one-shot limit
into a bounded periodic measurement of the small region of interest, not copying
2MB per frame and not substituting the CPU algebra.

**New product requirement: the ambient feed must be DIRECTIONAL.** Lamps placed
around the player follow the camera, but they must also express what is behind it.
Reference scenario from the user: standing in a cave mouth looking in — lamps
toward the interior go dark, unless a fire bowl stands there, while lamps toward
the opening stay bright if it is day outside. A single scalar at the camera cannot
express this; it would dim every lamp equally, which is exactly wrong.

The decomposition, with current status of each part:
- Directional sky radiance: ALREADY AVAILABLE. `/v1/ambient` carries9 SH
  coefficients per RGB channel, so it can be evaluated per direction.
- Directional occlusion: RECOVERABLE FROM DATA WE ALREADY COPY. The readback holds
  the whole64x32x264 volume and we have been reading a single texel of it at the
  camera position. Sampling the same volume at offsets around the camera gives a
  spatial profile; in a cave mouth the field genuinely differs over a few metres.
  Note the limits: this approximates direction by sampling a scalar field at
  neighbouring points, it is not a directional function at one point, and clipmap
  extent and resolution bound how far out it stays meaningful. Unmeasured so far.
- Local lights such as a fire bowl: ALREADY WORKS through existing ManyLights.
- Occlusion of those local lights: NOT BUILT. Still the separate hiZ route.

**Vendor neutrality:** this repository must not name Philips Hue or bake a
consumer-specific model into the API. The Hue consumer lives in C:\DEV\CrimsonHue;
what is published here stays a neutral directional ambient/occlusion contract that
any developer can consume. Nothing in the current API violates this; keep it so.

**DONE, same day:** the offline directional test was run on the preserved volumes
and the gradient is there. Sampling at the camera reproduced all three published
values exactly; the clipmap is 1.00 gu per voxel over 64x32x64 gu; sampling into
the ground returns exactly zero. In run3 under the roof, two units straight up is
still 0.000029 while five units up is 0.432942 and five units sideways is 0.311679
— more than a thousandfold across a few metres, at one instant, from one stored
volume. The cave-mouth effect is therefore expressible from data this instrument
already produces. Details and limits in LOCAL_ILLUMINATION_RESEARCH, "Directional
profile measured offline".

**ONE next step:** decide the shape of a directional feed before building anything.
The open questions are which offsets a consumer should sample, how to keep a lamp
from sampling through a wall into unrelated space, and whether the clipmap
selection must be recomputed per offset rather than held fixed. The repeat-spread
gap at a fixed point is still open and still wants one capture. Independent hiZ
per-source visibility remains pending and required; no public API, stream or
schema changed today.

## Previous checkpoint — occlusion series measured, 2026-09-09

Five paired captures with the installed 2.0.1-spatial-readback.4
(ASI 3F93F2568184011A3A7BE1FF1390DFC6A99AF1D87F872AFF31233180C64F508E), one
transaction per process, each with a `/v1/snapshot` recorded immediately before
firing. All five decoded as `texture-sample`, no fallback, GI window at offset0,
`cpuGiEqualsGpu` true. Every capture reports `exposure-window-absent`.

| capture | game time | sky-vis candidate | measurement point (= camera) |
|---|---|---|---|
| readback.4 open | ~22:32 | 0.457548 | -10526.5 / 613.4 / -4408.5 |
| run1 open, yaw180 | 22:32 | 0.386534 | -10530.6 / 611.3 / -4407.5 |
| run2 at the wall, yaw0 | 20:41 | 0.117666 | -10530.1 / 610.9 / -4415.2 |
| readback.1 deep in stall | ~22:32 | 0.000985 | -10537.7 / 613.1 / -4415.6 |
| run3 under the roof, yaw0 | 20:37 | **0.000031** | -10532.0 / 611.5 / -4422.8 |

RESULT: the values order monotonically by degree of enclosure and span a factor of
about15000. The cleanest pair is run2 versus run3: same game time (20:41/20:37),
same camera yaw0, same session, differing essentially only in depth under the
structure, and they differ by a factor of about3800. The earlier game-time
difference therefore cannot carry the effect. Two open points4.7gu apart differ by
only0.07, so the within-region spread is small against the effect — but note that
no point has yet been captured twice, so true repeat spread is still unmeasured.

REFERENCE IDENTITY PROVEN: in all three runs with a recorded snapshot, the decoded
`referenceWorldCandidate` equalled the API camera position to **0.00gu**. The
reference is the camera position exactly, not the player root and not merely
"view context". This also independently corroborates the assumed GI layout, since
the reference is decoded from GI constants while the camera comes from a separate
memory path. Player-to-camera boom measured6.48gu, collapsing to2.16gu when a wall
is behind the camera, so a capture must place the CAMERA under cover, not the player.

WHAT THIS DOES AND DOES NOT ESTABLISH. It establishes that this engine quantity
responds strongly and monotonically to local geometric enclosure — it behaves like
a sky-visibility term and detects a roof. It does NOT establish room brightness,
irradiance, lux, a physical roof percentage, per-source occlusion or anything about
light actually reaching the player. Pairing remains same-submission, never
binding-proven. No public API, stream or schema was changed.

Evidence artifacts/light-research/roof-control-20260909/ with run1/run2/run3
directories, each holding the raw JSON, derived.json, pre- and post-capture
snapshots, native log, INI and notes. Earlier captures remain in their own
directories. Raw captures stay out of Git.

**ONE next step:** capture the same point twice to measure repeat spread — the
only remaining gap in the series. run2's point at -10530.1/610.9/-4415.2 is a good
candidate because it sits mid-range where a spread would matter most. After that,
the exposure-window absence is worth a separate look, and independent hiZ
per-source visibility remains pending and required. Do not turn these numbers into
a public local-illumination field: the schema question is untouched and the
quantity is not room brightness.

## Previous checkpoint — FIRST paired GI/texture capture, 2026-09-09

PRIVATE **2.0.1-spatial-readback.4** installed via DMM and LIVE-TESTED in PID25944.
ASI SHA256 3F93F2568184011A3A7BE1FF1390DFC6A99AF1D87F872AFF31233180C64F508E
verified; INI saved12:55:40, log opened12:56:16 with "Spatial readback v4 IDLE",
health playing/error=null, supported build. ONE stationary capture, user standing
OUTSIDE the barn with the four lamps, at NIGHT.

SUCCESS. Complete transaction: texture540672 bytes, whole GI buffer65536 and whole
exposure buffer65536, status `gpu-complete-texture-and-buffers`, one queue, fence1,
exactly one Map, `giGpuFramePaired` and `exposureGpuFramePaired` true. 20/20 CPU
controls, error0, frames10319..10802, all GI copies equal, one selected exposure at
frame10395, reset generation34. Raw1570839 bytes.

Decoded: `giWindowOffset`0, found by matching the CPU GI copy inside the whole
buffer; `cpuGiEqualsGpu` true, so the fenced GPU bytes are identical to the CPU
read. Shader branch `texture-sample`, no fallback. Sampled R8_UNORM0.5424523291,
**candidate sky visibility0.4575476709** from the paired GPU GI constants.
Sample coordinates(-164.4762725830078,19.167692184448242,0.28228759765625).
The exposure window was NOT found: `gpuExposureInverseUnavailable:
exposure-window-absent`, so this run has no inverse and none may be invented.
`bindingProven` false, root corroboration zero, exactly as designed.

INTERPRETATION LIMITS. readback.1 measured0.000985 inside the roofed stall at
night; this measures0.4576 outside the barn at night. The direction matches what a
sky-visibility quantity should do, and the two sample coordinates are close, but
these are two SINGLE samples from different processes, frames and game times, one
using CPU constants and one paired GPU constants. That is not a controlled
comparison and must not be reported as an indoor/outdoor result, a roof
percentage, room brightness or per-source occlusion.

DEFECT FOUND AND FIXED: `rootSetsBeforeExposure`, `rootSetsInsideExposure`,
`tableSets`, `heapSets`, `rootThreadConflict` and `descriptorHeaps` lived in the
reported struct and were cleared by the NEXT list Reset, so this run reports zeros
while its arrays correctly show the same seven descriptor tables at root indices
5,6,7,8,9,12,14 and the unrelated upload-ring CBV at index1 (0x10CA1F1700).
readback.5 snapshots them with the arrays. In readback.3/.4 reports the counters
must be ignored; the arrays are authoritative. This never affected any copy.

PRIVATE **2.0.1-spatial-readback.5** built, NOT installed. ZIP
artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-spatial-readback.5-ModManagers.zip
SHA256 66072F10E3DBD9AB10299B5D891596D69DB2B326103901FA6FAA22A09190989F;
ASI SHA256 909CB22F7BA37F9BB380339152D92CB4CE98A5B8514482BFAED154253F09D304.
Spatial defaults OFF. 23/23 CTests, 247 WARP/detour checks, zero debug warnings,
including a new control that a later Reset cannot rewrite a dispatch snapshot.
37 Python tests. Earlier packages preserved.

Evidence artifacts/light-research/spatial-readback4-live-20260909-pid25944-outside-barn-night/
with raw JSON, derived.json, native log and INI. Raw SHA256
6C4DF3191EFA18108F136F606D2354F60F0D9C16B121DA3D4A6D0FE79FC54D5E.

**ONE next step:** the mechanism works, so the question becomes a controlled
experiment rather than another lone number. Install readback.5, then take a
BOUNDED REPEATABLE PAIR: several captures outside and several under the roof, in
the same session, at the same game time, ideally without moving between them more
than the comparison requires. One capture per process means one game restart per
capture, so agree the count with the user before starting. Report spread, not a
single value, and keep the same-submission label. Investigate the absent exposure
window separately; it is not needed for the sky-visibility comparison.
Independent hiZ per-source visibility remains pending and required.

## Previous checkpoint — readback.4 pairs by submission, 2026-09-09

PRIVATE **2.0.1-spatial-readback.4** built and host-tested, NOT installed or
game-tested. ZIP artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-spatial-readback.4-ModManagers.zip
SHA256 6EA36715767C134F86E9B7BB249F5798235312AAB37192CBC65A3CF9BE293F8E.
Expanded v2.0.1-spatial-readback.4-20260909-104838-829-2fb5e1d9/CrimsonDesertTelemetry;
ASI SHA256 3F93F2568184011A3A7BE1FF1390DFC6A99AF1D87F872AFF31233180C64F508E.
Spatial defaults in the ZIP remain OFF. All earlier packages preserved unchanged.
The game folder still holds readback.3; replacing it requires a shutdown.

CHOSEN EVIDENCE STANDARD (the user picked this over resolving the tables): the two
claims are now separated instead of proven at once. Resource IDENTITY rests on the
existing validated native producer/consumer path documented in "Native spatial
provenance". TEMPORAL pairing rests on copying within the same recording and
submission, immediately after the selected native dispatch, under one fence.
The dropped requirement is ONLY "a root descriptor must point into the pinned
buffers" — impossible here, because the shader binds through descriptor tables.

CHANGES: the root scan no longer gates the copy; its hits are recorded as
corroboration (`giBindingHits`, `exposureBindingHits`) and were zero in the live
game. Whole pinned buffers are copied from offset0 instead of 768/128 bytes at a
binding-derived offset, so no offset is ever guessed in the plugin; the readback
allocation grows by giBytes+exposureBytes (each bounded to 65536 by the existing
validation). The GI buffer barrier now covers CONSTANT_BUFFER|SHADER_RESOURCE,
since the read path is not observable. Format is private-spatial-readback-v4 with
`pairing: same-submission-not-binding-proven` and an explicit pairingCaveat.
Everything else still fails closed: same list, source, generation and recording
thread, exactly ONE direct Dispatch(2,1,1), device/heap/UAV/width validation,
the validated release barrier for the texture, one fence, no Map before it.

The decoder finds the window instead of reading it: it locates the 768-byte CPU GI
copy inside the whole GI buffer at 256-byte alignment and the 64-byte CPU exposure
cache inside the exposure buffer, and reports `giWindowOffset` and
`exposureWindowOffset` with `bindingProven: false`. An absent or ambiguous match
raises `gi-window-absent`/`gi-window-ambiguous` rather than picking one; a missing
exposure window yields `gpuExposureInverseUnavailable` and no inverse at all.

TESTS: 23/23 native CTests, 239 WARP/detour checks with zero debug-layer warnings.
The pair test now asserts whole-buffer copies with the GI block found at its real
offset256 and the output at1024, plus a new control that mirrors the live game —
descriptor tables only, an unrelated upload-ring CBV, zero root hits — and still
pairs. Dispatch-context negatives (wrong dimensions, wrong list, missing barrier,
a second dispatch) still fail closed. 37 Python tests: window found, absent,
ambiguous, missing pairing label, wrong sizes, resource-identity mismatch, and
unchanged v1/v2/v3 handling. All synthetic; no game evidence.

**ONE next step:** after shutdown install the whole readback.4 ZIP with DMM, keep
Research/SpatialProbe=1, SpatialReadback=1, AmbientProbe=0 with Lights/ManyLights
and Ambient enabled, and SAVE the INI before starting the game. Verify ASI hash,
health and a native log line reading "Spatial readback v4 IDLE", then run ONE
stationary capture. If the GI window is found, this is the first same-frame GPU
GI/exposure snapshot and the roof/outside control can finally be designed — as a
bounded repeatable pair of captures, never as a single number. Report it as
same-submission pairing, never as binding-proven. Independent hiZ per-source
visibility remains pending and required.

## Previous checkpoint — binding path identified as descriptor tables, 2026-09-09

PRIVATE **2.0.1-spatial-readback.3** installed via DMM and LIVE-TESTED in PID4340.
Installed ASI SHA256 9B35DEC7A0960A0FC94B3146B9104ECA3F930344A1947467CC5FEF7E4ED66352
verified before the run; INI saved10:19:18, native log opened10:20:05 with the
"Spatial readback v3 IDLE" line, health playing/error=null, supported build.
ONE stationary capture. New location: OUTSIDE the barn with the four lamps, the
first outdoor spatial run; earlier ones were in or at the roofed stall.

RESULT: the diagnostic answered its question. It still copied nothing, but the
rejection is now informative instead of blank. 20/20 CPU controls, error0, frames
10382..10850, all GI before/after copies equal, one selected exposure at frame
10480, bank0, single GI (5110197488) and exposure (5278233376) identity.

Binding evidence at the selected Dispatch(2,1,1):
`rootSetsBeforeExposure`20 — the widened window works, readback.2 saw none of
these — and `rootSetsInsideExposure`1. CORRECTION: the counter and heap fields in
readback.3/.4 reports keep changing after the dispatch, because a later list Reset
cleared them while the arrays stayed snapshotted; the `tableSets`98 and `heapSets`6
figures first recorded here are therefore NOT dispatch-time values and must not be
quoted. The ARRAYS are authoritative, and they show the seven live tables below.
readback.5 snapshots the counters with the arrays. `rootThreadConflict` false. Live root descriptors: ONE
CBV at index1 =0x1036631400, ~53GB outside the pinned GI buffer, matching the
unrelated per-dispatch constant seen in PID2652 at a different address, which is
consistent with an upload ring. ZERO root UAVs and ZERO root SRVs. SEVEN live
descriptor tables at root indices5,6,7,8,9,12,14.

CONCLUSION: **the GI constants and the exposure output are bound through
descriptor tables.** No root-descriptor guard can ever accept this dispatch, so
`required-native-root-bindings-not-unique` is the correct and final answer for
that design. This says nothing about light, roofs or the readback.1 texture
result. Do not repeat this capture; the question it asked is answered.

Evidence artifacts/light-research/spatial-readback3-live-20260909-pid4340-outside-barn/
(raw JSON, native log, INI). Raw spatial-binding-4340-6978343-1.json SHA256
AE2A0DB3CB928E26062E17F3546522E8AD9291CFE98E2F9307F4824564958B2E. The decoder
refuses it with `no-completed-gpu-copy`; this run has no derived values either.

**ONE next step — a decision, not another capture.** Two honest routes to a
same-frame GI/exposure snapshot, both bounded:

1. Resolve the descriptor tables. Requires a heap-slot -> resource map built from
   observed descriptor creation AND copies on the device, plus each heap's
   GPU handle start and increment, then matching a bound table range against the
   pinned resources. Gives real per-dispatch binding proof. Substantially more
   native surface than anything built so far, and descriptors created before the
   probe arms would be unknown, so it must fail closed on a miss.
2. Separate the two claims instead of proving both at once. Resource IDENTITY
   already comes from the existing validated native producer/consumer path, and
   TEMPORAL pairing from copying in the same recording and submission immediately
   after the selected dispatch, under one fence — which the current code already
   does structurally. Drop the root-binding requirement, copy the WHOLE 65536-byte
   GI buffer plus a bounded exposure prefix, and let the offline decoder locate
   the 768-byte window that matches the CPU GI copy rather than learning the
   offset from a binding. Report and decoder must then label the result exactly
   as same-submission pairing, never as binding-proven.

Route2 unblocks the actual roof/outside research at a clearly stated, weaker
evidence level; route1 is stronger and much larger. Ask the user before building
either. Independent hiZ per-source visibility remains pending and required.

## Previous checkpoint — readback.3 built for the real binding order, 2026-09-09

PRIVATE **2.0.1-spatial-readback.3** built and host-tested, NOT installed or
game-tested. ZIP artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-spatial-readback.3-ModManagers.zip
SHA256 1CA14A0ECB3D4A832D584C54562CF2E39B6D327279897CFA61A78B3080CCA547.
Expanded v2.0.1-spatial-readback.3-20260909-101411-176-7bdb890a/CrimsonDesertTelemetry;
ASI SHA256 9B35DEC7A0960A0FC94B3146B9104ECA3F930344A1947467CC5FEF7E4ED66352.
Spatial defaults in the ZIP remain OFF. All earlier packages preserved unchanged.
The game folder still holds readback.2 (941C9DDB...); replacing it needs a shutdown.

WHY: the live rejection below proved readback.2 looked in the wrong window. A root
argument persists until it is overwritten or the root signature changes, so the
bindings for the selected dispatch are normally issued BEFORE the game's exposure
wrapper runs. readback.2 cleared `cbv_`/`uav_` inside `Arm` and gated recording on
the thread_local `active` observation, so it could only ever see roots set within
that wrapper — one CBV, no UAV, in the measured run.

CHANGES: root recording now runs for the whole recording of the pinned command
list, opened by the observed Reset and cleared by Reset or a new root signature,
not by `Arm`. New diagnostic observers cover SetComputeRootShaderResourceView
(vtable39), SetComputeRootDescriptorTable(31) and SetDescriptorHeaps(28), so
"bound through a table" is now distinguishable from "not bound". Tables and heaps
are RECORDED ONLY; a descriptor handle is never resolved to a resource and never
copied from. GI may now pair through a root CBV or a root SRV into the same pinned
buffer, with the matching AccessBefore and 256/4-byte alignment respectively;
everything else still fails closed. Report format is private-spatial-readback-v3
with nativeSrv/nativeTable/descriptorHeaps, giFromSrv, rootThreadConflict and
root-set counts before/inside the exposure. All root arguments must come from the
recording thread. No public API, stream or schema change.

TESTS: 23/23 native CTests. The pair test now installs all SEVEN production
binding detours on DIRECT and COMPUTE, sets the roots BEFORE `Arm` — the exact
live failure order — and still reaches a fenced, byte-exact GPU GI/output/texture
pairing: 223 WARP/detour checks, zero debug-layer warnings or errors. New negative
controls cover a descriptor table replacing the GI root, roots left over from a
previous recording, and a root SRV pairing at a 4-byte offset. 32 Python tests
pass, including v3 acceptance, root-SRV validation against its own array, a
rejected root-thread conflict and unchanged v2 handling. Re-decoding the readback.1
capture reproduces its preserved derived.json byte-for-byte. These are host
controls, NOT evidence that the game binds its roots this way.

**ONE next step:** after shutdown install the whole readback.3 ZIP with DMM, keep
Research/SpatialProbe=1, SpatialReadback=1, AmbientProbe=0 with Lights/ManyLights
and Ambient enabled, and SAVE the INI before starting the game — a start with the
package defaults arms nothing. Verify ASI hash, health and a native log line
reading "Spatial readback v3 IDLE", then run ONE stationary capture. If it pairs,
design the bounded repeatable roof/outside control. If it rejects again, read
rootSetsBeforeExposure, tableSets and nativeTable first: a table binding is now
positive evidence about the path, not a dead end. Independent hiZ per-source
visibility remains pending and required.

## Previous checkpoint — readback.2 measured live, bindings rejected, 2026-09-09

PRIVATE **2.0.1-spatial-readback.2** installed via DMM and LIVE-TESTED in PID2652.
Installed ASI SHA256 941C9DDBC215F56676570376630D6C0A71277513E4B5B1824167DA95342529FD
verified against the immutable package before the run; exact supported EXE hash
4D99C15C..., build25116796, health playing/error=null. Config Research/SpatialProbe=1,
SpatialReadback=1, AmbientProbe=0 with Lights/ManyLights and Ambient enabled.
The INI was saved BEFORE the game start that mattered: an earlier PID29052 launch
read the package defaults0, never armed the probe and consumed no one-shot.

RESULT: a valid negative. The instrument worked; its binding hypothesis did not.
20/20 CPU controls, error0, frames12772..13266, all GI before/after copies equal,
exactly ONE selected exposure at frame12824, single bank0 and stable GI/exposure
resource identity. The selected native Dispatch(2,1,1) WAS observed on the pinned
list and recording thread (nativeDispatches1), so selection and detours are correct.

The guard then rejected with `required-native-root-bindings-not-unique`, HRESULT
0x80004005: gpuCopyIssued false, buffersCopied false, mapCalls0, fenceValue0 and
no texture copy. Inside the exposure wrapper exactly ONE root CBV was set, index1
=0x1039FF1700, and ZERO root UAVs. giBase0x2F7ED0000, exposureBase0x2F5820000;
the observed address is53GB away, so cbHits0 and uavHits0. Decode-SpatialReadback.py
correctly refuses with `no-completed-gpu-copy`; this run has NO derived numbers.

INTERPRETATION: the GI constant buffer and exposure output are NOT bound through
compute root descriptors inside the game's exposure dispatch wrapper. They are
bound either earlier on the same list — recording currently starts only at Arm(),
gated by thread_local `active` in `SelectedNative` — or through descriptor tables,
which this build does not observe at all. This is NOT zero light, NOT a hook
failure and NOT evidence about roofs. readback.1's texture result is unaffected.

Evidence artifacts/light-research/spatial-readback2-live-20260909-pid2652-bindings-rejected/
(raw JSON, native log, INI). Raw spatial-binding-2652-4706421-1.json SHA256
03C94ED43A2FEA71C14FA6A3BDEB02CA815D740BCFBABBAC281659978F7E2287.

**ONE next step:** do NOT recapture with this build; the guard would reject
identically and a restart alone changes nothing. Implement readback.3: open the
root-recording window at command-list Reset instead of Arm(), and ADD diagnostic
observers for SetComputeRootDescriptorTable(vtable31), SetComputeRootShaderResourceView(39)
and SetDescriptorHeaps(28) — record only; never copy from a guessed table. Then
ONE restart and ONE capture decides which path actually binds GI and exposure.
Independent hiZ per-source visibility remains pending and required.

## Previous checkpoint — GPU-paired diagnostic ready, 2026-09-09

PRIVATE **2.0.1-spatial-readback.2** built/tested, NOT installed or game-tested.
ZIP artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-spatial-readback.2-ModManagers.zip
SHA256 B721CBF0460AC6E32FF9864DB95F7D6B9626115E5E735D2543F6375A2B0BCBCB.
Expanded v2.0.1-spatial-readback.2-20260909-090652-602-084cc472/CrimsonDesertTelemetry;
ASI SHA256941C9DDBC215F56676570376630D6C0A71277513E4B5B1824167DA95342529FD.
Previous immutable packages preserved. LAST OBSERVED: PID8772 using readback.1,
health playing/error=null/sequence81700; recheck on resume, not a live guarantee.
No new live capture, toggle, install or config edit.
User STOPPED due to exhausted usage window; requested only finishing the ZIP.
No further live run or shutdown requested. Package ready for a later DMM install.

Resolved CB view: outer.vtable+88 ->142DEB870 -> storage+140 array,index0.
View+50 is actual GPU VA passed to native root CBV by1437B5F9E (slot37).
Both live GI banks and exposure view had offset0, but implementation DOES NOT
assume0: it observes native SetComputeRootConstantBufferView/UAV addresses and
subtracts actual resource.GetGPUVirtualAddress with bounds/alignment checks.
Full native provenance and raw-page evidence in LOCAL_ILLUMINATION_RESEARCH.

Readback v2 adds native root CBV/UAV/signature/Dispatch observers, only selected
exposure/list/thread. Requires unique in-resource bindings and exactly one direct
Dispatch(2,1,1). Immediately AFTER original native Dispatch, copies768 GPU GI bytes
and128 exposure-output bytes with compute->copy->compute buffer barriers. Existing
exact texture release copy follows; all share ONE destination/list/submission fence.
Only completed transaction sets paired flags. Root tables/indirect dispatch or
cached/unseen/replaced bindings fail closed, with observed root addresses in JSON.
CPU scene/frame label/cache remain UNPAIRED; no public local-illumination API.

Same config: Research/SpatialProbe=1,SpatialReadback=1,AmbientProbe=0;
Lights/ManyLights=1 and existing Ambient enabled. Same event script,20 CPU controls,
ONE transaction per process (including failure). Report private-spatial-readback-v2.
Decoder supports v1 unchanged and v2 from actual GPU GI/output, no invented CPU
cache flags; emits direct-vs-inverse difference and CPU-vs-GPU GI equality.

23/23 native CTests;187 original copy checks +165 new actual WARP shader/native
detour checks on DIRECT+COMPUTE. Nonzero GI256/output1024 offsets byte-exact,
GPU output computed from same CB, gate proves no Map/paired flag before fence.
Zero debug warnings/errors after correcting COMMON initialization in test setup.
14 existing +13 readback Python tests pass; package/payload validation pass.
These are synthetic/host controls, NOT evidence that game's roots will match.

**ONE next step:** after shutdown install whole ZIP with DMM, same config, load
and verify hash/health; run ONE stationary event capture. Inspect root bindings,
gpu-paired status, direct/inverse error, and progressing control. If binding guard
rejects, use its saved addresses (do not guess offset/state or report zero light).
If positive, design bounded repeatable paired capture for roof/outside control.
Independent hiZ per-source visibility remains pending and required.

---

**Historical checkpoints below.** Preserved evidence, not an instruction queue.
Their "next step", PID, package and "not tested" statements describe their dates;
the current checkpoint above takes precedence. No need to read them all on takeover.

## Previous checkpoint — FIRST live direct texture readback succeeded, 2026-09-09

ONE requested capture in PID8772 (start08:42:40), installed private
2.0.1-spatial-readback.1 ASI SHA256 matched
686BFA08BF49F9AFD3F78936B9BCB7966445B0372AB2DECD955307A5852FE666.
SpatialProbe=1/SpatialReadback=1/AmbientProbe=0; ManyLights and Ambient enabled.
User: between four lamps, NIGHT, indoors brighter than outside. Same partly
open roofed stall context, not an indoor/outdoor comparison. Released from
standing still immediately after recording. No second request or light toggle.

SUCCESS:20/20 error0 stable CPU controls, frames17704..18195. One direct texture
copy at frame17730, Reset generation9, queue5156726752. Exact release tuple
matched. Copy/submit tick1289546, fence completed1289562, exactly ONE Map.
540672 packed bytes, range0..255,33 distinct levels. Full shape64x32x264,
R8_TYPELESS60, footprint rowPitch256, allocation2162496 bytes.
Health playing/error=null, sequence12556 before ->13858 after.

Existing offline decoder: clipmap1, no fallback; CPU reference world
(-10537.655273,613.099182,-4415.592773). Linear-WRAP texture sample
0.999014573117 -> candidate sky visibility0.000985426883.
Independent algebra on SAME selected observation's single CPU cache read gives
0.000976572812 (absolute gap0.000008854071). Promising numerical agreement,
NOT a paired GPU proof. Do not invent cache stability flags: cache was ONE read
of unknown age, unlike the validated double-read recorder. GI CPU copies match,
but selected GPU CB content/offset was not copied. Not a physical roof percentage,
room brightness, light visibility or reason to discard prior inverse findings.

New CB metadata: all20 observations report BUFFER,65536 bytes, DEFAULT heap1,
GetHeapProperties S_OK. It is NOT an UPLOAD resource; do not Map it, assume offset0
or copy it using guessed state. Next pairing requires actual bound CBV GPU address/
offset and legal buffer-copy boundary. Existing owner/binding/producer route in
LOCAL_ILLUMINATION_RESEARCH is the anchor, not a new heap scan.

Raw/log/INI preserved under artifacts/light-research/
spatial-readback-live-20260909-pid8772-stall-night/;
raw spatial-binding-8772-1289031-1.json SHA256
A58BC2AE2CBB7B2E30B9B44829243A5C88C75AE52E2C2AD462FD7AF14AF6460E.
derived.json from Decode-SpatialReadback.py --assume-adapt-exposure-layout;
packed texture SHA25604c89b9be9a120646e246b161af7c222df951d5992c331d937dcce6d342e8726.
Detailed result/provenance in LOCAL_ILLUMINATION_RESEARCH, latest section.

**Next bounded step:** establish selected768-byte GPU CB binding/offset and
same-frame exposure-output pairing using the known consumer. Then enable a
bounded repeated direct comparison before a roof/outside control. Current
readback remains ONE request per process; no further run requested today yet.
Independent hiZ per-light visibility remains required. No plugin/config/source
or API changes this live-test turn; no installation/publish/push.

## Previous checkpoint — direct texture readback package ready, 2026-09-08

USER STOPPED FOR TODAY after package build. No further live run, switch or restart
requested. PRIVATE **2.0.1-spatial-readback.1** built, NOT installed/live-tested/
published. Game still PID32956/spatial-probe.2 at final check; installed ASI hash
C0CD941A340FC109FE681F530BBD0EA0FBF14BEC17D34720F1F5944639D191B4 unchanged.

ZIP artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-spatial-readback.1-ModManagers.zip
SHA256 B96869412578803A3AC0E5540B42AAB33D2A050D97C18757D8BF46C53D380269.
Expanded v2.0.1-spatial-readback.1-20260908-225633-355-e988a563/CrimsonDesertTelemetry
under artifacts/mod-manager; ASI SHA256
686BFA08BF49F9AFD3F78936B9BCB7966445B0372AB2DECD955307A5852FE666.

New **separate opt-in** Research/SpatialReadback=1 WITH SpatialProbe=1,
AmbientProbe=0, Lights/ManyLights=1; keep existing Lights/Ambient settings.
Both spatial options default0. SpatialReadback=0 preserves passive probe mode.
Same Start-SpatialProbe.ps1 event;20 CPU controls/2Hz/30s window, but ONE GPU
texture copy per process (including failure). Starts IDLE; no auto capture.

Uses actual post-exposure release packet, pinned source/list, known successful
Reset generation, same recording thread, stable CPU GI copies and <=250ms release
delay. Exact tuple only: Sync128->0, Access128->0x80000000, Layout6->1, whole
subresource, flags0. Inserts SRV->COPY_SOURCE/copy/restore, then forwards ALL
original barrier groups unchanged. Records exact submitting queue + its fence;
no Map until that fence completes. Worker allocation/Map only, one bounded
~2.2MB readback; ambiguous issued work retains COM refs, no unsafe timeout free.
No lossy global trace used as safety authority; disabled in direct mode.

Output private-spatial-readback-v1 in spatial-binding-PID-TICK-RUN.json beside
ASI: one packed64x32x264 volume + context + full release/submission/fence evidence.
GI GPU resource descriptor/heap metadata added; NO GPU-CB/cache copy yet. Direct
texture bytes are NOT proof of GPU-paired GI/scene/exposure cache or local lux.
Decode-SpatialReadback.py reuses existing GI model and verified linear-WRAP
sampler; labels sample a candidate from unpaired CPU reference. No API change.

22/22 native CTests passed, including187 WARP/debug-layer checks: exact540672
texels on DIRECT+COMPUTE, padded rows/depth, GPU gate/no early Map, exact packet
preservation and fail-closed controls. Zero debug-layer warnings/errors.
14 existing +7 new Python tests pass; package validation/payload equality pass.
These are HOST controls, NOT evidence about the game texture or paired frame.

**Tomorrow's ONE next step:** user closes game and installs whole new ZIP via DMM,
enables above config, loads anywhere. Verify running ASI/config/health first;
request one bounded stationary capture with Start-SpatialProbe.ps1. Check direct
copy status, progressing CPU control and fence before decoding; preserve raw/log/
INI under artifacts/light-research. If rejected, diagnose its explicit guard,
not a zero-light conclusion. Then assess GI/cache GPU pairing from new resource
metadata. Separate per-light hiZ visibility remains required, not lost or solved.
Implementation/details in docs/LOCAL_ILLUMINATION_RESEARCH.md, "Direct texture
readback implementation"; no changes in CrimsonHue or nested research repo.

## Previous checkpoint — exposure release barrier found live, 2026-09-08

ONE requested live run, PID32956, installed spatial-probe.2 ASI hash matched
C0CD941A340FC109FE681F530BBD0EA0FBF14BEC17D34720F1F5944639D191B4.
SpatialProbe=1/AmbientProbe=0.20/20 valid stable CPU observations, progressing
frames7186..7679. Health playing/error=null, sequence5248 after capture.
Reference(-10537.132,613.093,-4415.896), same camp area; no doorway test.
User released from standing still immediately after recording. No second run.

GLOBAL trace is LOSSY:376447 Barrier calls,7673 target barriers counted,
1136 dropped callbacks,8192-event capacity reached after2.734s (9.375s window).
Only2258 target barrier events stored; never confuse counted and stored totals.
sample complete=true describes20 attempts, NOT complete interval coverage.

Positive result: all FIVE stored exposure begin/end pairs have the same list/
known-reset-generation pattern: GENERIC_READ1 -> UAV3 -> SHADER_RESOURCE6 ->
exposure begin/end -> GENERIC_READ1 -> Close/Execute. Reset generations
7/20/33/46/59 on list0xC91C86F0; all submitted on queue0x135F1E820.
Transitions are outside Dispatch, explaining its zero local trace. Losses mean
these subsequences are observed evidence, not certified complete GPU history.
Release barrier: Sync128->0, Access128->0x80000000/NO_ACCESS, Layout6->1,
Flags0, all subresources. Detailed raw evidence/limits in LOCAL_ILLUMINATION_RESEARCH.

**Next bounded implementation:** use the ACTUAL post-exposure release barrier
as candidate copy boundary: match current exposure context, same pinned resource,
list/reset generation and full barrier tuple; reject unknowns/conflicts. Evaluate
copy-before-release + restore/forward semantics in WARP before touching the game.
Do NOT infer current state from this lossy log or merely enlarge/repeat the broad
trace. Reuse actual submitting-queue/fence machinery. GI GPU-CB pairing and
exposure-output age still require handling; CPU cache is not a paired GPU read.
No local-ambient or visibility API yet; independent hiZ source-occlusion remains.

Evidence artifacts/light-research/spatial-live-20260908-pid32956-stall/
spatial-binding-32956-10539062-1.json (+INI/native log), SHA256
181CFBFB5A7F1559BEA340E618D3720468CCAEBD6B8D57BF0A5CFE2197D501AF.
No source/ASI/config/package mutation, new GPU copy or API change this turn.

## Previous checkpoint — spatial-probe.2 ready, 2026-09-08

Resolved typed SRV from actual live view settings + its native descriptor builder:
R8_UNORM61, Texture3D view8, default component mapping1688. NOT guessed from the
typeless resource. Full provenance in docs/LOCAL_ILLUMINATION_RESEARCH.md,
"Typed view and interval trace". Actual Barrier call1437B3057 confirmed; its
input resource uses storage+100. Prior zero trace is still a coverage question,
not absence evidence. No new stationary/movement capture this turn.

Built PRIVATE **2.0.1-spatial-probe.2**, not installed/live-tested/published.
ZIP artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-spatial-probe.2-ModManagers.zip
SHA2567A26996CE09E26A888C092C2BEBC27AC167124C9EA11E1CAFED9641A05540E00.
Packaged ASI SHA256C0CD941A340FC109FE681F530BBD0EA0FBF14BEC17D34720F1F5944639D191B4.
Same SpatialProbe=1/AmbientProbe=0 test config; preserve Lights/ManyLights/Ambient.
Starts IDLE; same Start-SpatialProbe.ps1 named event,20 samples/2Hz/30s deadline.
JSON now private-spatial-binding-v2: old per-sample trace PLUS intervalTrace.
Whole-window target barriers, global interception controls, Reset/Close and
existing queue submission callback, thread/list/reset-generation/order fields.
Fixed8192 events/512 lists; try-lock loss and overflow explicit. No GPU copy,
new barrier, new fence or public stream. Never infer GPU completion/current
layout from CPU order alone. Target texture pinned for the run to prevent reuse.

**Next step:** game shutdown, install entire new ZIP via DMM, enable research
settings, load anywhere. Verify ASI/package identity then ONE bounded capture.
Check intervalTrace.barrierCalls/targetBarriers, droppedCallbacks/overflow,
list generations and Execute events against exposure samples. A missing trace
is not a source value. Only then prepare direct paired texture/CB readback.
Individual light occlusion remains a separate hiZ task; existing feeds unchanged.

At handoff game still PID23516/spatial-probe.1, health playing/error=null,
sequence85492. Installed ASI unchanged; no file/config installation performed.
21/21 native CTests and14/14 Python tests passed; package validation and payload
equality passed. These do not replace the pending live v2 capture.

## Previous checkpoint — passive probe valid in roofed stall, 2026-09-08

First live test of PRIVATE2.0.1-spatial-probe.1, PID23516 (started21:51:08).
User stood among four fire lamps in a PARTLY OPEN ROOFED STALL: two closed
walls, one fully open side, one doorway side. NOT an open outdoor baseline or
a doorway transition. One requested stationary capture; no light toggles asked.

Installed ASI matches immutable expanded package SHA256
A5F8F82C6F28E3A61C09BC3518DAF35D7CD5DEF04A509286186A7416A02C6167.
Research/SpatialProbe=1, AmbientProbe=0.20/20 valid observations in9.594s,
frames21107..21567;20 matching before/after GI copies. Actual GetDesc confirms
Texture3D64x32x264,1mip,R8_TYPELESS60,UAV flag4. Sampler matches prior finding.
CPU shader model selects clipmap1 in all20. This validates access/coordinates,
NOT texture values, GPU pairing, local brightness or per-source visibility.

Barrier hook installed for19/20 observations; ZERO matching barriers, no trace
overflow. Two native command lists, one texture resource. The scope was only
the exposure Dispatch invocation; neither a missing transition nor a safe copy
state is established. Do not simply repeat this run or issue a guessed barrier.
Raw view metadata is an engine object, NOT a D3D12_SHADER_RESOURCE_VIEW_DESC;
the typed SRV format remains unknown. No new GPU commands were issued.

Evidence preserved in artifacts/light-research/spatial-live-20260908-pid23516-stall/
(raw JSON, native log, INI). Raw spatial-binding-23516-8839093-1.json SHA256
D04CB79201035FA26D38CE27AD9B9F53028A6B9DF168CA57FD1F0BD7401E15D6.
Existing health still playing/error=null, sequence22597 after capture.
User released from standing still; no game files/config/API/package changed.

**Next bounded step:** close the observer's coverage gap around this SAME
identified texture: resolve its typed SRV and observe its actual enhanced-barrier
submission outside the narrow Dispatch scope, retaining list/generation/order
provenance. Current log has no global Barrier-call control, so zero target records
cannot distinguish out-of-scope transitions from interception/identity gaps.
No generic movement test. Only after a legal state/fence path is established,
implement direct texel + paired GI-CB/exposure readback. Source occlusion remains
separate via existing hiZ route; raw/smoothed lights and global sky stay unchanged.

## Previous checkpoint — passive spatial binding probe ready for DMM, 2026-09-08

User said continue. Found texture creation, DirectX resource description path,
static sampler and ENHANCED barrier path. Do NOT reuse the legacy UAV-buffer
barriers for this texture. Detailed evidence and instrument contract in
docs/LOCAL_ILLUMINATION_RESEARCH.md, "Direct readback preflight" section.

Built PRIVATE **2.0.1-spatial-probe.1**, NOT installed/live-tested/published.
ZIP artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-spatial-probe.1-ModManagers.zip
SHA256 F085947BAB4D1BDFDB14B6D7295DE8E3458BC5E02104B5C1D87EB63A2D141E33.
Package validation/payload equality PASS;21/21 CTests, including32 new WARP/
synthetic observer checks and80000 parallel ABI calls;14 Python diagnostics
tests unchanged/pass. These are host tests, not game evidence.

The probe is PASSIVE: GetDesc, actual caller's GI constants, sampler and exact
texture barriers observed during AdaptExposure Dispatch. NO new GPU command,
copy, inferred current-state claim or public stream. Existing raw/smoothed/sky
continue. This is preparation for direct readback, NOT completed readback.
Default Research/SpatialProbe=0. For test set1, keep AmbientProbe=0 and existing
Lights/ManyLights+Ambient enabled. Console/Explorer not required. Starts IDLE;
scripts/Start-SpatialProbe.ps1 -ProcessId ACTUAL_PID requests20 samples at2Hz,
30s hard timeout. JSON spatial-binding-PID-TICK-RUN.json beside ASI. Empty trace
does NOT prove no resource transition; record scope is one Dispatch invocation.

**Next step:** user closes game and installs this whole ZIP via DMM, enables
SpatialProbe=1, loads game. Then one bounded passive capture while stationary
anywhere (no doorway/walking needed), verify progressing frame/control and actual
texture barriers. Only after that implement safe copy + paired CB/inverse check.
Last observed game remains PID22128/sky.1; no files changed in game installation.
Individual source occlusion still separate; existing feeds remain unchanged.

## Previous checkpoint — GI reference is camera-linked, 2026-09-08

Followed existing exposure/filter chain, found exact GI constant producer
143C533A0: uploads768 bytes inline filterOwner+20; `_clipmapUVRelativeOffset`
equals viewContext+8F8/8FC/900 world position times `_invClipmapExtent`.
Exposure consumer1435429F0 selects the SAME CB at filterOwner+560/+568 via
byte+705 and sky texture at+4B8. Exact binding/producer offsets and preserved
native evidence: **docs/LOCAL_ILLUMINATION_RESEARCH.md**, native section.
No heap search/hook/new API/config/ASI/install needed for this discovery.

Implemented optional Capture-ExposureContext.ps1 -IncludeSpatialContext and
offline spatial decoder (same explicit shader-layout flag): bounded double CPU
copies, backlink/bank/layout guards, coordinates and fallback, no texture value.
14/14 synthetic tests (including3004 old FP32 cases), PS7 parse clean.
Live21:22:01 PID22128/sky.1:50 attempts/4.923s,50 progressing frames;45 stable
spatial observations,5 changing contexts rejected. Reference matches camera to
<0.001gu, NOT player. All45 CPU-model clipmap1, texture0x139A2FF40, CPU shape
64x32x264.44 independent exposure candidates,6 unavailable. User moved since
the prior outdoor control; do not invent an indoor transition from v changes.
Evidence artifacts/light-research/local-illumination-spatial-context-20260908-
pid22128-check1{,-derived}.json, rawSHA F34EB56E… . No GPU-frame pairing yet.

**Next bounded step:** verify this texture's actual format/sampler/legal copy
state at the known exposure binding/dispatch boundary and prepare paired direct
texture+GI-CB readback. No guessed ResourceBarrier or generic movement test.
Why v varied outdoors remains OPEN. Camera sky visibility is not player-local
irradiance, direct sun/moon shadows or source visibility. Separate hiZ
t15,space36 source-occlusion track still required; raw/smoothed feeds unchanged.

## Previous checkpoint — outdoor view-control, 2026-09-08

User said ready; ONE30s read-only exposure context recording20:57:18..48 CEST,
PID22128/sky.1 unchanged.300 attempts,297 progressing renderer frames56279..57769,
291 candidates;6 changing cache/3 changing bridge probes safely unavailable.
One cache-chain identity. Camera direction excursion72.294deg, camera position
excursion7.0333gu; final direction13.4404deg from start, not exact pose A-B-A.
Player instructed stationary but player pose not independently captured.
User subsequently clarified: standing IN THE OPEN, not at a door. This recording
is an outdoor camera control, with no reported interior transition. References
to the positive doorway test mean the SEPARATE earlier PID4208 recording only.
Do not explain this run's variation as crossing a doorway/roof boundary. Nor
interpret v~0.4 as a calibrated percentage of visible sky; normalization and
reference-position semantics still need the direct check below.

Histogram L2.92115..4.55835, exposure.0142187...0190443, candidate v.355885...482627.
Important: v changes even while camera X/Z/direction are constant (0..5s and
15..30s; only small Y bob). Direction-only explanation insufficient. Do not call
this a fixed indoor/roof Boolean, a failed decoder, or invalidate the earlier
doorway result. Actual GI reference origin, texture dynamics/coverage and CPU
cache GPU age/coherence are still open. NO new public stream/API/ASI/config.

Evidence artifacts/light-research/local-illumination-view-control-20260908-pid22128
.json/-derived.json/-analysis.json, rawSHA2A5F0782…; exact numbers and limitations
in docs/LOCAL_ILLUMINATION_RESEARCH.md. User released after recording.

**One next bounded research step:** follow the already identified AdaptExposure
Voxel GI constants producer (_wrappedViewPos / _clipmapUVRelativeOffset) and
prepare one direct texture-sample/inverse comparison at the same reference point.
No generic camera/doorway repeats before that instrumentation. Separate source
occlusion depth resource t15,space36 remains required, not replaced by sky/exposure.
No plugin switch needed for this completed run; further GPU instrument may need one.

## Previous checkpoint — local sky-visibility diagnostic found

User authorized research/implementation of local environmental illumination AND
a separately source-visibility-filtered stream while preserving current feeds.
**First concrete result:** AdaptExposureCS itself samples the spatial sky-visibility
texture and writes intermediate EV+20 and clamped histogram L+32/+60 into the
ALREADY captured 64-byte CPU cache. All3 indexed shader variants have identical
function bodies. The inverse recovers a sky-visibility CANDIDATE, not lux, player
irradiance, source-to-camera occlusion, or proof of valid clipmap coverage.

Replayed existing no-menu doorway control (120/120 stable appendices): mean v
outside0.402995 / inside0.0000122074 / return0.418049. Inside histogram L is at
1e-6 shader floor. No new roof/walking experiment or GPU hook needed for this.
Full derivation, archive paths/hashes and limitations:
**docs/LOCAL_ILLUMINATION_RESEARCH.md**. Do not rediscover exposure/GI/sky.

Implemented PRIVATE scripts Capture-ExposureContext.ps1 (bounded read-only,
exact EXE + existing Render mapping self-location + chain/bytes/control guards),
Decode-ExposureContext.py (explicit shader assumption, rejects invalid instead
of zero), synthetic tests including3004 independent FP32 forward/inverse cases.
Initial live smoke: PID22128, sky.1 unchanged,20:48:48 CEST,20 attempts/1.948s,
19 renderer frames;18 candidates v0.430869..0.431113, one changing cache and one
changing bridge unavailable. No new API/schema/ASI/config, no package/publish/push.
Evidence artifacts/light-research/local-illumination-live-20260908-pid22128-check*.json;
derived old run local-illumination-doorway-control-20260908-derived.json.
Final guarded-recorder check20:52:54:30 attempts/2.931s,30 frames,29 candidates
v0.406743891..0.406744957;1 changing cache unavailable. Separate final-check*.json.
9 synthetic tests pass; existing-output/wrong-process guards pass. Not a controlled
movement comparison. Actual GI reference origin still needs validation; neither
player nor camera world position is promised by the inverse.

Occlusion track: existing ProcessManyLightsCS actually reads g_hiZMap t15,space36
(five depth samples at selected mip); this tests an extended region and is NOT
an existing source-center-visibility flag. Concrete depth-resource capture route
and required provenance/wall-control are documented. **No occlusion stream yet**;
raw/smoothed/global sky are untouched. Local irradiance/direct sun shadow also open.

**One next live control:** with unchanged plugin, record fixed-player/changed-view
exposure context to separate reference-origin sky-visibility from scene luminance.
Capture camera POSITION as well as direction (orbiting moves the camera). Then
promote only validated semantics; raw texture coverage/GPU age remain unknown.
Do not repeat generic doorway/global-sky tests. Source-depth probe follows as its
own required implementation, not abandoned in favor of exposure.

## Previous checkpoint — combined sky/local-light live acceptance passed

User installed private2.0.1-sky.1 and loaded. PID22128 started20:16:12, supported
EXE4D99C15C…; installed ASI A6B77099E26448260B922D6E9301CDCDD631A2039546D91381E93387811E5E55.
Loaded module is present; native log confirms global-sky hook +20Hz ManyLights.
INI Lights/Enabled=1,ManyLights=1,Ambient/Enabled=1,Research/AmbientProbe=0.
No ASI/config changes or game controls in this verification. No publish/push.

ONE10s concurrent WebSocket recording at20:19:14..24:600 raw,601 smoothed,
601 sky envelopes. Sky available throughout, captureSequence315..334 (20 distinct
values/frames8382..8845), maxage532ms; all20 unique messages pass schema.
Raw/smoothed158 matching captures2475..2632 (~16Hz observed,20Hz configured),
maxage94ms,44 raw contributions at endpoints,37..38 grouped sources.
One bridge-changing unavailable message in BOTH local feeds at snapshot5681,
20:19:21.4076884; next message recovers. Safe rejection, not continuous stale or
native fault; existing smoothing invalidation resets tracks (two IDs per stable
test target across run). Do not claim zero dropouts or measured performance cost.
Sky continued without interruption; raw schema1.4, gameplaying, authoredavailable.
6926 member checks,17661 EMA lane checks,442 repeated-capture checks; no violations.
Healthplaying/errornull; subscriptions cleaned back to1 existing client.

Evidence artifacts/light-research/sky-stream-live-20260908-pid22128/:
raw.jsonl,smoothed.jsonl,ambient.jsonl, before/after HTTP+ambient, validation.json,
native log, INI. Script Capture-LightStreams gains opt-in IncludeAmbient; old
default stays two streams. Sky luminance estimate .005143259→.004315493 in run,
not local room illumination or exposure-normalized RGB. User standing unchanged.

**Latest user requirement:** source occlusion/visibility is a REQUIRED additional
telemetry capability, not merely an incidental discovery. Preserve current raw
and smoothed feeds without a new visibility filter AND provide a separate
visibility-filtered view/stream once measured. Current renderer-filtered sources
are not a complete world registry and do not prove line of sight (overlay has no
depth test). Do not call current data "all game lights" or "unoccluded lights".

Separate source visibility from camera (including offscreen/occluded/partial/
unknown distinctions) from contribution to visible surfaces and light reaching
the player. Hidden source can illuminate a visible surface; one point test would
not establish visibility of its whole contribution. Unknown is not occluded;
any derived view needs sample/camera provenance, freshness, method/limitations,
and a relationship to the original contributions without promising physical IDs.
No endpoint/schema/result invented yet. Requirements recorded in docs/API.md.

**One next research step proposed/accepted in discussion:** inspect the ALREADY
found exposure producer's input measurement, before adaptation/clamping, rather
than scan arbitrary heaps for an indoor Boolean. Distinguish view-dependent
brightness from local illumination with a fixed-player/changed-view control when
a concrete candidate exists. Keep occlusion/visibility as its own required track;
record relevant existing shader/renderer evidence encountered but do not equate
exposure with visibility. No new experiment or code change in this requirements
turn. Do not repeat sky discovery/doorway tests. Combined-feed acceptance above
is complete but does not prove long-session/no-dropout behavior.

## Previous checkpoint — combined local lights + global sky preview ready

Private **2.0.1-sky.1** built, NOT installed or live-tested. Current game still
PID4208/probe.3; do not replace a loaded ASI. User has not confirmed shutdown.
No publish/push. ZIP: artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-sky.1-ModManagers.zip
SHA256 C2864ECCB69D969432F1C46DE5839391FDFD9AED6949958A64DE29C241F33A88;
ASI SHA256 A6B77099E26448260B922D6E9301CDCDD631A2039546D91381E93387811E5E55.
Old packages/captures unchanged. ZIP's inherited README still describes public2.0;
the private addition/config/semantics are documented in docs/AMBIENT_STREAM.md.

New /v1/ambient HTTP, /v1/ambient/stream WS, /v1/ambient/schema: separate global
sky envelope1.0, raw27 signed SH coefficients + measured normalization/matrix
inverse and relative luminance estimate. Not player-local, lux, exposure-corrected,
or separated sun/moon. No exposure cache exported or correction applied. Raw
snapshot1.4, local smoothing and HUD unchanged. New Ambient/Enabled=1 in package.

Production native A shares ONE bounded in-flight readback/fence transaction with
ManyLights, optional nominal2Hz sky vs configured lights rate; either source can
initialize. Distinct seqlock mapping/PID/start identity and1500ms sky freshness.
Only A has public decode; B diagnostic-only. Exact-build/context guards, per-copy
source/list/queue device identity and actual GPU fence maintained. Failed sky
preflight leaves local capture operational; shared runtime GPU fault stops both.
Shader-only update validation remains open; recovery notes added, no blind offsets.

Validation: managed tests pass (four new sky suites); all19 native tests pass,
including mixed-feed WARP with either discovery order, direct/compute queues,
blocked GPU and no cross-feed data reuse. C# decode matches prior PowerShell
results for120 saved doorway samples. HTTP/WS/origin/schema test on27312 passes;
package validation/regression/payload match passes. These are NOT game evidence
for combined mode. Opportunity-based sky scheduling/cadence needs live acceptance.

**One next step:** user closes game, replaces whole package via DMM, ensures
Lights/Enabled=1, ManyLights=1, Ambient/Enabled=1, Research/AmbientProbe=0, restarts
and loads. Check fresh supported PID/ASI hash, progressing raw/smoothed LOCAL
lights AND /v1/ambient captureSequence/freshness concurrently (no doorway repeat).
No need to enable console/explorer or start a bounded diagnostic file recording.

## Previous checkpoint — doorway control supports global sky

ONE user-authorized repeat, PID4208/probe.3,19:50:34..19:51:34,120/120 v2
samples,59.688s, frames54621..58051, A3849BB7/resource12A923050, fences241..360,
all cache flags31. No menu requested or reported in this run. Camera/API show
outside → inside → outside. Returned IDLE; user released. No automatic captures.

Stable windows: outside0..24s (48 samples), inside30..40s (20), return52..60s (16).
Mean sky luminance estimates .00171915 / .00165853 / .00154335; mean exposure
cache76.5012 /12994.0513 /126.7265. Cache responds strongly/reversibly (indoor
plateau ~13020.825), while sky has no corresponding large interior suppression.
Sky first→last -18.09%, with variation already outdoors: time/weather/framing
not fixed; not a calibrated local roof factor. Return player is farther outside.
This supports global-sky scope for THIS decoded source, not player-local ambient.
Do not divide sky by cache or claim cache plateau is validated physical exposure.

Evidence: artifacts/light-research/ambient-live-20260908-pid4208-doorway-control/,
ambient-probe-4208-1163500-3.bin (495360 bytes), raw/derived JSON, log, INI,
partial timestamped transition-trace.json. Binary SHA256
B2FB28F90308807B96443BF0C39B9063640C05A1E7EAE75620ED573576F2B315.
No plugin/config/API changes. **One next step:** distinguish the useful global-sky
feed from unavailable local indoor/outdoor illumination in the API design; retain
relative/raw units and exposure-age limitations. Do not repeat this doorway test
or restart shader/heap searches without a new, concrete question.

## Previous checkpoint — doorway capture saved, build-menu confound

PID4208 / probe.3 unchanged. User explicitly authorized ONE new run at19:46:51;
completed19:47:52, 120/120 valid v2 records, frames42025..45490,59.719s,
native A3849BB7/resource12A923050, fences121..240, cache flags31 throughout.
Probe is IDLE; user released from standing. No further recording authorized yet.

Outside preflight player(-10404.18,611.77246,-4422.0044). User confirmed inside;
API shows indoor stand near(-10399.578,612.0811,-4416.6196). Then return outside
near(-10403.945,611.77277,-4423.5625). User subsequently disclosed intervening
BUILD MENU. Camera jumps toY622.3043 about45..50s, compatible with that report;
exact menu boundaries unknown, final exterior dwell only a few seconds.
Technically valid capture, NOT a clean locality ABA or exposure calibration.

Sky luminance estimate .00248175242→.00240593462 (-3.055%); largest adjacent
changes -0.666%/+0.801%. No large roof-related brightness change established.
Exposure cache6.87468..13020.828, indoor plateau13020.828, last31.11422:
large response/saturation candidate, not accepted physical brightness or a safe
normalizer. No exposure division applied. Supports the global-sky interpretation,
but does not prove absence of all local occlusion. Still no public ambient API.

Evidence: artifacts/light-research/ambient-live-20260908-pid4208-doorway/,
ambient-probe-4208-940718-2.bin (495360 bytes), parser/decoder JSON, installed INI,
native log, partial timestamped API transition-trace.json (with gaps/caveats).
Binary SHA256 A57E397CD344BD170D5D73B6B34AE4D10EBB2DE782A73E7770DE8E8FA25208B2.
No source/config/package changes. **One next step:** only if locality acceptance
is still needed, repeat once without build menu, direct doorway return early
enough for >=10s exterior dwell. Ask user readiness; do not auto-start or rebuild.

## Previous checkpoint — live ambient + exposure-cache capture passed

**ambient-probe.3 is now installed and live-tested.** New PID4208 started19:33:03,
ASI710C06B8… (full hash below), exact build25116796/EXE4D99C15C… verified.
User set Research/AmbientProbe=1 and restarted; native log confirmed v2 IDLE.
Normal ManyLights/smoothed feed is intentionally paused, not broken.

ONE explicit run19:36:08..19:37:09 completed:120/120 valid v2 records,
frames8368..11250 over60.093s, A3849BB7 only, ambient resource12A923050,
DEFAULT width65536, COMPUTE queue, fences1..120. All120 cache flags31 (available,
same identity/bytes, positive scalar);120 distinct cache payloads across the run.
This accepts the instrument, NOT exact GPU frame pairing or exposure scaling.
Engine-cache frame age remains unknown; no correction applied to ambient/local RGB.

Evidence: artifacts/light-research/ambient-live-20260908-pid4208-exposure/,
original ambient-probe-4208-297671-1.bin (495360 bytes), raw parser JSON,
derived candidate JSON, native log and INI. Binary SHA256
D71D3125BD1DD88799EDB8E3F3F4C290C68E5F1A543D2CECEFADC28497D706B0.
Sky luminance estimate .0113772→.00418441 (-63.22%); cache exposure0.x
27.07308→47.40596 (+75.10%). Product/ratio not constant; these observations
alone do not establish an exposure correction. Details: docs/AMBIENT_DECODE.md.

Camera X/Z constant during capture, Y range613.5317..613.5449; player endpoint
(-10502.284,610.41223,-4374.5625). Small movement BEFORE first sample compared
with preflight, not a failed recording. Sun/moon advanced; no controlled doorway
crossing. User released from standing and may move now. Probe returned IDLE.
**One next step:** arrange a short out/in/out crossing within ONE explicit run,
with transition times/user cues, to test roof sensitivity alongside sky/exposure.
No need to rebuild/reinstall or repeat shader/exposure-source searches first.
Do not automatically start before user is at a suitable doorway. Still no public
ambient API or player-local illumination claim. Restore AmbientProbe=0 and restart
only when returning to normal local-light use, not silently during diagnostics.

## Previous checkpoint — exposure-cache appendix ready for live test

**Private diagnostic v2.0.1-ambient-probe.3 built; not installed or game-tested.**
The last checked game was PID34736 with the previous local-lights.1 ASI and
AmbientProbe=1. **User stopped for bed: no installation or further experiment.**
Shutdown is NOT confirmed. Tomorrow recheck process/config; do not replace a
loaded ASI. No public ambient endpoint or exposure correction added.
ZIP SHA256664E9913123DE5EC929C6D1BCA2DA52FA22974CB9C029F161DE3B335306EDC93;
ASI SHA256710C06B8040C62039A5D3055A163C506A14BBBEDEE206D64042BC4220ABC0B4A.
Package validator/payload regression checks pass; older packages untouched.

**Important provenance correction:** ExposureOwner+D8 is NOT an upload source.
Native1435450CC passes owner+C0 to1437DD810 with the owner's readback helper+D0;
14354511E obtains completed readback data via143031500. Two 32-byte stores at
14354512C/139 copy that data to owner+D8/F8. Source-frame age is unknown.
The new diagnostic therefore captures this **GPU-derived CPU cache**, before
and after recording ambient, not a same-frame GPU ExposureConstantBuffer.
Same bytes/chain do not prove identical GPU age or eliminate ABA/torn-read risk.

Binary probe v2 appends224 bytes to each original3904-byte sample (total4128):
bounded64-byte cache before/after, known pointer chain, layout, times, validity.
Missing cache does not discard valid ambient; changed/unusable cache stays raw.
Packed NaN lanes preserved as bytes/uint32, not rejected as malformed RGB.
No new hook, engine Map call, exposure resource barrier, API/config/default change.
Decoder passes the appendix through WITHOUT dividing or changing sky values.
Old v1 binaries/exports remain intact and compatible.

Tests: native17/17 including WARP/fence/run-isolation, 55 cache unit checks,
35 exposure reader/decoder checks, original178 SH checks and malformed reader
controls pass. Package/evidence details: docs/AMBIENT_DECODE.md.
**One next step:** after DMM deployment retain Research/AmbientProbe=1, restart
and verify the actual ASI hash + v2 IDLE log. User need not freeze now. Start ONE
explicit bounded run only when loaded; inspect cache flags/values and raw sky
together. A valid stable cache is still not GPU-frame-paired exposure acceptance.
If exact GPU pairing remains necessary, establish CBV state/suboffset first;
never borrow the ambient UAV barrier. Normal/smoothed lights remain paused in
AmbientProbe mode; restore0 and restart when returning to normal use.

## Previous result — shader-family ambiguity resolved

**Latest bounded follow-up:** native A explicitly selects the named
`PrecomputeAmbient` technique pass before its captured dispatch. All SIX entries
for csPrecomputeAmbient in the existing shader index were extracted and compared:
their executable function bodies are identical (hash/evidence in
docs/AMBIENT_DECODE.md). Variant choice within this current-build family is no
longer a decoding blocker; do NOT build another shader hook merely to choose
between them. A bound-PSO hash was not captured; historical decoder reports keep
runtimeShaderIdentityVerified=false. No ASI/config/API change or new game capture.

**New result:** exact packing and sample normalization derived from the existing
csPrecomputeAmbient export. 9 signed coefficients per working RGB channel,
256 upper-hemisphere samples, shared sum/128. Mean sky RGB=C0/(2*.282095);
upward irradiance/pi quadrature=-C1/.488603. Same fixed color matrix as older
ManyLights research; decoder preserves working RGB and an inverse-matrix result.
Estimated Rec.709 luminance is labeled as an input-primaries assumption, not lux.
Detailed derivation, source hashes/lines, limitations: **docs/AMBIENT_DECODE.md**.

Private scripts/AmbientSh.psm1 +Decode-AmbientProbe.ps1 added; reader gains an
optional PassThru with unchanged default output. 178 synthetic/offline checks
pass; all360 preserved gameplay samples decoded under an EXPLICIT profile
assumption. Each capture directory has a new ambient-derived-candidate.json;
original binary/parser exports unchanged. Native A is known, live PSO shader
hash still NOT captured. No API/plugin/config/package/consumer changes.

**Follow-up:** the cache provenance/diagnostic extension is now at the top.
Exact GPU timing remains unmeasured; do not assume UAV state for a CBV copy or
divide every local light by one scalar.
Keep raw sky values and explicit relative units. Existing consumer exports read row7/56, not
the SH evaluation; don't claim otherwise. Local roof occlusion is unresolved;
if another live control is needed, use a quick out/in/out in ONE capture rather
than separated stand runs. Do not rebuild the decoder or redo the earlier heap/GI
search. AmbientProbe=1 still pauses normal/derived local lights until restored0
and restarted. User need not stay at their last pose.

## Three gameplay captures — reference

**Latest: return-outdoors run3 complete.** User reports outside the home at about
01:45 in-game, moonlight. Same PID34736/ASI/config. Explicit start23:09:15,
complete23:10:15: 120 valid fenced samples, frames56372..59645 over59.843s,
A RVA3849BB7 only, resource0x137024E80, fences241..360. API sequences55771->62632,
no errors; player(-10404.208,611.77277,-4424.308) unchanged at endpoint reads.
Paired CPU camera near(-10405.824,614.823,-4419.3525). Probe returned IDLE.
User released from standing; do not assume they remain here in the next turn.

Evidence `artifacts/light-research/ambient-live-20260907-pid34736-outdoor-return/`:
`ambient-probe-34736-11299500-3.bin`, parser JSON, native log and INI.
Binary SHA256F65F00872BDDAA6F901AA4A272C6F3F8A13EEC9CA28263546CD41AD171486344.
All three gameplay runs passed (360 samples). **No local indoor/outdoor
acceptance:** run2 end to run3 start has an uncaptured244.860s gap, and the moon
direction changed23.1885deg. No doorway transition captured; not a controlled
same-lighting A-B-A. Raw SH/scalar lanes change nonuniformly, not decoded RGB.
Details and source packing notes in research/light-source-tests/GPU_LIGHT_LAYOUTS_25116796.md.

Offline decoding follows at the top checkpoint; global sky and local occlusion
remain distinct. No plugin/API/config/package change during these measurements.

## Indoor run2 reference

User reports
inside the player home, with windows. Same PID34736/ASI/config as below. Player
(-10396.898,612.0811,-4414.3125), paired CPU camera near
(-10396.708,615.220,-4411.6694). Explicit start23:04:11, complete23:05:10:
120 valid fenced samples, frames38296..41870 over59.562s. Same A producer and
resource0x137024E80, fences121..240 (no old fence reuse), B=0. API sequences
38002->43596 and unchanged player position at the endpoint checks; no errors.

Evidence `artifacts/light-research/ambient-live-20260907-pid34736-indoor/`:
`ambient-probe-34736-10995062-2.bin`, parser JSON, native log, installed INI.
Binary SHA2567674AE55340D713107FFFB1C085728991B5A8451B4386ECD2DDEF3B92332B277.
Rows0..7 and56 vary; others unchanged within this run. Raw row6 XYZ
(.0003239843,.0004012281,.00030329468)->(.0003145608,.00038555585,.00029848135),
W stays1. Sun direction(-.017686604,-.33864108,.94074947)->
(.081872046,-.33665884,.9380607). Both solar/lunar directions have changed
substantially since the outdoor recording, so the lower raw coefficients do
NOT prove indoor occlusion. No decoded ambient RGB or locality claim yet.

Return-outdoors run3 above completed in the same process; time drift remains
a confound, despite the short spatial distance outside the home.

## Outdoor reference in the same process

User accepted local-light smoothing and moved to ambient. Restarted PID34736
(22:51:48) loads the same local-lights.1 ASI, hashB80FEA8597F1399EECBBA982C396D7E0651E36E32387CB82A38E2A0764AA1400.
Installed INI Research/AmbientProbe=1; log confirmed IDLE before the explicit
start. Player/camera API playing and progressing at the camp, not the menu.
Assistant sent ONE `Start-AmbientProbe.ps1 -ProcessId 34736` after this check.

**Outdoor run1 complete:** 120/120 parser-valid GPU-fenced records, frames
8611..11402 over59.984s, native path A RVA3849BB7 only (B=0). Resource
0x137024E80, native width65536, heap1/DEFAULT, queue2/COMPUTE. Logical view
64x16; copied1024byte prefix. Paired CPU camera(-10499.338,614.517,-4378.455)
through(-10499.338,614.515,-4378.455); player remained
(-10502.611,610.52826,-4373.8613). API sequences2483->11414, no errors.
Capture started22:54:52 and completed22:55:53. Subsequent indoor run2 above
reused this process without a restart or ASI replacement.

Evidence: `artifacts/light-research/ambient-live-20260907-pid34736-outdoor/`
contains `ambient-probe-34736-10436500-1.bin`, parsed `ambient-readback.json`,
native log and INI. Binary SHA256
`484B1DAE203BEAA2D40492A77B4D7188F2C57E8FC7CCE951E7189A189AEC8FBC`.
Rows0..6 and56 change; all other rows unchanged over this run. Raw row6 XYZ
changes(.008332003,.005748682,.001810758)->(.002566751,.001718922,.00048462633),
W .15278931->.23067258. These are raw coefficients/scalars, NOT decoded RGB.
Sun/moon directions also advance. The outdoor signal changes substantially
without movement: **time/weather confounds a simple sequential indoor decrease**.

Indoor and return-outdoors comparisons are recorded above.
Do not claim player-local ambient based on global sky coefficients alone. SH
packing/normalization, exposure, actual shader-variant identity and direct
sun/moon color/intensity remain unvalidated; no ambient public API yet.
Normal/derived local-light streams intentionally remain paused in diagnostic
mode. After experiments restore AmbientProbe=0 and restart; backup path below.

## Previous checkpoint — smoothed local-light live stand test passed

User additionally requested grouped/smoothed **local lights**, not ambient, for
immediate CrimsonHue consumption. Implemented in telemetry, no CrimsonHue edits.
Raw `/v1/snapshot`, `/v1/stream`, schemas1.1/1.4 and HUD data remain unchanged.
New HTTP `/v1/lights/smoothed` + WS `/v1/lights/smoothed/stream`, separate envelope
schema1.0. Contract/consumer warnings/config: **`docs/SMOOTHED_LIGHTS.md`**.

Current filtered ManyLights only (do not double count authored list). Conservative
0.15gu proximity groups, sum linear HDR RGB, 200ms time-based EMA once per NEW
GPU capture. Single lights smoothed too. Spatial session-local tracking ignores
sample indices; it is approximate, NOT physical identity. No transitive groups.
Current centroids and raw directional contributions preserved; no invented
aggregate spotlight cone. Missing groups removed immediately, stale/fault/loading
clears all output and tracking. Consumer must watchdog capturedAt/sequence, not
publishedAt, and use colorLinear once, not add contributions/raw stream again.
INI `[LightSmoothing] TimeConstantMilliseconds=200, GroupRadius=0.15`; CLI flags
also available. No ambient schema or physical lamp output added.

Private **normal-light** DMM package installed and running in PID23572, not published:
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-local-lights.1-ModManagers.zip`
SHA256 `97D1480A3DBFAA14F484879C813D79D3B00639A68BD87E927E110CCF41A7005B`;
ASI SHA256 `B80FEA8597F1399EECBBA982C396D7E0651E36E32387CB82A38E2A0764AA1400`.
Normal ManyLights enabled, AmbientProbe absent/off. Use this package for Hue,
NOT ambient-probe.2 (that ZIP still has the old managed host and pauses lights).
This new ASI includes the explicit-start ambient instrument too; later enable
Research/AmbientProbe=1 and restart if needed, instead of downgrading the host.

Evidence: managed build/tests pass incl. grouping/HDR sum, singleton/group EMA,
sample permutation/rate independence, disappearance/staleness/invalid controls,
32768 dense records and 4096 tracked groups, unchanged raw serialization and
separate subscriptions/health faults. Real loopback HTTP/WS smoke passes; native
16/16 CTests pass. Package validator passes. Packaged ASI bootstrap on a copied
fixture at port27316 successfully forwards nondefault333ms config. These checks
are synthetic/offline; the following stand test is separate real-game evidence.

Live PID23572 (started22:39:07), installed ASI hash matches B80FEA85...; native
log confirms normal20Hz ManyLights, no ambient probe. API playing, user standing
at(-10502.611,610.52826,-4373.8613). 12s simultaneous WS capture: **717 raw +717
derived messages,188 distinct captures**, all available, age0..94ms (mean46.38).
Player position unchanged; raw telemetry sequence12423->13147. Matched all188
GPU capture sequences,8791 contributions unchanged,22773 EMA lane checks and
529 repeated-capture checks passed, no violations. Group count39..43; not a
claim that every scene source was static or every group has physical identity.

Nearest known fire at(-10507.645,610.937,-4368.322): two contributions in all188
captures, **one tracking ID despite161 distinct sample-index combinations**.
Raw/smoothed mean luminance0.830806/0.830421; summed absolute step variation
9.57781->5.31268 (**44.53% reduction**). Blue singleton at
(-10510.692,611.633,-4371.438): one ID despite84 index values; step variation
0.0206773->0.0173479 (**16.10% reduction**). Reduction is the measured temporal
variation of this short run, not color accuracy or a universal noise measure.
No lamp was toggled during this capture; physical Hue and movement/off/culling
acceptance are not claimed by the stand test.

Evidence: `artifacts/light-research/smoothed-live-20260907-pid23572-a/`
raw.jsonl,smoothed.jsonl,before.json,after.json,validation.json. Reproduce with
`scripts/Capture-LightStreams.ps1` (bounded, read-only API; new directory) and
`scripts/Test-LightStreamCapture.ps1` (offline; creates new validation.json).
Original public2.0.0 and both ambient ZIPs remain immutable. No game files changed
by the assistant; the user installed the preview through their workflow.

User accepted the remaining fire pulsation at200ms and explicitly chose to move
on to ambient; **do not insist on another local-light off/on test**. It remains
an unperformed additional control, not a blocker to this next experiment.

**Next — ambient restart prepared:** current PID23572 still runs normal lights.
Assistant backed up the installed INI to
`artifacts/light-research/ambient-control-20260907/CrimsonDesertTelemetry.before-ambient-pid23572.ini`
and added `[Research] AmbientProbe=1` to bin64/CrimsonDesertTelemetry.ini only.
It takes effect on the NEXT process; no ASI/package changed or live trigger sent.
User should restart the game and load outdoors. On "ready", verify new PID,
installed B80FEA85... ASI, native log IDLE and progressing API player/camera;
then use `scripts/Start-AmbientProbe.ps1 -ProcessId NEWPID` and inspect first
complete GPU samples BEFORE asking for an interior comparison. Same ASI/ZIP
supports this; no downgrade to an older diagnostic package. ManyLights and its
smoothed stream intentionally pause in ambient mode. Afterwards restore
AmbientProbe=0 and restart; preserve all other user configuration.

## Previous checkpoint — ambient readback works; explicit-start probe.2 ready

Probe.1 ran in PID34848 with the expected ASI hash. **120 real GPU-fenced
samples, one DEFAULT resource, path A active; no faults.** However the automatic
60s window completed during loading/menu, before the camp was loaded. CPU scene
cameras remained near zero/(0,1000,0), unlike the later progressing live API camp
control. This validates the instrument/producer, NOT outdoor ambient semantics.
Raw capture/log/config/parser export preserved in
`artifacts/light-research/ambient-live-20260907-pid34848/`; full evidence in
`research/light-source-tests/GPU_LIGHT_LAYOUTS_25116796.md`, final subsection.
User was told the early start was our diagnostic-control mistake, not an absent
ambient source. Game is now absent (read-only process check); no ASI replaced.

Private probe.2 fixes control, not the measured path. Starts IDLE even with valid
loading/menu frames. The assistant explicitly starts each run after verifying a
fresh playing API snapshot using `scripts/Start-AmbientProbe.ps1 -ProcessId PID`.
This signals `Local\CrimsonDesertTelemetry.AmbientProbe.PID`; no Explorer/console
enablement, remote memory writes or public API changes. Verify log acceptance
and NEW `ambient-probe-PID-TICK-RUN.bin`, then use `Read-AmbientProbe.ps1`.
Each run: 2Hz/120 valid samples; returns idle. Repeated runs work in the same
process and keep increasing fence values. Busy requests are discarded, faults
and external stop permanently refuse restart. Each file is CREATE_NEW.

Private DMM ZIP (configuration included, not published or installed yet):
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-ambient-probe.2-ModManagers.zip`
SHA256 `E1463B646CE417182B7B8A03970B94A12811F79BABB9E3350E2CD89E5A8D7812`;
ASI SHA256 `9EFC5A2EE11738BADFF0FEF5434093E753884D6F9E4C3CE46B844318B02789E0`.
Native Release build and 16/16 CTests pass, including actual named-event control,
idle suppression, two GPU-gated runs (no old-fence reuse), file isolation,
busy/fault/stop negative controls. Parser fixture/15 negatives and package
validator pass. Public2.0.0 hash unchanged. These new control tests are synthetic.
ManyLights/rendered-light API and markers remain paused in this private mode;
player/camera/authored path remains. Restore AmbientProbe=0 plus restart afterwards.

Sun/moon directions are included in every scene record. Direct sun/moon color
and intensity are NOT separately validated. Ambient packing/normalization,
exposure and player-local interior/shadow semantics remain open; no public schema.

**Next:** user installs probe.2 through DMM and loads outdoors; on "ready", verify
PID/hash/fresh API, signal ONE start, inspect first complete samples while still
outside, and only then request an indoor comparison using a second bounded run.
Do not consume another loading sequence or branch into broad GI research.

## Previous checkpoint — ambient probe ready for DMM installation

User authorized continuing ambient validation and has now confirmed the game
is closed; process absence independently verified. PID30016 pointers below are
historical. **Private ZIP installation/restart has not happened yet.** No ambient
GPU sample has been measured in the game yet; public2.0.0/API remain unchanged.

Native route now resolved using existing bridge owner (no heap scan):
filterOwner+10 -> Renderer+668 -> SkyOwner+98 -> outer+30 -> inner+168 resource.
Live inner stride16/count64 confirms the1024-byte candidate. UAV producer and
CBV consumer both use SkyOwner+98. Full addresses and evidence are in
`research/light-source-tests/GPU_LIGHT_LAYOUTS_25116796.md`, final subsection.

Private probe implemented in existing `render_capture.cpp`, not another DLL or
public schema. `[Research] AmbientProbe=1` selects it **instead of ManyLights**;
`[Lights] Enabled=1/ManyLights=1` stay set. Explorer/console remain disabled.
Exact EXE guard plus executable-section/signature preflight; two post-dispatch
hooks at RVA3849BB7(skyRDI/commandRBX) and384CBA3(skyRBP/commandRDI).
Validates64x16 view and native UAV buffer bounds; copies1024bytes immediately,
on that native list, then waits for the exact submission's queue fence.
CPU SceneConstants sampled/rechecked around recording; not yet proof of the
GPU-bound scene CBV. Captures source/outer/sky/path/frame identities at recording.
No exposure capture/SH decoding/local-interior semantics/public ambient API yet.

Bound:2Hz,120 accepted samples, then no more copies. Output alongside ASI:
`ambient-probe-PID-TICK.bin` (CREATE_NEW, max468480bytes). Each3904byte record:
64byte private header,2816byte scene,1024byte ambient. Read/validate with
`scripts/Read-AmbientProbe.ps1 -Path <file> [-OutFile <new-json>]` (PS7.4+).
Header/finite-row validation rejects incomplete data, not evidence of absence.
Native log reports source width/heap/queue, each path's hit count and progress.
The normal rendered-light API/markers are unavailable during this diagnostic;
player/camera/authored light path remains. Restore AmbientProbe=0 and restart
(or reinstall public2.0.0) afterwards. Public package defaults are unchanged.

Verification: native Release build, **16/16 CTests PASS**; both actual hook
signatures independently match the current disk EXE. Both thunks pass160000
multithreaded calls preserving GPR/XMM/flags/stack. Real D3D12/WARP verifies
both path IDs, bad stride/count/short/non-UAV rejection, unrelated submission,
blocked GPU before fence, exact output/scene/provenance, duplicate-frame refusal,
bounded stop and no ambient bytes published into the light bridge. Parser:
positive GPU fixture +15 invalid inputs +JSON export/overwrite refusal pass.
These are synthetic/native tests, NOT live ambient-light acceptance.

Private DMM ZIP (not published, public release untouched):
`artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-ambient-probe.1-ModManagers.zip`
SHA256 `8A7E2378A44A68535689DF3159A53F6F233FC3CFB6E9EBAEB3C6B10ECAF638AD`.
Expanded `artifacts/mod-manager/v2.0.1-ambient-probe.1/CrimsonDesertTelemetry`;
ASI SHA256 `C5880F3F5BC06FA81C092F0A7FA95BD1C273463694DD04FB5885CA92527255E4`.
Uses unchanged managed/runtime/license payload from the actual GitHub2.0.0 ZIP;
only ASI/INI/private README differ. INI already enables the bounded probe.
Package validator/self-test and ZIP-to-expanded-payload comparison PASS.
Original2.0.0 ZIP SHA3212DD... remains unchanged.

**Next:** user installs the private ZIP via DMM, loads into an
outdoor scene, and inspect first complete ambient samples/log BEFORE requesting
an indoor comparison. If no hit, use path counters, valid scene progression and
phase diagnostics; do not call an empty file absent ambient. Do not publish.

## Previous checkpoint — ambient stream feasibility

User noticed CrimsonHue needs environmental light in addition to local lights;
authorized a bounded investigation of existing sources. **Published 2.0.0 and
its HTTP/WebSocket schemas are unchanged; ambient is not implemented.** No live
instrumentation or game installation changes in this check.

Two actual offline shader producers found, not only matching names:
`csPrecomputeAmbient` (entry6acf206f) and
`GenerateAmbientFromEnvironmentAtmosphericScatteringCS` (entryca7a87f5) write
`g_texPrecomputedAmbientUAV`, float4 stride16, u2 space39 in the inspected
variants. Directional ambient/SH coefficient evidence is strong; not a single
ready-to-stream RGB. Existing GI reflection has a 1024-byte
`PrecomputedAmbientConstantBuffer` (b32 space35), but the runtime producer,
UAV-to-CBV link, decoding/exposure convention and behavior indoors are unverified.
Sky ambient must not be advertised as measured local illumination at the player.

Reuse findings/paths/hashes in
`research/light-source-tests/GPU_LIGHT_LAYOUTS_25116796.md`, final ambient section.
Ignored evidence: `artifacts/light-research/ambient-check-20260907-sky*`;
16 representative sky-compute entries decoded/validated, 491,971 bytes total.
Already available SceneConstants contain sun/moon directions, not ambient RGB;
do not restart camera discovery or equate atmosphere input parameters with output.

**Next:** identify the live ambient producer/output, validate actual resource
bounds and immediately read back the compact result with fence/scene provenance;
then compare daylight and shade/interior. Reuse existing graphics instrumentation.
Only after validation define an additive ambient stream; no broad GI investigation.

## Previous checkpoint — published 2.0.0 ZIP passes user live test

User authorized both releases. GitHub **2.0.0 is public**:
https://github.com/fabianviol/CrimsonDesertTelemetry/releases/tag/v2.0.0
Release ID 383735205; tag source `44c7a72e3aeebcdfb7d5fc6253dc7510f68a2bf8`.
Release workflow 34063681737 and CI 34063670548/34063681715 succeeded.

Published asset 547756043: `CrimsonDesertTelemetry-v2.0.0-ModManagers.zip`,
805939 bytes; nine-file payload inspected.
ZIP SHA256 `3212DD1FD6CDEE5F537C2000572965648B97D0AC38335381E65BEF086FBBE214`.
ASI SHA256 `D5D6337637E4582704105B9D95C803E2A7F7F6AD87DA31E57AF6D20167C3E4F9`.
This GitHub-built archive is distinct from the preserved local validation ZIPs.

Post-publication acceptance: the user explicitly tested the **GitHub release ZIP**
in the live game and reported "funktioniert alles" (2026-09-07). This confirms
the published package, including the HDR implementation, works on their setup;
it is not merely acceptance of the older local build. No new independent API or
capture measurements were taken in this follow-up; output mode was not specified.

Nexus https://www.nexusmods.com/crimsondesert/mods/3374 was updated through its
editor/uploader: the new lighting-first 2.0 description is saved; the same ZIP
was uploaded through Update existing file and Save file succeeded. The table shows
Main / Primary / 2.0.0. Public file ID **14887**, 787 KiB; at final verification
Nexus says **not yet downloadable: virus scanning in progress**. Public version,
new description, file summary and changelog were checked; no scan bypass attempted.
Previous 1.2.1 was archived, not deleted. The new screenshot,
"Version 2.0 - Live light markers and 3D radar", was uploaded and made the thumbnail;
https://youtu.be/eyRkkTXAU64 was added. Automatic Nexus CI remains unconfigured;
`NEXUS_AUTO_PUBLISH` is unset. This release used the site UI, not automatic CI.

Native Release and 14/14 CTests pass; GitHub also verifies 56 managed tests and
HTTP/WebSocket coverage. HDR10/scRGB uses the original linear-light UI compositor
with configurable 200-nit white (80–500); SDR retains its direct path. HDR evidence
is WARP/synthetic plus real ImGui integration/SDR↔scRGB transitions, **not live HDR
display/game acceptance**. No suitable HDR setup was available; the new live test
above does not establish actual HDR-output/display coverage. Full native lighting
still targets exact Steam 25116796.
API v1/WebSocket `/v1/stream` and raw schemas remain unchanged; no complete-light,
permanent-OFF, persistent physical-ID or exact-pixel-color claim is made.

Public GitHub release is visibly Latest, with the verified asset available through
the release API. Nexus requirements still correctly point to the x64 ASP.NET Core
runtime and ASI loader. Next, if following up: check file 14887's automatic scan
result on Nexus; do not upload it again. No further source/game installation work
is required by publishing. In Nexus's file-description editor, locator fill did
not persist; normal select-all/type/Tab did, verified on the public download page.

## Previous checkpoint — HDR added; publication paused

User stopped the GitHub release after learning the old HUD explicitly disabled
HDR output, then authorized implementing HDR without a live HDR display/game test.
**No v2.0.0 tag, draft or public release was created.** Main was pushed through
`1e7d999` (lighting-first README/media/credits); repository About/video updated.
WebSocket is unchanged: `/v1/stream` carries JSON; HTTP snapshots remain available.

Implemented original D3D12 HDR compositor: HDR10 R10/PQ/Rec.2020 and FP16 linear
scRGB, alongside unchanged direct SDR rendering (8-bit plus 10-bit SDR). UI goes
into a transparent FP16 target, then converts/mixes with the game in linear light.
No game tone mapping, output color-space setting or HDR metadata is changed.
Transparent finite scene pixels and scene alpha are preserved. UI reference white
is `[Overlay] HdrPaperWhiteNits=200`, clamped80–500, shared by all UI/notices.
Mode/format changes pause incompatible drawing and rebuild only after our GPU fence.
Two extra full-resolution GPU textures/copy/composite are used for HDR UI; not SDR.

Verification: native Release build and **14/14 CTests PASS** (8.73s), including all
previous SDR/light/notification/WebSocket tests. New offscreen WARP tests verify
PQ/scRGB pixel goldens, alpha, gamut conversion, negative/extended scRGB and repeated
states; real ImGui scRGB tests cover notices/fonts/4K/resize, plus FP16→SDR→FP16.
This is synthetic GPU evidence, NOT live HDR-game acceptance. No HDR-capable setup
available per user. Sources/tests: `overlay_hdr.*`, `overlay_graphics.cpp`,
`overlay_hdr_tests.cpp`, `graphics_smoke.cpp`; details in OVERLAY_VALIDATION.md.

GitHub CI34062330798 passed managed56/API but exposed flaky native capture smoke:
Sleep(2) can leave GetTickCount64 unchanged and throttle away one-shot test calls.
Old binary reproduced both missing capture and skipped queue rejection. Fixed
test-only interval=0, phase diagnostics and6s deadline; production20Hz/5s policy
unchanged. Targeted and full native runs pass. CI log stays ignored under
`artifacts/github-release-v2.0.0/`. A fresh GitHub CI must verify the saved update.

HDR validation ZIP (803152bytes):
`artifacts/mod-manager/v2.0.0-hdr-20260907-001024/CrimsonDesertTelemetry-v2.0.0-ModManagers.zip`.
SHA256 `8B92CE3AC3E4EC09A468DF772AC84BFFA127895E3507D317A28FB8B775D71E80`;
ASI `C781D4F04B2A24D7403DA073150CDC86406B7537D32120E2773946B64619F26E`.
Nine-file validator/negative controls/ZIP equality PASS; README and INI explicitly
match current sources. It uses the newly built ASI and unchanged managed/runtime
payload from the verified pre-HDR2.0 package (host source/ABI/API unchanged).
The original versioned ZIP hash still matches E8A268...; no replacement occurred.

Saved/pushed source: `09d9462` (Codex). GitHub CI
https://github.com/fabianviol/CrimsonDesertTelemetry/actions/runs/34063281265
is green: managed/API/WebSocket plus all14 native tests, including HDR paths and
the corrected capture smoke. Working tree/package were verified; publication
recheck confirms no2.0 release/tag and latest published remains1.2.1.
Next: await resumption of the stopped publication; tag the validated source only
when release is requested again. No ASI was installed into the game during this
HDR pass. Nexus remains untouched.

## Previous checkpoint — GitHub 2.0.0 release preparation

User authorized GitHub publication, fully refreshed descriptions with lighting
first, the supplied screenshot/video, and Codex coauthor credit. Nexus publication
is separate, not requested in this turn. Repository variable NEXUS_AUTO_PUBLISH
was verified unset before publication; the Nexus workflow must remain inactive.

README, release notes, package README, API/provenance/validation and contributor
guidance now describe the unified 2.0 feature set and its actual limits. Screenshot
`media/screenshot1.jpg` is public with permission; the large local MP4 stays ignored.
Demo: https://youtu.be/eyRkkTXAU64. No plugin/gameplay/config changes in this pass.
Publication audit found no game binaries, captures or credential material in the
unpublished product commits; independent research remains local and unchanged.

Next: push main, verify GitHub CI, then tag v2.0.0. The existing release workflow
builds a fresh package and creates a draft; verify its asset before publishing.
Do not replace the locally tested ZIP below. Its hash records the live-tested
pre-description-refresh package, not the new GitHub build. No release published
at this checkpoint; replace this paragraph with the actual result after completion.

## Previous checkpoint — 2.0.0 startup behavior accepted

User requested version **2.0.0**, not another preview, for the forthcoming release.
User reports "läuft" after installation/restart. **2.0.0 is installed and the
packaged live data path passes.** NOT uploaded/tagged/published. PID40280 started
23:20:22 CEST; loaded bin64/CrimsonDesertTelemetry.asi disk hash matches the package
below. Current-start native log confirms exact-build/context detour and recurring
20Hz capture with paired counter/fence; bootstrap starts hostPID39732, no errors.

Read-only API control23:23:25→27 CEST: playing/tested, sequence4767→4845,
captureSequence2460→2480, frame8436→8496, light age16→15ms, malformed0;
18 authored sources and56 filtered contributions within configured radius.
Player and camera are independent, available poses; health.error=null.
These are contributions, not counts of distinct physical lamps.

User subsequently repeated startup and explicitly accepts the observed behavior:
the success notice appears as soon as data arrives, already during the visible
loading sequence. This supersedes the earlier request to wait until that sequence
has completely ended. No change requested. INI duration remains6000ms; precise
on-screen duration was not separately measured. PID/API evidence above is from the
earlier measured run, not a fresh measurement of this later startup.

Known accepted edge case: returning to the title screen without restarting can
leave HUD/data visible for several seconds, until stale or another load. Cause
unproven (continued engine activity vs state detection); user explicitly says leave
it alone because the HUD can be hidden. Do not reopen this as a release blocker.
No further plugin changes for these accepted startup/title-screen observations.
Publication status and the authorized media use are superseded by the top checkpoint.

- Native EXE hash, hook/caller guards and scene/wrapper/GPU contract now derive from
  `definitions/build-25116796.json` via CMake. Production StartCapture validates PE,
  executable sections, exact hook and three known caller/binder contexts before
  MinHook. Unknown EXEs remain barred from native game instrumentation; ABI2 unchanged.
- `check-update <exe>` is read-only/offline and never enables candidates. Current
  EXE returns exact-profile-anchors-checked: 11 matched code/RTTI anchors. Scene
  vtable has no proven relocatable fingerprint; it and authored layout remain
  unverified by this offline check. Current direct-layout automatic promotion is
  deliberately NOT implemented; no unsupported "just change SHA" shortcut.
- Build profiles reject unknown/duplicate JSON fields, missing chain offsets,
  malformed/ambiguous patterns, bad RIP bounds and inconsistent native contracts.
  Real player-chain fixtures cover identity, position, basis and replaced pointers.
- Normal startup/loading/discovery waiting messages are suppressed, without arbitrary
  loading timeout. Ready requires API playing + fresh requested data (valid empty light
  feeds count), default6s/clamped5–10s. Local bootstrap/native faults render without
  a host or validated game hooks. Errors may appear before loading; explicit
  Notifications.Enabled=0 still disables them. Unsupported graphics can prevent UI;
  retain logs. Radar, raw RGB and light API semantics were not changed.
- Recovery entry: `docs/UPDATE_RECOVERY.md`; three preserved archive tools now travel
  with product Git, reused from research af5485b. No active PSO/shader-identity gate:
  unchanged EXE + changed shader assets remains a real unclosed risk. Exposure
  normalization/capture remains separate and unfinished; evidence is below.

Verification: final2.0 managed build0warnings; 56/56 managed tests, HTTP/WebSocket
smoke, 11/11 native CTest paths PASS. Actual EXE file matches generated hook+all
contexts. Negative production StartCapture tests leave code untouched/no trampoline.
Raster tests verify silent startup/loading and local errors with HUD/host absent.
Package validator/negative cases and ZIP payload equality PASS. Reused shader
inspector reproduced ProcessManyLightsCS from preserved PASC/DXBC; no game writes.
Normal PR CI now runs native tests too. No whole-game future-update claim.

Package: `artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.0-ModManagers.zip`
SHA256 `E8A268B2B0A9D2C592A13789E0C67FC0F5C7E75BF81F09ADE7736B5784D16D12`.
Expanded: `artifacts/mod-manager/v2.0.0-20260906-231021-302-bb4227cc/CrimsonDesertTelemetry`.
ASI SHA256 `F2B62762945EC0E3A1FEDFB5B8836FB927A1DF81520A777D1F4EE6A521B1387D`.
Host FileVersion2.0.0.0; package compiled from this turn's working tree on base8ad54db
(the automatic informational-version suffix names that base, not a clean source tag).
Older versioned ZIPs are untouched. Release notes: `docs/releases/v2.0.0.md`.

Source saved in `7a33227` (Codex). Private recovery copy verified at
`artifacts/recovery/20260906-231424-3734727b/manifest.json`: product Git history
through7a33227, independent research af5485b, baseline EXE and selected shader/index
evidence. Both bundles verified; EXE copy hash matches. Same-disk copy, not off-device
backup. Original captures remain untouched; research remains clean/no remote.
Never publish the EXE/shader/index backup. No local closeout work remains.

## Previous checkpoint — update-stability audit (proposal now implemented above)

User confirmed preview.4's enlarged radar/frustum and grouped light UI in game
with a screenshot. PID was not rechecked. Product source remains `db727ca`;
this audit changes documentation only, not plugin/config/releases or game state.
No GitHub/Nexus upload is authorized by the audit request or was performed.

**Result: restart-safe and guarded against unknown EXEs, not automatically
portable for the current renderer.** Native EXE SHA gate stops game hooks before
startup on an unknown hash. The current direct camera profile also has no automatic
resolver: only historical `renderer-camera-v1` is supported by BuildCompatibility.
Actual offline `check-compatibility` on current EXE4D99...F454 fails with zero layouts,
reference24994088 missing static-position-xy-write. Exact current profile still works;
this is a recovery-coverage gap, not evidence the running telemetry is invalid.

Prioritized hardening proposal (NOT implemented):
1. Shared validated build contract for managed/native roots, layouts, hook signature
   AND surrounding register/binding provenance; a read-only update-check command
   should report individual failed anchors. Never just update the SHA allowlist.
2. Extend recovery to current direct-camera/scene paths, using existing names and
   producer chains. Native candidates must not become automatic hooks merely because
   one pattern matches. Layout/type/queue-state checks and a short live control remain.
3. Preserve a compact current recovery recipe/tools/reference manifest in product Git,
   plus private backup of irreplaceable research/artifacts. Independent research Git
   is clean at af5485b but has NO remote; product ignores research/external/artifacts.
   Existing shader names, entry hashes, seed, hook/counter chain below are valuable;
   old build-specific scripts and stale tail-validity conclusions must not be reused.
4. Add update-contract negative tests: production hook rejection (existing D3D12
   capture smoke bypasses StartCapture), current-layout relocation/ambiguity and real
   player pointer-chain fixtures. Present PR CI tests managed only; native tests run
   in release workflow. Test safe refusals in normal CI before new profile promotion.

Separate residual risk: only EXE bytes are identified. Shader-only asset changes
could keep that hash yet change 48-byte fields/counterbyte4/color semantics. No active
PSO/shader identity gate exists; numerical plausibility cannot prove field meaning.
Profile loading also lacks central schema/contract validation. Exact/trusted player
mode may retain static position when orientation validation fails (not automatically
a bad position); existing tests do not execute PlayerOrientationReader's real chain.

Checks run without new builds/instrumentation: existing managed50/50 PASS; native
hash-guard, bridge and 8-thread/80k thunk tests PASS. These do not validate a future
game version. Audit sources: BuildCompatibility.cs, BuildDefinition.cs, definitions/,
Program.cs528..533, EngineCameraReader/EngineLightReader/RenderLightReader,
native build_guard.cpp/instruments.cpp/render_capture.cpp/filter_thunk.asm;
research recovery entry points and current native/shader evidence remain below.

One next step: agree/implement the bounded current-build recovery/contract hardening
before publishing. Exposure capture remains approved but separate and unfinished.

## Previous checkpoint — larger 3D radar and light detail groups

**1.3.0-preview.4 is built, tested and packaged for DMM.** At packaging time the
installed game was PID33348 / preview.3 (superseded by the user's preview.4 confirmation);
no game files, capture hooks, raw API records or RGB normalization were changed.
One next step: user closes the game, installs the new DMM ZIP,
then checks the fire detail panel and camera pitch in the larger radar.

- Radar uses the full panel width (logical radius205, previously80); normal panel
  height550, diagnostics806. Root/camera/FOV/XYZ readouts sit below the radar.
- Camera frustum uses measured forward/right/up, vertical FOV and aspect in the
  same affine 3D projection as light positions. XZ ground, Y height; no clamped
  vertex heights or elevated flat yaw wedge. Length .4*radarRadius is schematic.
  Missing/invalid projection metadata hides the frustum, not guessed geometry.
- Nearby detail cards merge by pairwise distance<=.15gu (complete-link, no chain),
  only as presentation. Separate raw contribution values stay in spatial order;
  no physical-object IDs, sums, pulse classification or smoothing are invented.
  At most64 detail candidates/four visible contributions per group; raw API records stay.
  GPU slots appear only with F9 diagnostics. Fixed card width avoids digit-width
  jitter; compact values retain HDR/tiny magnitudes and small viewports scale cards.
- All six UI/client test paths passed: model, general HUD, notifications, combined
  lights, lights-only and WebSocket. Raster checks include a .03gu pair with an
  independently changing second row, pitch/roll with unchanged yaw, missing camera
  basis, isolated near/behind clipping, stale clearing, resize and 800x480/4K.
  Pitched4K and compact screenshots visually inspected; live game check pending.
  Relevant source: overlay_hud.cpp, overlay_model.{h,cpp}, overlay_tests.cpp,
  graphics_smoke.cpp. Package defaults/hotkeys remain unchanged.

ZIP: `artifacts/mod-manager/CrimsonDesertTelemetry-v1.3.0-preview.4-ModManagers.zip`
SHA256 `8D8FB31CBCE31979200541C7454D44C52BADC67E2AF99EAA2310F097C6DFBBC6`.
Expanded: `artifacts/mod-manager/v1.3.0-preview.4-20260906-222529-708-5a75c437/CrimsonDesertTelemetry`.
Package validator/negative cases passed; older immutable ZIPs preserved.
Screenshots: `build/light-overlay-preview4-{small,4k,pitched4k,compact}.bmp`
(synthetic fixtures, NOT game screenshots). Source research repository unchanged.

**Exposure remains a separate approved follow-on, not included in this HUD build.**
Bounded read-only check22:13:52 CEST PID33348: ExposureOwner+D8 contains FOUR plausible
exposure float4; +118 already contains pointers, so never blindly read80 bytes.
Bridge frame40441, unchanged seqlock101142, validCount42: CPU E before/after
.08051319/.08049921 predicts mode1 factor1.32961224; blue lamp measured1.32911805
(-.0372%). Strong candidate, still NOT frame-paired upload provenance. Missing
proof: how this CPUblock reaches the bound Exposure-CBV for the sampled frame.
GPU copy also lacks CBV suboffset/heap/state proof; never assume the light UAV state.
Do not revisit generic GPU searches or silently divide all effects by this value.

## Previous checkpoint — filtered light tail fix and fire diagnosis

## Result / one next step

**1.3.0-preview.3 is installed in PID33348; user reports the result is already great.**
The actual GPU valid-prefix counter is now captured with its light buffer and
the managed reader decodes only that prefix. No hardcoded33, color/motion heuristic,
smoothing or new hook. The miniHUD root arrow/camera cone are larger, outlined,
and drawn above the light dots; dots retain measured HDR-derived color swatches.
UI change commit `673651c`; counter fix `ff1f85b`.

Current discussion: usable HUD/API presentation for overlapping fire contributions.
Agreed direction (not implemented): retain raw current contributions and
offer a separately labelled grouped summary only with defensible membership;
do not collapse nearby sources blindly or claim source-RGB sums reproduce pixels.
Actual appearance also depends on spatial/angular attenuation, visibility,
materials, indirect light and tone mapping. Source-derived summaries and a
view-dependent Hue estimate must remain distinguishable from measurements.
No code/config/game changes. Controlled preview.3 movement recording remains pending.
The user approved paired exposure capture, then raised the fire HUD's usability.
Next technical step: capture the matching ExposureConstantBuffer alongside the
existing light/counter sample to validate its scalar. Separately, GPU-slot numbers
must not act as persistent HUD identities; do not smooth away real fire variation.

### Fire HUD diagnosis — 21:57 CEST

User screenshot identifies the fire at (-10507.66,610.94,-4368.33), two nearby
SPOT contributions with very different amplitudes. Read-only API recording:
`artifacts/light-research/fire-two-contributions-20260906-215755.jsonl`, eight seconds,
PID33348/preview.3; 482 playing rows, 480 available/two bridge-changing, 124 distinct
captures35782..35905, native frames56555..56978. Player fixed, camera Y bob .00671.
Both fire contributions occur in EVERY available capture; assign by disjoint
height bands, not sampleIndex. Lower Y610.91943..610.9313: luminance .12450..18824,
116/123 index changes; upper Y610.94904..610.9607: .45831..91976, 121/123 changes.
Each spans 37 GPU slots. Their source-RGB luminance sum varies .63132..1.04581;
it does not cancel to a constant. Blue glass is always slot1 here, luminance
2.58732..2.66662 (3.1% span), while the fire sum spans 65.7%. Fire variation remains
after division by the blue control; it is not explained by that common factor.
Aggregate fire RGB ratio is roughly 1:.303..310:.0754..0763 (mostly amplitude).
This spatial pair is an experiment association, not a generic physical-light ID.

Code confirms sampleIndex is simply the GPU valid-prefix index. HUD independently
re-sorts and places labels by crosshair distance every frame, so near contributions
can exchange screen boxes. However, exact projection with each envelope camera
shows ZERO pair-rank swaps here: the upper contribution is closer to the crosshair
in 124/124 captures and 480/480 available API rows. A box-swap explanation is not
supported for this recording. Normal HUD should not foreground transient slots;
one stable detail panel can retain individual current values without claiming an
unproven grouping or pixel-accurate combined brightness. No plugin/config edits.

### Exposure cause and spot labels — 21:37 CEST continuation

The upstream shader is identified: `InjectLightsCS` entryhash b606b219,
`InjectLightGroupsCS`05125ef9. Local artifact prefix
`artifacts/light-research/light-rgb-inject-20260906-2215-<entry>` (.ll/.json/.dxbc/.padxil;
filename2215 is a label, not the measured live time). InjectLights LL458..506:
q=1/max(0.0001,ExposureConstantBuffer._exposure0.x), b21/space35 byte0.
Mode0 factor1; mode1=min(max(.01,q),.1+9.9*saturate(.01*q)); mode>=2=clamp(q,.05,150).
SceneCB byte2748 bit1 adds multiplier.1 when set, else1. Do not infer that bit's
meaning just from packed name `_isPhotosensitiveMode_isAllolwBlood`.

Current CPU pack1438AF200 writes only SourceRGB(+3C)*SourceScale(+4C) to GPU+10.
1438AF322..351 encodes GPU+3C as def+75<<1 when def+74 enabled, OR prioritybit;
shader >>1 recovers mode directly, no+1. Read-only21:37:26 PID33348, stable18record
source vector: all THREE selected anchor definitions have useExposureAdaptation=1,
mode=1, four flicker floats+60..6C allzero. Blue def46A32A4B550, warm46A32A4C368,
crystal46A32A4B7E0. AuthoredRGB/scale match the prior series. At21:38:01 coherent
bridge frame60149 SceneCB byte2748=1: bit1 unset, no extra .1 dimming.
Thus shader + active lamp settings establish the exposure-adaptation route.
Observed common factors imply exposure0.x~23.9..26.2 in the active curve branch;
this is INFERRED, not directly sampled/frame-paired exposure.

Exposure binding: filterowner+10→renderer+690→exposureOwner+C0→wrapper+30→inner.
Current owner46AF0028C00, inner46AF1597C40, resource(inner+168)=195830640.
The resource is a readable COM object (not a proven GPU VA). Map method1437ACBE0
uses cached CPU pointer inner+158; cache isNULL. No Map call/hook/new capture run;
no currently available CPU Exposure0 value via this checked path. Not proof all
possible CPU copies are absent. Plugin remains preview.3 unchanged.

Spot question, API21:37:01/capture17210/frame57240/age15ms:68records=44spot+24point;
nearest5gu=3spots, nearest10gu=5spots. Blue and warmglass match authored SPOT
half-angle26.997278deg/downward; crystal POINT. Two nearest warm records around
(-10507.66,610.9584,-4368.328) /(-10507.6455,610.92175,-4368.327) are actual
SPOT27.109184deg/downward. Do not assign a physical object solely from proximity.
HUD correctly reports decoded types, not all lights as SPOT. Internal spotlight
representation does not promise a visually obvious narrow beam.

### Live RGB diagnosis — 21:26 CEST

Artifact `artifacts/light-research/overlay-blue-rgb-diagnostic-20260906-2126.jsonl`,
21:26:07.933..15.863:483 playing API rows,481 rendered available/2 bridge-changing,
120 distinct captures, native frames25140..25533. Player/camera direction fixed;
camera Y varies0.0157. PID33348 started21:17:34. Separate coherent bridge check:
ABI2/flags15, frame28097, validCount68 (input counter0=2693), bank1,
output18DD79DB0/counter18DD79520. No counterless fallback or old bank-count pattern.

Blue glass (-10510.692,611.6332,-4371.4375), warm glass
(-10493.734,611.61084,-4364.254) and crystal(-10528.074,611.3604,-4354.011)
are each present exactly once in120/120 captures; positions exactly stable.
Authored colors and rendererScale stay bit-identical in483/483 rows:
blue1.977898, warm7.5021653, crystal3.0008664.

Blue rendered luminance ranges0.061520785..0.067384094 (~9.53% max/min),
largest adjacent step0.000529051 /0.8511%, median absolute step0.000124909.
Start0.06157888→end0.06684646, small reversals, no parity alternation.
Normalized blue RGB stays effectively constant: R/B~0.7190869,G/B~0.8528073.
All nine RGB channels across the three anchors fit
`renderRGB = commonFactor * M * authoredRendererRGB` to1.285e-6 relative spread;
commonFactor0.038209728..0.041851336, anchor luminance correlations>0.9999999995.
This proves common scaling of these controls, not a changing blue hue or source
swap. Each anchor changes sampleIndex23 times/seven slots; never use it as identity.

Shader evidence (same Process LL below): ordinary nonnegative input RGB receives
a static 5%-luminance floor and constant matrix
M=[[.61312,.33951,.04737],[.07020,.91636,.01345],[.02062,.10958,.86980]].
No dynamic multiplier on that path. ExposureConstantBuffer is read only in its
negative-RGB special route. At this earlier checkpoint the upstream cause was not
established; the later InjectLights findings above supersede that uncertainty.
Older blue-glass factor-of-two report at research handover3733 was real amplitude
evidence but never proof of pulsing. Current HUD shows renderer-scaled values,
not just the constant authored color; luminance is derived from those same RGB.

### Why this bound is the engine's, not a visual heuristic

Current EXE function0x143CB5000 selects matching counter/output wrappers from
owner+0x638/+0x648 using owner+0x8F8. At existing hook0x143CB65CA, R12=output,
original R15=counter, R13=owner, RBX=command wrapper. The ASM thunk now passes
all four; original R15 is read from [r15] after R15 becomes the saved-stack pointer.
Both GPU copies are on the same command list before the same submission fence;
resource refs and capture-time identities survive through publication.

Game archive shader `ProcessManyLightsCS` atomically increments **byte4 / DWORD[1]**,
using the previous value as its output index (stride48, capacity32768).
Independent consumers `InitSortingDataCS` and `InitSortingDataIndirectCS` read
byte4 and give only indices<count valid sorting keys; the tail receives -1.
`SetDispatchIndirectArgumentsRecursiveCS` uses CPU literal _srcStartIndex=4
as a BYTE offset, not DWORD[4]. DWORD[0] and the later consumer's DWORD[2] are
not this filtered-prefix length. A special input.color.w>99999 producer route
can write at input index without incrementing; it does not enlarge consumer range.

Reproducible archive lookup: CrimsonForge `hashlittle(case-sensitive UTF8name,
0xC5EDE)` matched three known entry controls. Entry hashes:29588660 (Process),
84574902 (InitSorting),5eecfe7e (Indirect),a2b2b7e9 (DispatchArguments).
Extracted variant paths `shadercache__/63bb3e83_9d3ccf48_5_<entry>_3_deba1dcd_b13a9f29.padxil`,
group0017. Evidence prefix `artifacts/light-research/filtered-count-exact-20260906-2113-<entry>`:
.padxil/.dxbc/.ll/.json. Process shader hash c40f89b9b04627219c4b77a1736da3b7;
LL1075 append, LL1101 output; InitSorting LL86 load, LL169 prefix bound.
One archive variant per kernel inspected, not every permutation/live PSO hash.

Native bridge ABI v2 appends256 counterbytes after lights; header88/96/104 stores
output/counter/owner addresses,112 bankindex (UINT32_MAX unknown),116 counterBytes.
Flags15 require paired counter. C# rejects v1, absent identities and counts>32768;
zero is a valid empty current list. Existing public JSON schema1.4 is unchanged.

Verification: 50/50 managed tests; native bridge concurrent publication test;
capture/thunk CTests2/2 plus foreign-device rejection; 80,000 parallel thunks
preserve registers/flags and all four arguments. D3D12 test holds GPU execution
behind a fence, verifies all counterbytes, alternating resource pairs and frozen
capture-time identities, rejects missing/undersized/non-UAV/aliased counters.
Graphics smoke passed and small/4K preview rendered; small image visually checked.
Package validation passed. These tests do not replace the pending game check.

### Live diagnosis — 20:40 CEST

Video reviewed in extracted frames:
`C:\Users\fabia\Videos\NVIDIA\Crimson Desert\Crimson Desert 2026.09.06 - 20.36.51.02.mp4`.
Player moves without an intentional camera pan. #41/#170/#192 shift together
by about(-5.96,+0.16,+0.89) world units while a lamp contribution near
(-10491.68,610.89,-4370.00) remains world-fixed. These are not just jumping labels.

Read-only artifact `artifacts/light-research/overlay-static-lamp-diagnostic-20260906-2037.jsonl`
was actually recorded20:40:01.903..05.791:151/151 playing/available rows,57 distinct
captures/native frames29329..29521, zero malformed/unavailable. Player XYZ and
camera X/Z/basis/FOV are constant; camera Y bobs0.01826. Native frame parity
separates two exact populations:

| Native frames | Captures | Published / within35 | Frozen tail, indices>=33 |
|---|---:|---:|---:|
| Odd | 27 | 332 / 236 | 299 |
| Even | 30 | 335 / 222 | 302 |

Only indices0..32 change RGB. Every tail record has exactly frozen RGB within its
parity group and camera-relative XYZ spread<=0.0001001; world Y follows camera
bob. #41/#192 match the video. Known glass anchor(-10493.734,611.61084,-4364.254)
remains world-fixed57/57. Strong stale-tail/alternating-buffer evidence, not proof
that every constant-color light is invalid. Actual GPU resource identities/count
are not exposed yet. Producer+recorded transport age103..349ms; the recorder
skipped84 API sequences but saw57/58 captures (not proof of HUD packet loss).

Preview.2 code confirmed the gap: native `render_capture.cpp` copied every accepted matching
48x32768 resource without publishing its identity or valid count. Managed
`RenderLightReader.cs` scans all32768 slots, treating position.w≈pi as validity.
The old pi criterion in `GPU_LIGHT_LAYOUTS_25116796.md` came from an UNFILTERED
buffer observation, not proof of current filtered-tail lifetime. A completed GPU
copy and paired scene camera do not prove every copied slot was rewritten.
Separately, HUD focus rank and collision-based label placements are stateless,
so changing neighbors/indices also make labels jump; fix presentation separately.

## Package / controls

Immutable ZIP:
`artifacts/mod-manager/CrimsonDesertTelemetry-v1.3.0-preview.3-ModManagers.zip`
SHA256 `5F107B18B5B1677A8366F6102503A1077BEF6FA4D8865E452F09C45A8B203372`.
Expanded:
`artifacts/mod-manager/v1.3.0-preview.3-20260906-211037-749-5c0dd6b7/CrimsonDesertTelemetry`.
ASI SHA256 `B45170BD2F1BC691C1902CEF2993ABE28C83443CF79C791893F0F26E66916CF2`.
Previous preview.1/2 ZIPs remain unchanged for rollback. Never overwrite releases,
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

## Previous preview.2 HUD verification

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
