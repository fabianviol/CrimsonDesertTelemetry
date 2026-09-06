# CrimsonDesertTelemetry — shared Codex / Claude rules

Documentation is for the assistants, not homework for the user.

1. Keep work scoped. Implement, test proportionately, save and stop. No unrelated cleanup.
2. Reuse existing findings. On takeover check Git status and `docs/HANDOVER.md`; follow its relevant research links before new experiments.
3. Preserve both assistants' work. Commit completed changes with clear ownership. Never overwrite a versioned release or delete old research/captures.
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
- Old research paths under `C:\DEV\CrimsonHue` map to this repository after the 2026-09-06 migration.
