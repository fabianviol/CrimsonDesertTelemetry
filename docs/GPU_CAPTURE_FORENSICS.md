# GPU capture forensics — self-contained handover, 2026-09-11

Written so that someone with no memory of this session can continue. It covers the
toolchain built for reading a PIX capture offline, and the ambient-light
investigation that toolchain was built for. Read this first, then use
`docs/TOOLING.md` for the machine's paths; read this instead of the layered checkpoints in `docs/HANDOVER.md`,
which record how the conclusions were reached, in the order they were reached,
including the ones that were later withdrawn.

The one habit that matters: **every strong claim below was tightened or reversed at
least once by an outside review.** Distinguish established from hypothesis from
withdrawn before building on anything, and say which you are doing.

**Picking this up cold? Go straight to section 4b.** Both product goals are now
answered; it says how far each one reaches, what is deliberately not being pursued,
and what is genuinely next. Sections 1-3 are the toolchain and the identities;
section 4 is the ledger behind the claims.

---

## 1. Orientation

The goal behind all of this is a lighting companion that dims ambient light under
structures and occludes fires and lamps. The engine's own answers are being read out
of the GPU rather than reinvented.

Read order: this document for the authoritative GPU/PIX state; `docs/TOOLING.md` for
the existing tools; `docs/HANDOVER.md` for product/runtime checkpoints; and
`docs/LOCAL_ILLUMINATION_RESEARCH.md` only for deep evidence and history. Do not
continue from a historical claim when this document marks it hypothesis, withdrawn
or superseded.

| what | where |
|---|---|
| product repository | `C:\DEV\CrimsonDesertTelemetry` (this one) |
| the capture | `artifacts/light-research/pix-captures/CrimsonDesert_lantern_2026-09-05_2346.wpix` |
| its C++ export | `artifacts/light-research/pix-provenance-20260909/cpp` (2.7 GB, out of Git) |
| working outputs | `artifacts/light-research/cbv-moment/` |
| shader disassemblies from the game archives | `artifacts/light-research/*.ll` (79 of them) |
| the running research log | `docs/LOCAL_ILLUMINATION_RESEARCH.md` |

Nothing under `artifacts/` is in Git. The `.ll` files there came from the game's
shader-cache archives; the ones under `cbv-moment/` came from the capture itself, and
only the latter prove what a frame executed.

---

## 2. The toolchain

Four scripts, each solving one link in a chain that ends in a provable statement about
a frame. 123 unit tests cover them: `python -m unittest discover -s tests/scripts`.

### `Read-PixExportResource.py` — bytes out of a capture

`export-to-cpp` puts every resource's contents in `resources.bin` with **no index**.
It is a bare concatenation of XPRESS-compressed blocks consumed in program order by
`ResourceReader::Read(buffer, compressedSize)`, so a block's file offset is the sum of
every compressed size read before it. The script reconstructs that order from the
generated source and decompresses one block through `Cabinet.dll`.

```bash
E=artifacts/light-research/pix-provenance-20260909/cpp
python scripts/Read-PixExportResource.py $E --list-cbv 1024
python scripts/Read-PixExportResource.py $E --cbv 15728 589824 --length 1024 --out out.bin
python scripts/Read-PixExportResource.py $E --resource 15739 --out out.bin
```

Self-checking: a wrong order makes XPRESS fail rather than return plausible bytes. The
reconstructed sequence accounts for 2,705,577,794 of the file's 2,707,270,671 bytes,
the remainder being later render-phase reads.

- A CBV names the **heap** for placed resources, so `--cbv` resolves heap+offset to
  whichever placed resource covers it.
- `--list-cbv` searches **descriptor creations only**. D3D12 also binds constant
  buffers as root CBVs by address, with no descriptor to find, so a listing is never
  an inventory. This capture records **no `SetComputeRoot32BitConstants` at all**, so
  even things named "push constants" arrive as root CBVs here.
- This solves payload access, which is what buffers need. It is **not a texture
  decoder**: a texture also needs footprint, row pitch, subresource layout, format and
  any tiling before its bytes mean anything.

### `Map-PixExportShaders.py` — pipeline states to shader names

`CreatePSOs.cpp` reads each pipeline state's bytecode from `resources.bin`, and a DXIL
container keeps its entry name as a NUL-terminated string.

```bash
python scripts/Map-PixExportShaders.py $E --out map.csv
python scripts/Map-PixExportShaders.py $E --grep Ambient
python scripts/Map-PixExportShaders.py $E --extract 22283 --out pso.dxbc
```

**236 of this capture's 287 compute pipeline states are named.** Absence from the map
is therefore not proof of absence — for a negative, do a direct byte search for the
name across all blobs.

`--extract` writes the shader container **the frame actually ran**, which removes the
standing caveat that an archive variant is not proof of what was bound. Disassemble it
with the DXC call in `docs/TOOLING.md`:

```bash
dxc.exe -dumpbin pso.dxbc -Fc pso.ll
```

Trap already paid for: this engine names entries either with a stage suffix
(`RenderDiffuseCS`) **or a lowercase stage prefix** (`csPrecomputeAmbient`). Matching
only the first silently drops the entire atmospheric-scattering family. A unit test
now pins both.

### `Find-PixExportDispatches.py` — candidates only

```bash
python scripts/Find-PixExportDispatches.py $E --resource 15739 --view uav --map map.csv
```

Reports dispatches that bound a descriptor table whose **base** views the resource.
That is a candidate filter, nothing more. **It produced a false positive here** —
`ClearVoxelsBufferCS` looked like the writer of the ambient buffer and was not. Never
claim a shader touches a resource on this alone.

The generated helper suffix is not restricted to letters: texture views use names
such as `CreateShaderResourceView_Tex3D`. Both descriptor parsers accept digits in
that suffix, with SRV and UAV `_Tex3D` regression tests. The older `[A-Za-z_]*`
pattern silently hid these bindings and must not be restored.

### `Resolve-PixExportBindings.py` — the actual binding

```bash
python scripts/Resolve-PixExportBindings.py $E --pso 22274 \
    --register u5,space39 --register u15,space39
```

Walks the whole chain and is the tool to trust:

```
shader register (u15, space39)
  -> root parameter and descriptor range in the PSO's root signature
  -> OffsetInDescriptorsFromTableStart, with OFFSET_APPEND resolved
     against the running end of the preceding ranges
  -> descriptor heap index
  -> the descriptor AS IT STOOD at that point in the frame
  -> resource
```

Time accuracy is **not optional**: `RenderFrameWorker_000.cpp` interleaves 10307
`ModifyDescriptors_*()` calls, one descriptor each, with `PopulateCommandList_*()`
calls. Resolving to a slot's final contents would be wrong.

Its regression test is the false positive above — `ClearVoxelsBufferCS` must resolve
u5, u6, u15 to resources 15789, 206 and 15787, none of them 15739.

---

## 3. Resource and shader identities in this capture

Established by the tools above.

| id | what |
|---|---|
| resource **15739** | the 1024-byte ambient buffer, `PrecomputedAmbientConstantBuffer`; heap 15728 offset 589824; 14 UAV descriptors, each `FirstElement 0, NumElements 64, StructureByteStride 16` |
| resource **15741** | a 16 KB cache, `g_precomputedAmbientCacheUAV`; heap 15728 offset 655360; `NumElements 1024, StructureByteStride 16` |
| resource **15411** | a camera: position `(-10487.69, 605.58, -4436.77)`, unit direction `(0.9406, 0, -0.3396)`, and `30` |
| resource **224** | 32 pairs of world-space triples |
| ApiObjectId **190** | TEXTURE3D 64x32x264 R8, the sky-visibility volume |
| ApiObjectId **191** | TEXTURE3D 128x64x1040 R16, the signed distance volume |
| ApiObjectId **211** | TEXTURE3D 64x32x512 RGBA8, the axis-aligned distance companion |

Engine names for the volumes, from `EvaluateDiffuseRadianceCS`:

```
t232, space36   g_skyVisibilityVoxelsTexturesLikeUav
t233, space36   g_signedDistanceVoxelsTexturesLikeUav
t224, space36   g_axisAlignedDistanceTextures
t234, space36   g_environmentColor   (TextureCube)
```

Pipeline states worth knowing:

```
21565  GenerateAxisAlignedDistancePass0_CS
21566  GenerateAxisAlignedDistancePass1_CS
22274  ClearVoxelsBufferCS
22283  csPrecomputeAmbient
22306  SkyMaterialCS
22313  GenerateAtmosphericScatteringDispatchIndirectArgumentsCS
22314  csRenderAtmosphericScattering        (also 22337)
22408  EvaluateDiffuseRadianceCS            (also 22409)
22565  RenderDiffuseTiledCS                 (also 22566)
```

**`GenerateAmbientFromEnvironmentAtmosphericScatteringCS` is absent** from all 287
blobs by direct byte search — it did not execute in this capture, though every sibling
did.

---

## 4. Findings ledger

### Established

**The distance-resource dependency is now resolved at the actual dispatches.** The
time-accurate binding resolver gives:

```
GenerateAxisAlignedDistancePass0_CS, pso 21565 (two dispatches)
    t66, space36  -> resource 191
    u14, space38  -> resource 211

GenerateAxisAlignedDistancePass1_CS, pso 21566 (two dispatches)
    u14, space38  -> resource 211, in place

EvaluateDiffuseRadianceCS, pso 22408
    t224, space36 -> resource 211
    t232, space36 -> resource 190
    t233, space36 -> resource 191
    t234, space36 -> resource 14529
```

So t224/resource 211 is **derived from t233/resource 191**, not an independent
occupancy truth. The captured Pass0 reads each 2x2x2 fine-SDF neighbourhood. With
`cellSize = 0.25 * 2^level`, it sets the upper six bits of `w` to 63 when any sample
is within about `1.05 * cellSize`, bit 0 for the looser `2.10 * cellSize` test, and
bit 1 for overlap with one of the character-occlusion AABBs. Pass1 uses the low two
bits as seeds, computes six axis-aligned run distances clamped to 15, and packs each
plus/minus pair as two nibbles into x, y and z while preserving w.

The reconstructed archive `RaymarchLocalLightsCS` therefore has this structure:

```
t233       distance / stepping / coverage
t224.w>>2  gate derived from fine-SDF proximity
t224.xyz   six coarse axis-aligned distances, used numerically
```

It does **not** use t224 as the final occupancy/hit decision. The shader name is
absent from all 287 captured PSO blobs, so this is an archive-shader reconstruction
combined with captured resource identities, not proof that this frame dispatched
that raymarcher.

**The consumer snapshot has one exact valid point.** Pass0 dispatches are GlobalIds
743 and 745; Pass1 dispatches are 747 and 749. Evaluate follows at 13547. Resource
191 is not written again until 13902 (`ClearInvalidSignedDistanceVoxelsCS`), then
14058 (`AccumulateInjectDataSingedDistanceOnlyCS`) and 14137
(`ClearIncompleteSignedDistanceVoxelsCS`). Therefore the paired state to validate is
immediately after GlobalId 749, not an arbitrary point before Evaluate.

`resources.bin` cannot provide that state: it contains the replay's serialised
initial payload. The reproducible acquisition route is to instrument
`PopulateCommandList_21556_1_2()` directly after the second Pass1 `Dispatch(4,2,4)`:
transition resource 191 from shader-resource and 211 from unordered-access to
`COPY_SOURCE`, use `GetCopyableFootprints` and `CopyTextureRegion` into dedicated
readback buffers, then restore both layouts. After the existing queue fence completes,
map and dump the two buffers. This plan names the precise event and command-list
location; the event-state bytes have **not yet been acquired**.

**The engine multiplies environment radiance by our own quantity.** In
`EvaluateDiffuseRadianceCS`:

```
s   = SampleLevel(t232 skyVisibilityVoxels, u, v, w).x
vis = saturate(1 - s) * 0.03125             (other branch: bare 0.03125)
env = SampleLevel(t234 g_environmentColor, -d.x, d.y, -d.z, LOD 4).rgb
out = env * (vis * cb[0].w)
```

So `saturate(1 - sample)` is the engine's own expression, not our inference, and
`s = 0` is full sky visibility. The w coordinate is scaled by `1/264`, the depth of
ApiObjectId 190 — tying the shader binding to the captured resource by a constant
neither derivation shared.

**The ambient buffer is 1024 bytes laid out as eight sets of eight `float4`.** Within
a set, slots 0..5 are three colour channels of two `float4` each and slot 6 carries
each channel's ninth value in x/y/z, giving **three channels of nine coefficients**.
DXC's groupshared array sizes predicted this before any byte was read, and the bytes
matched.

**The SH basis order, from the producer's own constants** — an identification, not an
inference from magnitude. All six distinct constants are the real spherical harmonic
normalisations to float32 (worst deviation 1.2e-06):

| lane | expression | basis |
|---|---|---|
| 0 | `0.2820950` | Y00 |
| 1 | `-0.4886030 * d.y` | Y1,-1 |
| 2 | `+0.4886030 * d.z` | Y1,0 |
| 3 | `-0.4886030 * d.x` | Y1,1 |
| 4 | `+1.0925480 * d.x * d.y` | Y2,-2 |
| 5 | `-1.0925480 * d.y * d.z` | Y2,-1 |
| 6 | `0.9461759 * d.z^2 - 0.3153920` | Y2,0 |
| 7 | `-1.0925480 * d.x * d.z` | Y2,1 |
| 8 | `0.5462740 * (d.x^2 - d.y^2)` | Y2,2 |

Several real-SH sign conventions exist, so the engine's is simply the one written
here, with z as the polar axis. `scripts/Decode-AmbientSH.py` applies it.

**The SH producer's structure.** 4096 samples per entry (16x16 threads, `threadId << 2`,
two 4-iteration loops; independently, the NDC scale of 1/32 needs 64 steps to span
[-1,1]). Directions come from unprojecting a 64x64 NDC grid at the far plane through
literal rows 30..33 of the 2768-byte `SceneConstantBuffer`; radiance is
`g_texSkyInscatter` by `textureLoad` at mip 0, clamped non-negative. Thread 0 scales
the 27 reduced values by `1/6144` into `_renderFlags.x * 8 + 8`; a six-iteration loop
then sums six such entries and stores that sum **unscaled** into slots 0..6.

**No solid-angle weight in that shader.** Every sample carries the same scalar weight
with no position-dependent correction of any kind. The stored values are therefore
moments against the L0-L2 basis, not canonical spherical harmonic coefficients.

**`csPrecomputeAmbient` writes slot 56**, one `float4`: the sky's Mie scattering
reduced over 256 threads, scaled by `4*pi/256`. Its 256 directions are a **Hammersley
point set** — `z = 1 - i/256`, azimuth `bitReverse32(i) * 2*pi/2^32`,
`dir = (r cos, r sin, z)` — uniform in area, low discrepancy, no Jacobian missing.
The samples cover only the upper hemisphere, whose natural equal-area weight is
`2*pi/256`. The reason this Mie-summary store uses the additional factor two is open;
antipodal or symmetry handling has not been demonstrated.

**Two shaders use different direction sets and stored scales.** The Mie summary uses
an upper-hemisphere Hammersley set with applied total scale `4*pi`; the SH producer
uses a projected grid with total scale `4`. They are different stores and domains,
so neither establishes the other's integration measure. In particular, the Mie
store does not prove canonical spherical quadrature for the Ambient/SH path.

**The cache (resource 15741) is not harmonics.** Stores go to `threadId.x * 4`, `|2`
and `|3`: a four-`float4` stride over 256 entries, one per thread, colour triples with
flags.

### Hypothesis, with an exact numerical fit

**`1/6144 = (4/4096)/6`.** `4/4096` is exactly the uniform Riemann weight of one cell
of a 64x64 grid over the area-4 square [-1,+1]^2. So the normalisation is exactly
compatible with **six equally weighted 64x64 integrals**, and if the six entries are
cube faces that is very natural — and would explain the absent Jacobian outright, the
engine integrating in the face parameter space rather than the spherical measure.

It presupposes what is open. `1/6144` also factors as `(2/3)/4096` and as
`(4*pi/24576)/pi`; the constant alone proves nothing about the six.

### Withdrawn — do not resurrect these

- **A Lambertian reading of the `/pi`.** A cosine convolution is band-dependent
  (`1`, `2/3`, `1/4` after dividing by pi), so one global scalar cannot be it. The
  producer's arithmetic settles this without any search; separately, no literally
  encoded standard convolution constant appears in the 79 archive listings.
- **Slot 56 corroborating the direct term's hue.** Both derive from the same
  environment state. The description "a bare RGB triple" turned out to be right, but
  the corroboration argument built on it was not.
- **`ClearVoxelsBufferCS` as the writer of the zeros.** Refuted by resolving its
  registers: u5, u6, u15 go to resources 15789, 206, 15787.
- **`csPrecomputeAmbient` computing harmonics of its own.** Refuted by resolving u3
  and reading the stores.
- **"No runtime state could look like that."** 14 UAV descriptors cover the whole
  buffer; a writer was always possible.

---

## 4b. Occlusion — handover for Codex, 2026-09-11

### Where this stands, in one paragraph

Both product questions are answered. Ambient dims correctly under cover, and a
camera-to-light sphere trace through the engine's own signed distance field separates a
lamp behind a wall from the same lamp in the open. Neither needs more renderer
archaeology. What is left is product work in CrimsonHue — which consumes neither feed
yet — plus the limits recorded below. Everything in this section was measured in the
running game, not inferred from the capture; where something is an observation rather
than a check, it says so.

### Goal 1: ambient under cover — done, parked

`skyMean x cameraSkyVisibility`, both raw inputs retained alongside it, published on
`/v1/ambient` as `localEnvironmentAmbientEstimateWorking`. Measured 2026-09-10:

| place | camera sky visibility |
|---|---|
| the Abyss, high above the world | 0.62 – 0.86 |
| open ground | 0.27 – 0.39 |
| beside the barn, under trees | 0.17 – 0.31 |
| deep inside the barn | 0.000025 – 0.014 |

A barn traverse gives 15550x on visibility and 17902x on the estimate while the sky term
holds between 26.0 and 38.4 with no trend, so the collapse is entirely local. The value
follows camera POSITION, not view direction. That open ground reads about a third rather
than one is the best support for reading it as a solid-angle fraction.

**Open, and it belongs to CrimsonHue:** the mapping to lamp brightness. A visibly open
barn still reads 0.000025 inside, so a linear product drives a lamp to black where a
person would say "much darker". It needs a perceptual curve with a floor. Do not reopen
the atmosphere analysis to improve this; it buys nothing for the product.

### Goal 2: lamp occlusion — demonstrated for the controlled case

Nineteen camera-to-light traces at the player home against a lit doorway at
`(-10403.25, 613.84, -4419.10)`, identified by world POSITION and never by the rendered
sample index. Grouping by the camera position the copies themselves carry — the phase
labels alone were not poses, because the 15 s windows spilled across the user walking —
resolves into exactly three poses, unanimous within each:

| camera z | copies | verdict | closest approach |
|---|---|---|---|
| −4422.90 | 3 | clear | +0.304 … +0.320 |
| −4417.72 | 10 | **blocked** | −0.0000 … −0.0068 |
| −4424.09 | 9 | clear | +0.454 … +0.510 |

One fixed parameter set, set before the occluded phase was examined and not tuned
afterwards: `hit_tolerance=0.0`, `minimum_step=0.05`, `iteration_bound=400`,
`start_offset=0.6`, `end_margin=1.0`. Full write-up in
[SDF_VARIANT_A.md](SDF_VARIANT_A.md); evidence at
`artifacts/light-research/variant-a-pid15940-20260910-2351/`. The three preserved
payloads carry their own cameras, so the whole result reproduces offline with the
shipped tool — the exact commands are in that document.

**The margin is thin, and that matters.** In the blocked pose the ray grazes the wall at
−0.0000 to −0.0068 gu rather than driving through it. A tolerance sweep holds the same
classification from −0.05 to +0.10, so there is a working band, but a thinner wall or a
shallower angle could plausibly fall the other way. Do not read the pass as a margin.
Nothing is established for thin geometry, doorways at grazing angles, moving occluders or
other materials — untested, not known-bad.

**Stopping rule, still in force.** No `t224`, no reconstruction of the engine's raymarch,
no coverage model, no DXR. `t224` in particular is derived from `t233` via
`GenerateAxisAlignedDistancePass0/1`, so it is not independent truth. These return only if
Variant A visibly fails on a concrete case.

**The cheap alternative is closed by measurement.** Two logged runs with torch-carrying
NPCs walking behind geometry, the user calling each occlusion: no contribution is ever
attenuated, and the surviving lights sit in the same 0.27–0.39 band at every mark. What
happens instead is that lights leave the list, and a departure cannot be told apart from
view filtering or range. Scope that correctly — this feed does not represent usable
attenuation, which rules it out for the product. It is NOT a claim that the engine never
attenuates internally; that was not tested.

### The signed distance volume, calibrated

Resource **191** in the capture, `t233` / `g_signedDistanceVoxelsTexturesLikeUav`,
`TEXTURE3D 128x64x1040 R16_TYPELESS`, SRV as `R16_FLOAT`, committed with
`ALLOW_UNORDERED_ACCESS`, 17,039,360 bytes — about 16.25 MiB against the sky volume's
528 KiB.

All of the following is measured from live payloads and their own GI constants:

```
value        a signed distance in GAME UNITS
sign         negative is inside          (from a profile descending through a surface
                                          at y ~ 606.72, not assumed)
addressing   z_texel = level * 130 + 1 + Frc(normalised) * 128,  coord_z = z_texel / 1040
             world -> normalised uses the inverse extents at 0x10, scaled by 1 / 2^level
cellSize(L)  0.25 * 2^L                 (float[0] = 0.25; the per-level w at 0x140 + 16L
                                          is 1/cellSize, running 4 -> 0.03125)
clamp(L)     1.5 * sqrt(2) * cellSize(L) — exactly, on all eight levels:
             0.5303 1.0605 2.1211 4.2422 8.4844 16.9688 33.9375 67.8750
```

The gradient magnitude in the unsaturated band is **0.99957** (central differences
`d/dx=+0.0297`, `d/dy=−0.9935`, `d/dz=+0.1060`). One number settles the unit, the mapping
and the fact that it is a genuine distance field there, all at once. The clamp truncates
downward, so a clamped sample UNDERESTIMATES — the safe direction for a sphere-tracing
step. Toroidal wrap was confirmed at exactly 16 gu in y for level 0.

Level 0 is finest at 0.25 gu cells, level 7 coarsest at 32 gu. Each level wraps toroidally
over its own window, so a point outside that window aliases onto the wrong texels: take
the finest level whose window contains the point. That is what `finest_level()` does.

The sky volume is the same clipmap family — same `Frc` addressing, same 768-byte
`VoxelGlobalIlluminationConstantBuffer` — differing only in the numbers:

| | sky visibility | signed distance |
|---|---|---|
| resolution | 64 x 32 | 128 x 64 |
| z per level | 33 (32 + 1 border) | 130 (128 + 2 border) |
| levels | 8 | 8 |
| total depth | 264 | 1040 |
| z scale | 1/264 | 1/1040 |

**How the bytes are acquired.** The engine releases the volume on a compute list with
layout `6 -> 1` (`SHADER_RESOURCE -> GENERIC_READ`), access `80 -> 80000000`, sync
`80 -> 0`, subresources `(4294967295,0,0,0,0,0)`, bitfields in hex. The release ends
access with NO_ACCESS/NONE, so the copy path round-trips SRV -> COPY_SOURCE -> SRV BEFORE
forwarding the unmodified engine release, preserving its final layout and scope.
`GENERIC_READ` alone would have permitted copy-source, but the access half is what
decides. The route that does NOT work: `AdaptExposureCS`, the dispatch this probe hooks,
binds `g_skyVisibilityVoxelsTexturesLikeUav` and nothing else — confirmed by
disassembling pipeline state 21568, the shader the frame actually ran. The SDF is found
by descriptor shape through the barrier hook instead.

### Tools

| script | purpose |
|---|---|
| `Decode-SignedDistance.py` | sample the live R16 payload at a world position, or walk a profile |
| `Trace-SignedDistance.py` | sphere-trace camera to light; reports the smallest distance seen and where, so an endpoint self-hit at the fixture is distinguishable from a wall |
| `Decode-SpatialReadback.py` | the R8 sky-visibility decoder |
| `Decode-AmbientSH.py` | the 1024-byte ambient buffer |
| `Read-PixExportResource.py` | resource bytes out of a pixtool export |
| `Map-PixExportShaders.py` | pipeline state to shader name; `--extract` yields the container the frame ran |
| `Resolve-PixExportBindings.py` | the actual register-to-resource binding, time-accurate |
| `Find-PixExportDispatches.py` | candidates only — it produced a false positive once |

### Operating the probe, learned the hard way

- **A run can now be repeated without restarting the game.** Two separate things blocked
  it. First, `if(requested_) return false` in `SpatialReadback::Begin` refused every
  repeat; `Restartable()` replaced it and keeps what that rule actually protected — a
  series still owing transactions is never replaced mid-flight, and a destination the GPU
  or an open map may still touch is never handed to a new series. Second, and the larger
  one: the dispatch hook sets the probe's phase to `Pending` every observed frame, so it
  passed through `Idle` for only a fraction of a frame and a request was consumed and
  discarded almost every time. The request now tests `observing`.
- **Every request outcome is logged** — started, ignored because one is running, refused
  because the previous series is not settled. The silent refusal above cost a round of
  guessing precisely because it logged nothing.
- **The log is what survives, not the report.** The report is written only when the series
  ends or the plugin shuts down, and a game that exits without running
  `DLL_PROCESS_DETACH` — many do — loses it. That already cost one twenty-minute run.
  Anything worth having goes to `ch::Log` when it happens.
- **`SpatialReadbackCount` and `SpatialVisibilitySeconds` mean different things.** The
  first bounds RETAINED diagnostic snapshots, capped at 8 because each carries a whole
  volume; the second is how long the live value keeps refreshing. One key meaning both
  once turned a request for 900 into a run of 1. `SignedDistanceReadback` replaces the R8
  diagnostic with R16 copies, bounded by `SignedDistanceReadbackCount` and
  `SignedDistanceReadbackIntervalMs`.
- **Out-of-range configuration clamps to the maximum, never collapses to 1.** It used to,
  silently, which is how an over-large request produced the least data.
- **The measured transaction rate is 0.31–0.48/s against a configured 1 s**, because
  arming needs the pinned resource identity to recur and `Discover` only re-pins while the
  phase is Idle, which a running series never returns to. One 30 s stall was observed.
  A known reliability gap, deliberately not fixed.
- Test subjects at **Alfonso Estate, Duskwood** — a fixed teleport point, so they repeat:
  torch-carrying NPCs, and the Warspike Spearmaker workshop with interiors, roofed outdoor
  areas and individually fed lamps. The player home (Serkis Estate) is where Variant A ran.
- The glowing pillars at Alfonso never enter the light feed while the fire bowls beside
  them do. Emissive material, not renderer light sources. No occlusion work will surface
  them.

### Packages

`v2.0.1-restart.2` is the current build and includes Codex's barrier-tuple fix (`a87a2ab`)
and bounded R16 acquisition (`27c094d`) as well as the restart work. Earlier ones are
superseded. Private packages ship with the working INI baked in via `-IniOverrides`; only
a release gets the clean template.

### What is genuinely next

1. **CrimsonHue.** It consumes only `/v1/stream` today and neither the ambient estimate nor
   occlusion. That is where the perceptual curve and the occlusion gate belong. It was
   deliberately left untouched while this work ran, at the user's instruction, and it is
   the next thing to open.
2. **Widen Variant A only if a concrete case fails.** Thin geometry, doorways at grazing
   angles and moving occluders are untested, not known-bad.
3. **The sampling rate**, if the occlusion gate turns out to need data fresher than
   0.31–0.48/s.

## 5. Product decisions and next tests

The product target is deliberately smaller than reconstructing the renderer: local
environment ambient plus camera-to-light occlusion for fires and lamps.

**Local ambient working value.** The private diagnostic candidate is
`localEnvironmentAmbientEstimateWorking = globalSkyWorking * cameraSkyVisibility`.
That name is deliberate: it is an estimate for product use, not the exact renderer
term. Preserve and expose in the diagnostic evidence the raw global sky RGB, raw
`cameraSkyVisibility`, the derived RGB, source frame/timestamp/age and availability
or staleness for both inputs. Do not divide by the observed open-sky value near 0.53,
and do not copy the renderer's internal `0.03125` scale. No public API schema is
approved by this note.

**Occlusion variants, in test order.** Stop when the simplest robust answer works:

```
A   t233 pure distance/sphere-tracing from camera to light
B'  reconstructed engine structure: t233 stepping/coverage,
    plus t224.w gate and t224.xyz axis-distance data
C   length-weighted custom variant, only for a demonstrated A/B' weakness
D   t224-only traversal, low-priority control
E   t224 as final blocker decision: unsupported hypothesis, not engine structure
```

The current next action is the live acquisition gate in section 4b, followed by A
against labelled visible and occluded lamp segments. The GlobalId 749 paired
replay recipe above remains preserved, but is not required while the live route
can answer the bounded question. If A reliably disables lamps behind walls, stop; B' is then unnecessary.
Use B' only to address a measured failure of A. Do not infer a blocker from t224.w
alone.

The broader Ambient/SH archaeology is parked behind that product test. Remaining
questions include the full write history of ambient buffer resource 15739, whether
the six SH partials are cube faces or frames, an end-to-end SH projection replay,
slot 7, and the reason for the extra factor two in the Mie summary's `4*pi/256`.
For the latter, trace the six-way branch after direction generation or evaluate a
constant input before making any symmetry claim.

---

## 6. Traps that have cost time here

- **Nested escaping.** Writing Python through a Bash heredoc that itself writes Python
  string literals will corrupt `\x00`-style escapes into real control bytes and
  `\n` into newlines. Two test files were broken this way. Use the Write tool for
  anything with escapes, and never assume a `.replace()` in a throwaway script
  succeeded — assert it.
- **Silent no-op edits.** One regex fix appeared to apply and did not; the rebuild that
  followed was meaningless. A unit test caught it. Assert on every substitution.
- **Descriptor-table bases are not bindings.** See section 2.
- **Absence in the shader map is not absence in the capture.** 51 of 287 pipeline
  states are unnamed. Use a direct byte search for a negative.
- **An archive `.ll` is not what the frame ran.** Use `--extract` when that matters.
- Everything in `docs/TOOLING.md` under "Shell traps" still applies.
