# Ambient decoding checkpoint — 2026-09-07, Codex/Astra

Private derivation, not a public ambient contract. The 2026-09-08 diagnostic
extension below changes the private capture file only; no HTTP/WS, HUD or
CrimsonHue consumer change. Existing raw captures stay intact.

## Evidence and exact assumed profile

`artifacts/light-research/ambient-check-20260907-sky-6acf206f.ll`,
entry `csPrecomputeAmbient`, shader hash8501bc6e8367adacee2edc17d1defccc.
LL SHA2565D2E9CFDD7E748735EDF5FB593F037AEDDD0B8132890A39D16A883352A6E0936.
DXBC SHA256E17BB8A551F09B368697178798237D1F406508FDA70F3C4CA60F98A40327C9B1.
Archive path/decoded PASC hash and native routing are in
`research/light-source-tests/GPU_LIGHT_LAYOUTS_25116796.md`.

The live instrument captures A after native dispatch RVA3849BB7. It does not
capture the PSO shader hash. Consequently `Decode-AmbientProbe.ps1` requires
`-AssumeCsPrecomputeAmbientLayout`, rejects B, and explicitly exports
`runtimeShaderIdentityVerified=false`. Matching output and native provenance
support this profile but do not silently upgrade it to confirmed live identity.

### Follow-up: named native pass and all six archived variants

Native A selects the pass by name, not just a similarly named buffer:
at143848AB6 it references145BDB340 (`PrecomputeAmbient`), then at143848AE6
calls143830170 on sky+9D48. That lookup returns the selected pass payload;
143848AF0 loads its first pointer, supplied to command virtual+200 at143848B04.
The same function dispatches at143849BB1 immediately before the existing probe.
The existing dump `rawpages/ambient-native-routing-pid30016-b` preserves A;
new `rawpages/ambient-profile-route-pid34736` preserves the name/lookup code.

Bounded read-only PID34736 check: sky3D6E1DC8000 → technique3D66B8B54C0;
name key at146F64B0C=970E. Lookup key(0,0,970E) resolves node3D66E7BFC20,
payload3D66FB2BA90, program3D72AFB4800. Evidence under
`artifacts/light-research/rawpages/ambient-profile-{technique,pass-key,selected-pass,selected-objects,shader-record,variant-nodes}-pid34736`.
Addresses are session evidence, NOT future discovery anchors. Adjacent shader-name
strings support the family association but are not proof of bound bytecode identity.

Existing `crimsonforge-shader-index-20260905.json` contains exactly six paths
`shadercache__/c6f4169c_0b1104a5_5_6acf206f_3_<A>_<B>.padxil`:
A={0e46d723,deba1dcd}, B={56d1e36e,7bc8d106,c8810edf}.
Reused Read-ArchiveLightAsset.py/Inspect-ArchiveShader.py and SDK dxc to extract
all six successfully. Artifacts beside the reference export:
`ambient-profile-deba-56d1e36e.*` and
`ambient-profile-{0e46d723,deba1dcd}-{7bc8d106,c8810edf}.*`.

**All six have identical executable csPrecomputeAmbient function text.**
Reproduce with Python text-mode LF normalization and regex
`(?ms)^define void @csPrecomputeAmbient\(\) \{\n.*?^\}` (exactly one match/file),
then SHA256 of the UTF-8 match:
`15C3C3D59DD02CE753048C3EF843A7E7B7E3AE390F9AED5AAB50A0C41BE5F941`.
The four 56d1e36e/c8810edf DXBC files match reference E17BB8A5…;
the two 7bc8d106 files have DXBC SHA256
`8A85EFA5F50F925703825A984109B52FC1552F0EF8441DF22C1941589FAC998E`.
Their differences are metadata/type descriptions, not the function calculation.

This removes variant selection within the indexed current-build family as a
normalization blocker. It does NOT measure a bound PSO hash or cover future
shader updates; keep existing reports/explicit assumption unchanged. A separate
PSO hook is not needed merely to choose between these six identical calculations.

## Packing and normalization, from producer instructions

256 threads (LL19); direction at LL399..439 is
`(x,y,z)=(cos(phi)*sqrt(1-y*y), 1-i/256, sin(phi)*sqrt(1-y*y))`.
Phi uses the bit-reversed thread index. **Upper hemisphere only**, y>0.
Nonnegative working RGB L is projected (LL18127 onward); the full 256-thread
sum is reduced with offsets1,2,4,8,16,32,64,128, then multiplied by1/128
(LL20043 onward). No cosine weighting at this projection stage.

| Working channel | Coefficients0..3 | Coefficients4..7 | Coefficient8 |
| --- | --- | --- | --- |
| R | row0.xyzw | row1.xyzw | row6.x |
| G | row2.xyzw | row3.xyzw | row6.y |
| B | row4.xyzw | row5.xyzw | row6.z |

Basis in WORLD (x,y,z), with game Y vertical:
`[a, -b*y, b*z, -b*x, c*x*y, -c*y*z, d*z*z-e, -c*x*z, f*(x*x-y*y)]`.
Constants are float32 a=.282095, b=.488603, c=1.092548,
d=.9461759328842163, e=.31539198756217957, f=.546274.
Rows0..6 are signed coefficients, not seven independent color samples.
Row6.w is a separate atmosphere calculation (sun/moon selection and cloud
sampling), not intensity/alpha or coefficient9. Row7 contains other scalars;
row56 is separately reduced Mie scattering, not added to SH color by this decoder.

For each channel, stored C0=(sum(L*a))/128=2*a*mean(L), and
C1=(sum(-L*b*y))/128=-2*b*mean(L*y). Hence:

- Upper-sky sample mean: `C0/(2*a)`.
- Upward cosine-weighted sky, irradiance/pi quadrature: `-C1/b`.

These two quantities can be recovered from the actual sampled sums without
inventing a general-direction SH evaluation or consumer convolution factor.
A constant hemisphere gives mean=L; the discrete upward quadrature gives
L*1.00390625 because the shader samples y=1..1/256. This small quadrature bias
is preserved, not silently normalized away. Not lux, nits, display brightness
or total scene illumination. Sun/moon disks are not separately identified here.

## Color handling

Before projection both incoming branches apply the already known matrix
(LL9280..9298 and18075..18093), then a scalar and a nonnegative clamp:
`M=[[.61312,.33951,.04737],[.07020,.91636,.01345],[.02062,.10958,.86980]]`.
It is the same matrix documented for ManyLights in the older handover; no
local-light schema or values were changed during this investigation.

M resembles the linear sRGB-to-AP1 matrix in
[Unity's primary ACES shader source](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.core/ShaderLibrary/ACES.hlsl),
but that is supporting color-math evidence, NOT proof of Unity/Unreal usage or
the game's exact primaries/white point. Use the actual game float32 constants.
The decoder preserves working RGB, and separately solves M*x=RGB. It neither
clips the inverse result nor applies gamma/tone mapping. The shader clamp means
matrix reversal is not necessarily lossless recovery of the pre-clamp source.
`rec709MeanLuminanceEstimate` uses .2126/.7152/.0722 ONLY as an explicitly named
input-primaries estimate. No calibrated color-space or photometric-unit claim.

No ExposureConstantBuffer is bound in this inspected producer variant. That
does not establish the scaling of upstream LUTs or comparability with the
exposure-scaled local-light feed. Do not sum these feeds as equal-unit lighting.

## Reproduce and evidence

PS7 from the product root:

```powershell
./tests/Test-AmbientSh.ps1
./scripts/Decode-AmbientProbe.ps1 -Path <original.bin> -AssumeCsPrecomputeAmbientLayout -OutFile <new.json>
```

178 synthetic/offline checks: independently projected black/uniform/directional/
HDR hemispheres versus direct RGB/cosine sums, all27 coefficient lanes, matrix
inversion, ignored scalar rows, malformed/nonfinite/signed-profile controls,
binary provenance, required assumption, producer rejection and no-overwrite.
Decoder uses `Read-AmbientProbe.ps1 -PassThru`; default reader output unchanged.

All360 gameplay samples from PID34736 decode under the explicit profile without
failures. `ambient-derived-candidate.json` accompanies each of the three raw
capture directories, retaining raw-source SHA256. Initial/final inverse-matrix
means, with corresponding estimated Rec.709 luminance in relative units:

| Run | First RGB | Last RGB | Estimated luminance first → last |
| --- | --- | --- | --- |
| Outdoor1 | .024366 / .015171 / .005582 | .007492 / .004744 / .001968 | .016434 → .005127 |
| Home2 | .001522 / .002785 / .003567 | .001503 / .002723 / .003555 | .002573 → .002524 |
| Outside3, user reports moonlight | .001652 / .001892 / .001785 | .001385 / .001470 / .001254 | .001834 → .001437 |

These color differences are not an indoor-occlusion result: the uncaptured
home→outside gap is244.860s and the moon direction changes23.1885deg. Different
time/weather and no measured threshold crossing remain confounds.

## Remaining work / shortest next step

The existing consumer exports actually read row7 and/or56, not SH rows0..6.
Do not claim a renderer SH evaluation was verified from declarations alone.
Keep the new derived quantities as **upper-sky candidate diagnostics** until
color/exposure semantics are defined. The family audit above resolves the
current variant-calculation ambiguity without claiming bound-PSO identity.
Next reuse the documented exposure route for paired ambient/exposure diagnostics;
the CPU four-float4 candidate still needs upload/timing provenance, and GPU CBV
state/suboffset must not be guessed. A short
within-run doorway out/in/out control can then address locality; absence of a
roof response would mean an additional local-visibility source is needed, not
that the sky data is useless. No new broad GI branch or public schema yet.

## 2026-09-08: engine exposure-cache provenance and private probe v2

Bounded live PID34736 pointer check reused sky+10 → Renderer+690 → ExposureOwner.
Renderer3D64CAA7000, owner3D72B96D900, outer3D6E1DD9700, inner3D6F10563C0,
native resource12C8A5A70; inner+158 cached Map pointer remainsNULL.
Evidence: rawpages/ambient-exposure-{owner,chain}-pid34736.*.

The older CPU +D8 candidate is a **destination of an engine GPU readback**, not
an upload source. In live native1435429F0, after its dispatch/flush sequence:

- 1435450CC loads owner+C0; inner+AE must equal2, +AF bit3 must be clear.
- 1435450E9 loads owner+D0 readback helper, passed with the C0 source to1437DD810.
- 14354511E calls143031500 on that helper, then tests the returned pointer.
- 14354512C and143545139 copy two32-byte vectors into owner+D8 and+F8.
- 14354514B calls143031850 to finish the readback access.

Raw evidence: exposure-readback-provenance-pid34736.*, exposure-transfer-code-pid34736.*;
the supporting update/caller dump is exposure-update-code-pid34736.*.
Disassemble from known instruction boundaries (e.g.1435450CC), not the dump's
arbitrary starting address143545090. Helper143031500 selects ready entries;
we have NOT measured their producing frame. Native A also attempts to bind the
owner+C0 wrapper at143848EFF..143848FB3, but the inspected optimized ambient
shader does not consume ExposureConstantBuffer. Do not invent a data dependency.

`exposure_probe.h` follows this existing chain with sky/owner backreferences,
validates the 4-byte element view (20..16384 elements) and inner mode2, and copies
exactly64 bytes. It rechecks pointer identities; the ambient capture takes two
such samples around command recording. It performs no COM Map/resource-state
change or global graphics hook. Stable bytes are a CPU observation, not an
engine lock or evidence of GPU-frame equivalence. Missing data remains visible.

Private binary v2: original64-byte header (version2/recordBytes4128), unchanged
2816-byte CPU scene +1024-byte fenced ambient, then224-byte CTEX appendix:
magic/version/bytes/flags (16), begin/end tick (16), six pointers (48), before64,
after64, stride/count/innerMode/reserved (16). Flags1/2=available before/after,
4=same identity,8=same bytes,16=finite positive exposure0.x in stable bytes.
Other lanes may contain packed NaN patterns (also seen in the preserved PIX
ExposureConstantBuffer export); preserve bytes/uint32, don't force float JSON.
Only a scalar with flag16 is surfaced as `exposure0x`; raw bytes always remain.
`gpuFramePaired=false`, `sourceFrameAgeKnown=false` are explicit.

Read-AmbientProbe supports v1/v2 with homogeneous bounded records and appendix
validation. Decode-AmbientProbe carries v2 context through, with NO exposure
normalization or ambient arithmetic change. Old v1 output fields stay unchanged.
Tests:17/17 native, including55 synthetic cache checks and updated actual WARP
copy/fence smoke (worker cannot resample a changed cache after recording;
missing exposure still saves ambient). 35 additional parser/decoder controls;
178 existing independent SH tests; original malformed-file controls pass.
These are synthetic/offline results, not live probe-v2 acceptance.

Private package `artifacts/mod-manager/CrimsonDesertTelemetry-v2.0.1-ambient-probe.3-ModManagers.zip`.
ZIP SHA256664E9913123DE5EC929C6D1BCA2DA52FA22974CB9C029F161DE3B335306EDC93;
ASI SHA256710C06B8040C62039A5D3055A163C506A14BBBEDEE206D64042BC4220ABC0B4A.
Package validation passes. User stopped for bed before deployment; shutdown
not confirmed. No installation, INI change or new live capture performed.
Not published; keep older immutable packages. Deploy only after game shutdown,
retain Research/AmbientProbe=1 for diagnosis, verify v2 IDLE then use the existing
Start-AmbientProbe command for one loaded-scene run. No automatic recording and
no need to redo the three prior static scene captures. Inspect availability and
cache/sky relationship before deciding whether exact GPU exposure is necessary.
