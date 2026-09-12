# Provenance

This file records public material consulted during research. A listed source does not imply that its code was copied.

| Source | What was consulted | Use here | Code copied |
|---|---|---|---|
| [blizz3010/CrimsonDesertCoop](https://github.com/blizz3010/CrimsonDesertCoop) | Public notes and historical WorldSystem/player-position/FOV signatures | Historical research leads; all current-build matches and runtime behavior were independently derived and validated | No |
| [andreikapica-dot/CD_Companion](https://github.com/andreikapica-dot/CD_Companion) | Public WebSocket architecture and camera-heading research | Compared its public current and historical heading signatures with the supported executable; both were absent | No |
| `Orcax-1399/CrimsonDesert-player-status-modifier` | Public player-pointer and position-hook documentation | Terminology and possible investigation routes | No |
| `Baal-TehDriverman/CrimsonDesertMods` | Public pattern-scanning and hook-coordination documentation | Compatibility considerations | No |
| CDTT and UltimateCameraMod public releases | User-facing behavior and packaged data | Capability comparison only | No |
| [Developer Debug Console Enabler](https://www.nexusmods.com/crimsondesert/mods/803) by YinjiDawn/YinjiD | Published description plus offline inspection of command/RTTI strings in the user-supplied v0.1.0 proxy DLL | Historical research lead for the game's MSVC RTTI and dormant console commands; telemetry addresses and layouts were independently derived and validated | No |
| [hzeemr/crimsonforge](https://github.com/hzeemr/crimsonforge/tree/09f821638da4e1f04b26be43af408b4d59337ad3) | Archive readers, asset/prefab research and the shader-disassembly approach; exact research revision `09f821638da4e1f04b26be43af408b4d59337ad3` | Separate local research dependency: recovery wrappers import its PAMT/PAZ, crypto, decompression and prefab helpers; its entry-name hash helper was used for shader lookup | No upstream source pasted into the product or bundled in the release |

The structural camera scanner, camera validation rules, historical redundant-copy
consensus algorithm, current-build player-position signature, telemetry schema,
CLI implementation and authored-light record/pointer-walk derivation are original
work based on memory observation and executable disassembly. Current rendered-light
capture was additionally derived and verified through scoped native renderer hooks,
GPU readback and shader inspection. Those instrumented runs are not untouched
baselines. No community light-mod code supplies the runtime light reader.

The ambient sampler and per-source geometry test were derived from this project's
preserved shader, buffer-layout and controlled game observations. See
[ambient derivation](AMBIENT_DECODE.md), [SDF calibration](SDF_VARIANT_A.md) and
[source visibility](SOURCE_VISIBILITY.md). Production reuses those findings with
bounded acquisition of current data; it does not require research capture runs,
binding histories or binary dumps. The new SDF acquisition adds three necessary
barrier/lifecycle callbacks and reuses the existing exposure context and queue
submission observer. Research source and historical evidence remain preserved.

Current production (`CDT_RESEARCH=OFF`) cannot activate the Research, Console,
Explorer or legacy `OcclusionTest` entry points through INI settings. The
experimental paths and their own INI template remain available in
`CDT_RESEARCH=ON`. This boundary corrects configurations that could replace the
product streams or expose inactive options; it is not an antivirus mitigation.
See [INI validation](INI_VALIDATION.md). Historical packages and their recorded
behavior are unchanged by this development correction.

Validation status as of 2026-09-12: camera-local ambient visibility passed a
controlled open/enclosed/open route in the OFF v2.1.9 package on build 25246367.
The production per-light implementation has synthetic coverage; its controlled
live visible/blocked/visible and behind-camera acceptance remains pending. Those
are separate results, and the preserved SDF research does not establish release
readiness for the new production implementation. Neither telemetry feed drives
physical lamps.

CrimsonForge is used as a tool/dependency in a separate checkout, not as pasted
runtime source. The original wrappers in `tools/update-recovery/` were preserved
from this project's independent research repository at `af5485b`; they require
that external checkout to run. The runtime host and ASI do not import CrimsonForge;
the release does not ship it, its Python dependencies, the game executable,
extracted shaders or archive assets.
See [update recovery](UPDATE_RECOVERY.md) for reproducible lookup steps.

No source was copied from a repository without a verified compatible license. Any future reuse must identify the exact source revision, license, affected files, and required notices in this document and in the reused source files.

## Bundled native dependencies

The native ASI builds against the following upstream libraries. Their source archives
are pinned by version and SHA-256 in `native/CrimsonDesertTelemetry.Asi/CMakeLists.txt`.

| Dependency | Version | License | Use |
|---|---|---|---|
| Dear ImGui | 1.91.9b | MIT | HUD layout and D3D12 rendering |
| MinHook | 1.3.4 | BSD 2-Clause | Graphics-presentation hooks and guarded native instrumentation |
| JSON for Modern C++ | 3.12.0 | MIT | HUD WebSocket payload parsing |

The package includes `THIRD-PARTY-NOTICES.txt` with upstream notices, including
Dear ImGui's embedded stb components and ProggyClean font. The project's MIT
license does not replace these dependency licenses. No game assets are bundled.
