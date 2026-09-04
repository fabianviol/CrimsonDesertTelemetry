# Provenance

This file records public material consulted during research. A listed source does not imply that its code was copied.

| Source | What was consulted | Use here | Code copied |
|---|---|---|---|
| [blizz3010/CrimsonDesertCoop](https://github.com/blizz3010/CrimsonDesertCoop) | Public notes and historical WorldSystem/player-position/FOV signatures | Historical research leads; all current-build matches and runtime behavior were independently derived and validated | No |
| [andreikapica-dot/CD_Companion](https://github.com/andreikapica-dot/CD_Companion) | Public WebSocket architecture and camera-heading research | Compared its public current and historical heading signatures with the supported executable; both were absent | No |
| `Orcax-1399/CrimsonDesert-player-status-modifier` | Public player-pointer and position-hook documentation | Terminology and possible investigation routes | No |
| `Baal-TehDriverman/CrimsonDesertMods` | Public pattern-scanning and hook-coordination documentation | Compatibility considerations | No |
| CDTT and UltimateCameraMod public releases | User-facing behavior and packaged data | Capability comparison only | No |
| [Developer Debug Console Enabler](https://www.nexusmods.com/crimsondesert/mods/803) by YinjiDawn/YinjiD | Published description plus offline inspection of command/RTTI strings in the user-supplied v0.1.0 proxy DLL | Research lead for the game's MSVC RTTI and dormant console commands; all telemetry addresses and layouts were derived independently from the game executable and validated read-only | No |

The structural camera scanner, camera validation rules, redundant-copy consensus algorithm, current-build player-position signature, telemetry schema, CLI implementation, and engine-light record/pointer-walk derivation in this repository are original work based on read-only observation and disassembly of the supported game executable. No community light-mod code or assets are used by the light reader.

No source was copied from a repository without a verified compatible license. Any future reuse must identify the exact source revision, license, affected files, and required notices in this document and in the reused source files.

## Bundled HUD dependencies

The HUD builds against the following upstream libraries. Their source archives
are pinned by version and SHA-256 in `native/CrimsonDesertTelemetry.Asi/CMakeLists.txt`.

| Dependency | Version | License | Use |
|---|---|---|---|
| Dear ImGui | 1.91.9b | MIT | HUD layout and D3D12 rendering |
| MinHook | 1.3.4 | BSD 2-Clause | Opt-in graphics-presentation hooks |
| JSON for Modern C++ | 3.12.0 | MIT | HUD WebSocket payload parsing |

The package includes `THIRD-PARTY-NOTICES.txt` with upstream notices, including
Dear ImGui's embedded stb components and ProggyClean font. The project's MIT
license does not replace these dependency licenses. No game assets are bundled.
