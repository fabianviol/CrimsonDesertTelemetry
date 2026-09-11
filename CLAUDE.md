# CrimsonDesertTelemetry — shared Codex / Claude rules

Documentation is for the assistants, not homework for the user.

1. Keep work scoped. Implement, test proportionately, save and stop. No unrelated cleanup.
2. Reuse existing findings. On takeover check Git status and `docs/HANDOVER.md`; follow its relevant research links before new experiments.
3. Preserve both assistants' work. Commit completed changes with clear ownership. Never overwrite a versioned release or delete old research/captures.
3a. Preserve every game executable you work against: run `scripts/Backup-GameExecutable.ps1` after any install or update, before Steam replaces the file. Relocating a hardcoded address after an update can be impossible without the PREVIOUS binary to read context from, and an overwritten executable cannot be recovered.
4. Verify the actual result. Separate measured facts, hypotheses and invalid tests. A failed reader proves no absence; game tests need a progressing control.
5. Maintain one concise current checkpoint in `docs/HANDOVER.md`: result, remaining work, relevant evidence and one next step. Raw captures stay out of Git.
6. Scoped reversible console/graphics instrumentation is authorized for lighting research. Never call an instrumented run an untouched baseline. Require game shutdown for ASI replacement.
7. Use PowerShell 7 where suitable. Keep `AGENTS.md` and `CLAUDE.md` byte-identical.

## Locations

- Product / single ASI source: this repository, `native/CrimsonDesertTelemetry.Asi`.
- Research: `research/`, a preserved independent Git repository (commit its changes separately).
- Research entry points: `research/light-source-tests/CODEX_HANDOVER_FIRE.md` and `research/console-enabler/HANDOVER.md`; historical checkpoints remain, latest overrides stale claims.
- Captures: `artifacts/`; third-party checkouts: `external/`; obsolete copies: `archive/crimsonhue-workspace-20260906/`. Never develop the archived standalone copy.
- Future Philips Hue consumer: `C:\DEV\CrimsonHue`; it consumes the telemetry API and contains no telemetry implementation.
- Tooling: read [docs/TOOLING.md](docs/TOOLING.md) before reaching for a GPU capture, a native build or a repository script. pixtool export-to-cpp answers resource identity and provenance far faster than shader disassembly, and cmake/ctest are not on PATH.
- GPU capture forensics: [docs/GPU_CAPTURE_FORENSICS.md](docs/GPU_CAPTURE_FORENSICS.md) is self-contained. Read it before any claim about what a frame did. It carries the four-script chain from a PIX export to a proven shader-register-to-resource binding, the identities found so far, and a ledger separating established findings from hypotheses and from ones already withdrawn.
- Old research paths under `C:\DEV\CrimsonHue` map to this repository after the 2026-09-06 migration.
- Antivirus detections: [docs/ANTIVIRUS_FINDINGS.md](docs/ANTIVIRUS_FINDINGS.md) records what was MEASURED. Microsoft's Wacatac verdict is toolchain drift, proven by rebuilding the 2.0.0 source today, and no feature removal fixes it. Bitdefender's Barys signature is pinned to commit 6937fa9, the bounded repeated readback series. Do not cut functionality on a hunch; both detections were misattributed at first glance.
- Build 25246367: the light path is verified live; the ambient hook RVAs in `ambient_probe.h` are hardcoded, moved +0x21C0 on this update and are now relocated but UNTESTED live. Follow the top of docs/HANDOVER.md and check anchors with `scripts/Verify-AmbientAnchors.py` after any game update. Variant A/SDF remain parked. Keep t224, engine-raymarch reconstruction, DXR, new PIX work and CrimsonHue closed.
