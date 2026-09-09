# GPU capture forensics — self-contained handover, 2026-09-09

Written so that someone with no memory of this session can continue. It covers the
toolchain built for reading a PIX capture offline, and the ambient-light
investigation that toolchain was built for. Read `docs/TOOLING.md` first for the
machine's paths; read this instead of the layered checkpoints in `docs/HANDOVER.md`,
which record how the conclusions were reached, in the order they were reached,
including the ones that were later withdrawn.

The one habit that matters: **every strong claim below was tightened or reversed at
least once by an outside review.** Distinguish established from hypothesis from
withdrawn before building on anything, and say which you are doing.

---

## 1. Orientation

The goal behind all of this is a lighting companion that dims ambient light under
structures and occludes fires and lamps. The engine's own answers are being read out
of the GPU rather than reinvented.

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
a frame. 117 unit tests cover them: `python -m unittest discover -s tests/scripts`.

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
`4*pi/256` is twice the hemisphere weight, consistent with doubling by symmetry.

**Two shaders integrate the sky differently.** Hammersley with total weight `4*pi`
against a projected grid with total weight `4`. Different domains, so NOT one
integration with an extra division — but the engine plainly applies canonical
spherical quadrature where it wants one.

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

## 5. Open questions, in the order worth attacking

**1. A full write history for resource 15739.** The extracted payload has slots 0..6
populated while their six summands read zero, which currently rests on "the producer
did not execute". That searched **compute dispatches only**. Copy destinations, CPU
uploads, graphics-stage UAV writes, clears, creation and aliasing were not searched. A
complete history would settle both the zeros and whether the payload precedes or
follows `csPrecomputeAmbient`'s store to slot 56.

**2. Where the replay payload sits in the frame.** `resources.bin` yields the
**serialised initial resource payload of a replay export**, not live buffer contents.
Its position relative to any dispatch is unestablished. **Until it is, do not use these
bytes for semantic validation.** Either recover the resource state after a dispatch, or
compare a computed store value numerically against the extracted floats.

**3. Cube faces or frames — needs a capture containing the producer.** Then, per
dispatch: resolve the root CBV holding `GlobalPushConstants` for `_renderFlags.x`, and
rows 30..33 of the bound `SceneConstantBuffer` to unproject. Six centre directions near
the axes are suggestive but not sufficient — **unproject the four NDC corners too**,
since a real `+X` face gives corner rays proportional to `(+1, ±1, ±1)`, with square
aspect and a 90 degree opening following from the same rays. Note the two need not be
exclusive: if `_renderFlags.x` cycles 0..5 across frames, the six could be spatially the
faces and temporally amortised, one refreshed per update.

**4. The end-to-end replay of the SH projection.** Extract `g_texSkyInscatter` from a
frame that ran the producer, reproduce the 4096-sample projection offline with the
basis table above and the same `1/6144`, and compare all 27 values. Expect close
numerical agreement, not bit equality — a parallel reduction sums in a different order.
This needs the texture footprint work item 5 describes.

**5. Texture extraction.** The reader handles buffers. Textures need footprint, row
pitch, subresource layout, format and possible tiling. Item 4 is blocked on this.

**6. Register-to-descriptor mapping is solved; resource-to-register is not.** The
inverse scan (which dispatch has a resource inside a range it could index) currently
samples the front of unbounded bindless ranges. Good enough for narrow UAV ranges,
loose for wide SRV spaces.

**7. Unresolved contents.** Slot 7 of the ambient buffer, read by every consumer in
the archive listings, holds `(16366681.0, 1115.56, 1673.45, 0.111556)` and is
unexplained. The six-way branch on `cb[2].x % 6` in `csPrecomputeAmbient`, immediately
after the direction is built, was not traced.

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
