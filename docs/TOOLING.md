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
2.7 GB of source in about a minute and answered in minutes what hours of shader
disassembly had only narrowed down.

`save-event-list` produces a bare API event list — Signal, Wait, Reset,
SetDescriptorHeaps — with **no shader names and no bindings**. Do not expect it to
answer provenance questions.

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

A worked example: to identify a 3D texture from its shader-side shape, grep
`CreateAndInitResources_*.cpp` for `TEXTURE3D` and match the dimensions, read the
`ApiObjectId` from the comment a few lines above, then grep the descriptor files
for `GetResource(<id>)` to obtain the actual SRV format, `MostDetailedMip` and
`MipLevels`. That sequence identified all three voxel volumes in one sitting.

Existing captures live in `artifacts/light-research/pix-captures/`; existing
exports in `artifacts/light-research/pix-provenance-*/`. Both stay out of Git.

Buffer contents (constant buffers, structured buffers) were exported through the
export button in PIX's own buffer view, not by script.

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

Script tests: `python -m unittest discover -s tests/scripts`.

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
| `Survey-SegmentOcclusion.py` | segment occlusion over every rendered light in every capture |
| `Analyze-SegmentOcclusion.py` | one capture, given viewpoints and a target |
| `Analyze-VolumeChurn.py` | how much of a volume changes between transactions |
| `Probe-DirectionalVolume.py` | samples a stored volume at offsets around the recorded camera |

All the Python tools read only preserved artifacts and never touch the game.

## Shell traps that have cost time here

- **Bash heredocs truncate** around a few kilobytes in this harness. Write longer
  scripts to a file with the Write tool and execute the file.
- **Backticks and double quotes inside `python -c "..."`** are interpreted by the
  shell. Long or quote-heavy Python belongs in a file.
- Editing docs by appending with a heredoc is reliable; editing them inline with
  `python -c` and embedded markdown is not.
- Git prints `LF will be replaced by CRLF` warnings constantly. They are noise from
  `core.autocrlf`, not a problem; the files here are pure LF.
