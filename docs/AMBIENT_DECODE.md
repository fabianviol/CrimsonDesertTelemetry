# Ambient decoding checkpoint — 2026-09-07, Codex/Astra

Private offline derivation, not a public ambient contract. No plugin, HTTP/WS,
HUD, package or CrimsonHue consumer change. Existing raw captures stay intact.

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
