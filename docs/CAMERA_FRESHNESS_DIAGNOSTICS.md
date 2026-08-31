# Camera freshness investigation

2026-08-30. A candidate fix passed offline verification; renewed game acceptance
and GPU/feature coverage remain open.

## Observations

The user saw an amber HUD arrow lag and briefly turn backwards. A controlled
camera-only right-turn recording of the installed preview.4 host reproduced
back-and-forth direction changes in the WebSocket data itself. The HUD renders
the received heading without smoothing.

- Recording: `artifacts/camera-jitter-right-retry-20260830.jsonl` (private, ignored).
- 45 seconds, 2702 samples. Movement appears in the final approximately 15 seconds;
  the initial stationary section must not be counted as a latency measurement.
- Largest wrapped step: about 177.16 degrees, followed by the opposite step.
- At large jumps the winning consensus had only 2-3 copies, with 16-18 distinct
  states still considered valid.
- Mean sample-to-receive age: about 0.58 ms, including an initial approximately
  120 ms old snapshot. Low delivery age does not prove current engine-frame data.
- The first two recordings did not overlap the user's rotation and therefore
  cannot establish whether the camera updates correctly during movement.

The previous selector chose the largest quantized state group, breaking ties by
distance to the player. It had no temporal freshness ranking. Historical renderer
copies can stay valid. This is a concrete weakness consistent with the trace;
individual-copy histories were captured before implementing a replacement.

## Individual-copy evidence and candidate fix

`artifacts/camera-copies-right-20260830.jsonl` contains 3600 samples, 60 seconds,
97 discovered addresses and 95 that yielded a valid sample. The independent scan
differs from the installed host's 60 cached addresses. Three individual copies
followed the approximately 15-second right turn without backwards steps, each with
291-292 changes. They update at staggered times, so following just one copy loses
updates. Many other copies retained earlier directions.

The production `RenderCameraTemporalSelector` now qualifies regularly updated
copies and follows their newest observed change. Nine targeted tests cover initial
consensus, stale majorities, rotating buffers, legitimate reversals/wrap/abrupt turns,
position/projection-only changes, stillness, lost sources, frozen sources and 1 Hz.
All 22 managed checks pass.

The production-code replay of that same trace passes with:

- Old majority-only selection: 5 direction changes, 2 backwards steps.
- Temporal selection: 803 changes, zero backwards steps, zero unavailable samples.
- Largest temporal step: 4.85 degrees, versus 8.47 degrees for the most frequently
  changing single copy. First motion is selected in the first observed motion sample.

```powershell
dotnet run --project tests/CrimsonDesertTelemetry.Tests -c Release -- --replay-camera-right artifacts/camera-copies-right-20260830.jsonl
```

This is a right-turn replay, not proof of in-game integration or other hardware.
The raw trace stays private/ignored. Synthetic regressions contain no game dumps
or fixed session addresses. The classifier is heuristic: it has no engine frame ID,
initially bootstraps from consensus, and cannot prove that a still-valid frozen
source is current if no alternative changing source was discovered.

## Controlled individual-copy recording

The working CLI includes an explicit, read-only diagnostic command:

```powershell
dotnet run --project src/CrimsonDesertTelemetry.Cli -c Release -- trace-camera-copies 60 60 artifacts/camera-copies-right.jsonl
```

It performs one independent structural discovery, then refreshes the discovered
addresses at the requested rate. Wait for `RECORDING` before turning. Keep the
character stationary; rotate the camera smoothly right, then stop. No game values,
installed files or active API settings are changed. Duration and rate are bounded;
existing output files are refused. Build and invalid-argument/overwrite protection
checks passed.

The JSONL contains one metadata record and per-sample candidates plus the current
consensus calculation. This is an internal diagnostic format, **not** the public
telemetry schema. Candidate addresses are private and session-specific. The new
discovery may differ from the installed host's cached set; record that distinction
when comparing results. Samples are sequential reads, not atomic engine frames.

Use the trace to compare changing/current and retained historical records, then
add offline regression tests before changing production selection. Do not hide
source errors with display interpolation or claim GPU-vendor coverage from this test.
