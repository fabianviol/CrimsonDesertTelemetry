# Tooling — what exists on this machine and how to invoke it

For both assistants. Written because several of these cost real time to rediscover.
Paths are machine-specific; verify before relying on a version number.

## PIX and pixtool — GPU capture analysis

Installed at `C:\Program Files\Microsoft PIX\2603.25`. The GUI is `WinPix.exe`;
there is no separate exporter binary in that folder besides `pixtool.exe`.

**`pixtool` is the programmatic route and it is the reason to reach for PIX at
all.** Two invocations are proven here:

```powershell
& 'C:\Program Files\Microsoft PIX\2603.25\pixtool.exe' `
    open-capture '<path>.wpix' `
    save-event-list '<out>.csv'

& 'C:\Program Files\Microsoft PIX\2603.25\pixtool.exe' `
    open-capture '<path>.wpix' `
    export-to-cpp '<out-directory>'
```

`export-to-cpp` is by far the more valuable of the two. It reconstructs the whole
capture as compilable C++: every resource creation with its full
`D3D12_RESOURCE_DESC`, every descriptor and view creation, the command lists, the
PSOs and the raytracing structures. On the 2.7 GB lantern capture it produced
roughly 2.7 GB of binary resource data plus generated source in about a minute and answered in minutes what hours of shader
disassembly had only narrowed down.

`save-event-list` produces a bare API event list — Signal, Wait, Reset,
SetDescriptorHeaps — with **no shader names and no bindings**. Do not expect it to
answer provenance questions.

`pixtool --help save-resource` supports event selection by GlobalId, but its
documented resource selector is RTV/depth only. It does not expose a selector for
the arbitrary 3D SRV/UAV pair needed here. The exact t233/t224 event-state route is
therefore an instrumented export replay: copy resources 191 and 211 immediately
after GlobalId 749 in `PopulateCommandList_21556_1_2()`, then map after the queue
fence. The barrier/layout recipe and evidence limits are in
`docs/GPU_CAPTURE_FORENSICS.md`.

**How to use an export-to-cpp tree.** The files are grouped by purpose and are
grep-friendly:

```
CreateAndInitResources_*.cpp   every resource, with dimensions and format,
                               each preceded by  // ApiObjectId = N
Descriptors_*.cpp              CreateShaderResourceView / UAV / Sampler,
ModifyDescriptors_*.cpp        linking GetResource(N) to a heap slot
CommandLists_*.cpp             the recorded command stream
CreatePSOs.cpp                 pipeline states
AccelStructureRecreation_*.cpp raytracing acceleration structures
CapturedAssets.h               large embedded data
```

**`docs/GPU_CAPTURE_FORENSICS.md` is the self-contained guide to this chain**, with
worked invocations, the identities found so far, and what each step does and does not
prove. **The chain from an export to a named binding is complete**, and each link has a
script: resource contents (`Read-PixExportResource.py`), pipeline state to shader name
and container (`Map-PixExportShaders.py`), candidate dispatches
(`Find-PixExportDispatches.py`), and the actual register-to-resource binding
(`Resolve-PixExportBindings.py`). Use the last one before claiming a shader touches a
resource: a descriptor table's base viewing a resource does NOT mean the shader's
registers land on it, and that mistake has already produced one false positive here.

Both descriptor scanners must accept digits in generated view-helper suffixes.
`CreateShaderResourceView_Tex3D` and `CreateUnorderedAccessView_Tex3D` were silently
missed by the former `[A-Za-z_]*` pattern; `[A-Za-z0-9_]*` plus SRV/UAV regression
tests is the fixed form.

A worked example: to identify a 3D texture from its shader-side shape, grep
`CreateAndInitResources_*.cpp` for `TEXTURE3D` and match the dimensions, read the
`ApiObjectId` from the comment a few lines above, then grep the descriptor files
for `GetResource(<id>)` to obtain the actual SRV format, `MostDetailedMip` and
`MipLevels`. That sequence identified all three voxel volumes in one sitting.

Existing captures live in `artifacts/light-research/pix-captures/`; existing
exports in `artifacts/light-research/pix-provenance-*/`. Both stay out of Git.

**Buffer and texture contents can be read from an export WITHOUT building it.**
`resources.bin` is a bare concatenation of XPRESS-compressed blocks with no index,
consumed in program order by `ResourceReader::Read(buffer, compressedSize)`, so a
block's file offset is the sum of every compressed size read before it.
`scripts/Read-PixExportResource.py` reconstructs that order from the generated
source and decompresses one block through `Cabinet.dll`:

```powershell
$export = 'artifacts/light-research/pix-provenance-20260909/cpp'
python scripts/Read-PixExportResource.py $export --list-cbv 1024
python scripts/Read-PixExportResource.py $export --cbv 15728 589824 --out out.bin
python scripts/Read-PixExportResource.py $export --resource 15739 --out out.bin
```

`--list-cbv SIZE` finds every constant buffer view of exactly that size and resolves
each to a resource; a CBV names the HEAP for placed resources, so the resource is
whichever one covers that heap offset. Note that this searches DESCRIPTOR CREATIONS
only -- D3D12 also binds constant buffers as root CBVs by address, with no descriptor
to find, so a listing is not an inventory of every buffer of that size.

The walk is self-checking: a wrong order makes XPRESS fail rather than return
plausible bytes.

**Scope.** This solves access to the compressed payload, which is what buffers need.
It is NOT a general texture decoder: a texture also needs its footprint, row pitch,
subresource layout, format and any tiled or swizzled arrangement before the bytes
mean anything. Buffer contents can also be exported by hand through the export button
in PIX's own buffer view.

## Shader extraction and lookup — existing Codex tools

**There are now two routes to a shader.** This section covers the archive route. The
other is `Map-PixExportShaders.py --extract` on a PIX export, which yields the DXBC a
captured frame actually ran and so is not subject to the variant caveat below; feed it
to the same `dxc -dumpbin` call.

These `.padxil/.dxbc/.ll` families came from the game's **on-disk shader-cache
archives**, NOT PIX or process memory. Both examples below came from
`C:\Steam\steamapps\common\Crimson Desert\0017\1.paz`. An archive variant
is not proof of which shader was actually bound in a captured frame.

### Shader name to local file — no large IR search needed

`Inspect-ArchiveShader.py` already saves exact entry names in `<stem>.json`
under `defined_functions`. From the product repository, this builds a lookup
from existing metadata only (tested against both requested examples):

```powershell
$shaderMap = foreach ($metaFile in Get-ChildItem artifacts/light-research -Filter '*.json' -File) {
    $listing = [IO.Path]::ChangeExtension($metaFile.FullName, '.ll')
    if (-not (Test-Path -LiteralPath $listing)) { continue }
    $meta = Get-Content -LiteralPath $metaFile.FullName -Raw | ConvertFrom-Json
    foreach ($entryName in $meta.defined_functions) {
        [pscustomobject]@{ Shader=$entryName; Listing=$listing; Metadata=$metaFile.FullName }
    }
}
$shaderMap | Where-Object Shader -eq 'RaymarchLocalLightsCS' | Format-List
# Optional inventory; choose a NEW filename:
# $shaderMap | Export-Csv artifacts/light-research/shader-name-map-NEW.csv -NoTypeInformation
```

| Entry | Listing under artifacts/light-research |
|---|---|
| RaymarchLocalLightsCS | filtered-count-1322f152-20260906-2111-52c33a4a.ll |
| RaymarchDiffuseHitDistanceCS | gi-entry-864eec9d.ll |

Multiple results can be different variants. Ad-hoc listings without inspector
metadata will not appear in this map: 16 of the 79 listings, all
`ambient-check-*-sky*.ll`. For those the entry name is still recoverable from the
listing itself, which closes the gap to all 79:

```bash
grep -oh '^define [^@]*@[A-Za-z0-9_]*' artifacts/light-research/*.ll | sed 's/.*@//'
```

Verified against all 16. The metadata route stays preferable because it also
carries the archive variant, which the listing does not. `<stem>.padxil.json` separately preserves
the exact archive variant in `entry.path`, PAZ filename/offset and content hashes.

### Extraction order and exact calls

Reuse `artifacts/light-research/crimsonforge-shader-index-20260905.json`:
92,788 entries under `candidates`, group0017, created with the existing
`research/light-source-tests/Find-ArchiveLightAssets.py`. Its PAZ offsets belong
to that archive version, not an arbitrary game update. No default rescan needed.

```powershell
$shaderPython = 'C:\Users\fabia\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$shaderDxc = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe'
$shaderOut = 'artifacts/light-research/raymarch-local-NEW' # fresh output stem
& $shaderPython research/light-source-tests/Read-ArchiveLightAsset.py `
    --forge external/crimsonforge --deps external/archive-python-deps `
    --index artifacts/light-research/crimsonforge-shader-index-20260905.json `
    --group 0017 `
    --path 'shadercache__/c8755039_1322f152_5_52c33a4a_3_3c400631_d81d9086.padxil' `
    --out "$shaderOut.padxil"
if ($LASTEXITCODE -ne 0) { throw 'Archive extraction failed' }
& $shaderPython research/light-source-tests/Inspect-ArchiveShader.py `
    --input "$shaderOut.padxil" --out $shaderOut --dxc $shaderDxc
```

1. `Read-ArchiveLightAsset.py` reads one indexed PAZ entry with the existing
   CrimsonForge readers, decrypts/decompresses as indicated and checks length.
   Saves unchanged decoded `.padxil`, extraction metadata `.padxil.json` and log.
2. `Inspect-ArchiveShader.py` validates PASC's declared DXBC offset (or accepts
   direct DXBC), container/chunk bounds and presence of DXIL. Saves inner `.dxbc`.
3. It runs **`dxc.exe -dumpbin <stem>.dxbc -Fc <stem>.ll`**. The `.ll` is DXC's
   output, not a custom IR rewrite/decompiler stage. `<stem>.json` records
   `defined_functions`, chunk metadata, SHA256s, DXC path and exit code.

Both scripts refuse existing outputs; inspector outputs must be within product
`artifacts/light-research`. Some assets contain RTS0 root signatures only, no DXIL:
their rejection is expected, not a broken tool. Preserved metadata may still name
`C:\DEV\CrimsonHue\artifacts\...`; after migration use that stem under Telemetry.

### What the hashes mean

For `c8755039_1322f152_5_52c33a4a_3_3c400631_d81d9086.padxil`, the second
underscore field is the observed HLSL-source family key, the fourth the entry key.
The first is a pipeline/root-signature family, NOT HLSL-source identity. Other
fields distinguish variants; their complete semantics are not established here.
Existing `Find-ArchiveShaderSource.py --key-field 1` samples source families when
needed; the earlier first-field source-identity control failed.

Entry key = CrimsonForge `core.crypto_engine.hashlittle(name.encode('utf-8'),
0xC5EDE)`, formatted as eight lowercase hex digits; the NAME is case-sensitive.
Verified: RaymarchLocalLightsCS ->52c33a4a; RaymarchDiffuseHitDistanceCS ->864eec9d;
independently re-checked here on two shaders not used to derive it,
PropagateSignedDistanceCS ->71fe2870 and GenerateHiZLevel0FromSDF_CS ->ddce827c,
both matching the suffix of their local listing.
Use this to select archive candidates; confirm the decoded entry afterward.

Local prefixes and dates are human research labels, not engine IDs. In
`filtered-count-1322f152-20260906-2111-52c33a4a`, 1322f152 is the source key,
52c33a4a the entry key; `gi-entry-864eec9d` kept only the entry key. Recover the
FULL variant from `.padxil.json`, the actual entry name from `.json`. Neither
filename key is the DXBC SHA256 or the DXBC container's shader HASH chunk.

## Native build and test — not on PATH

`cmake` and `ctest` are NOT on PATH. Use the Visual Studio copies:

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'

& $cmake -S native/CrimsonDesertTelemetry.Asi -B build/native-package -A x64
& $cmake --build build/native-package --config Release
& $ctest --test-dir build/native-package -C Release --output-on-failure
```

`Build-ModManagerPackage.ps1` finds cmake itself through `vswhere` and does not
need these. Warnings are errors in the native build (`/W4 /WX`), so a shadowed
variable name fails the build.

## Python

The real interpreter is

```
C:\Users\fabia\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe
```

Script tests: `python -m unittest discover -s tests/scripts` (123 tests as of the
2026-09-09 `_Tex3D` parser fix).

## PowerShell

`pwsh` 7.6.5 is present alongside Windows PowerShell 5.1. `Start-SpatialProbe.ps1`
requires 7.4 or newer.

## Repository scripts

| script | purpose |
|---|---|
| `Build-ModManagerPackage.ps1` | builds a package; `-IniOverrides @{...}` bakes research switches into private builds, refused for versions without a prerelease suffix |
| `Start-SpatialProbe.ps1` | signals the plugin's capture event; ONE transaction series per process, including failures |
| `Decode-SpatialReadback.py` | decodes a spatial capture; also hosts the shared `sample_world`, `select_clipmap` and `march_segment` helpers |
| `Verify-NativeSampler.py` | compares the native sampler against the decoder on preserved captures |
| `Verify-AmbientAnchors.py` | checks the hardcoded ambient hook RVAs in `ambient_probe.h` against a game executable, and derives the shift when an update moves them |
| `Get-VirusTotalVerdict.py` | looks a package binary up on VirusTotal by hash, or uploads it with `--submit`; key from `.env`. See [ANTIVIRUS_FINDINGS.md](ANTIVIRUS_FINDINGS.md) |
| `Backup-GameExecutable.ps1` | preserves the installed game executable under `artifacts/recovery/`, idempotent by content. Run it after every install or update -- the previous binary is what makes the next relocation possible |
| `Compare-PeSurface.py` | diffs two PE files' sections, entropy and imports, flagging the APIs generic AV engines weight |
| `Survey-SegmentOcclusion.py` | segment occlusion over every rendered light in every capture |
| `Analyze-SegmentOcclusion.py` | one capture, given viewpoints and a target |
| `Analyze-VolumeChurn.py` | how much of a volume changes between transactions |
| `Probe-DirectionalVolume.py` | samples a stored volume at offsets around the recorded camera |
| `Read-PixExportResource.py` | reads a resource's captured bytes out of a pixtool export without building it |
| `Map-PixExportShaders.py` | names the pipeline states in a pixtool export, and `--extract` writes one's DXBC out for DXC |
| `Find-PixExportDispatches.py` | names the dispatches in an export that CAN REACH a resource -- a candidate filter, not a binding |
| `Resolve-PixExportBindings.py` | resolves a shader register to the resource actually bound at a dispatch, time-accurately |
| `Decode-AmbientSH.py` | decodes the 1024-byte PrecomputedAmbientConstantBuffer into three channels of nine |
| `Decode-SignedDistance.py` | samples the live R16 signed-distance payload at a world position, or walks a profile; holds the calibrated addressing, cell sizes and clamps |
| `Trace-SignedDistance.py` | sphere-traces camera to light through that payload and reports the smallest distance it saw and where, so an endpoint self-hit at the fixture is distinguishable from a wall |

All the Python tools read only preserved artifacts and never touch the game.

## Other existing workflows

- DMM: `C:\Modding\CrimsonDesert\DMM\DMM.exe`. The user installs the WHOLE ZIP
  from `scripts/Build-ModManagerPackage.ps1 -Version <new-version>` through DMM;
  outputs are in `artifacts/mod-manager/`. Close the game before ASI replacement.
  Keep versioned ZIPs immutable; never add the archived second console ASI.
- `scripts/Backup-UpdateEvidence.ps1` preserves existing update/shader anchors;
  inspect parameters before use, do not regenerate the research instead.
- Binary comparisons: `Get-FileHash -Algorithm SHA256`; source comparisons:
  `git diff` / `git diff --check`. No custom comparison service is needed.
- GitHub/Nexus publishing previously used web interfaces; no new publishing
  automation is claimed here. Building a diagnostic ZIP does not publish it.

## Shell traps that have cost time here

- **Bash heredocs truncate** around a few kilobytes in this harness. Write longer
  scripts to a file with the Write tool and execute the file.
- **Backticks and double quotes inside `python -c "..."`** are interpreted by the
  shell. Long or quote-heavy Python belongs in a file.
- Editing docs by appending with a heredoc is reliable; editing them inline with
  `python -c` and embedded markdown is not.
- Git prints `LF will be replaced by CRLF` warnings constantly. They are noise from
  `core.autocrlf`, not a problem; the files here are pure LF.
