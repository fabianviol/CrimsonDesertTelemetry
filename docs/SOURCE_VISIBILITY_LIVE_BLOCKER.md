# Live blocker — SDF volume never reaches the bridge (2026-09-13, Claude)

First live run of `2.1.11-source-visibility.3`. The schema fix works; the
per-light test cannot start because no SDF volume is ever produced.

## Measured

Deployment is `.3` and correct: ASI, Core.dll, host DLL, runtimeconfig and INI
all hash-match the package. Overlay log shows the `.2` run failing with
"Telemetry data is incompatible" and this run reporting "Telemetry is ready",
so the schema 1.5 HUD fix is confirmed live.

Source visibility produces no verdict at all. 12 samples over ~800 snapshot
sequences, player stationary in the known room:

    status = unknown (29/29 every sample)
    reason = waiting-for-volume
    volumeSequence = null, closestApproach = null, contextFrame = null

HUD agrees: `0 visible / 0 blocked / 15 unknown`.

**Control, same probe, same moment:** ambient is fully alive —
`status=available, frameNumber=23281, captureSequence=782, age=542ms`,
progressing. So `spatial::Start` succeeded and the probe runs. The sky readback
flows; the distance volume does not.

Evidence: `artifacts/source-visibility-live-20260913-0025/capture.json`.

## Localisation

`SaveDistanceTransaction` (spatial_probe.cpp:672) returns early unless
`readback->Completed()` is non-zero. In the `!persistDistanceEvidence` branch —
our configuration, since `SignedDistanceReadback=0` — it logs
"Signed distance HUD snapshot %u: cached, ..." on *every* completed transaction.

`CrimsonDesertTelemetry.native.log` is **0 bytes**. No such line exists, and no
"Spatial binding probe refused initialization" either. Therefore
`readback->Completed() == 0`: not one distance transaction ever completed, so
`sdf::Publish` (spatial_probe.cpp:691/722) was never called, so
`sdf::CurrentStatus().sequence` stays 0 and `source_visibility::Poll`
(source_visibility_bridge.cpp:157) returns before publishing. Every source
therefore stays `State::Waiting` = "waiting-for-volume". The bridge and
classifier are not at fault; they are starved.

## Excluded

- **Not the INI.** `distanceReadback = persistDistanceReadback || hudOcclusion ||
  sourceVisibility` (instruments.cpp:225), so `SourceVisibility.Enabled=1` arms
  distance mode regardless of `SignedDistanceReadback=0`, with a 120-transaction
  budget. Shipped INI is correct; do not change it.
- **Not antivirus.** The `.3` ASI still scores 11/71 with seven Barys-family
  hits, and Barys is pinned to the readback series. Its presence proves the
  readback is compiled in and live. Nothing was cut for scanners.
- **Not enhanced barriers.** `ArmDistanceRelease` is reachable only from the
  enhanced-barrier `BarrierHook`, which made this a candidate — but historic
  captures on this machine record `"enhancedBarriers": true` (29/29 in
  spatial-binding-29248). The capability is present.

## Next

Arming requires, at `ArmDistanceRelease` (spatial_probe.cpp:300): a prior
`NoteDistanceVolume` sighting of the resource, `MatchesRelease`, and
`distanceLatest` being gi-stable, error-free, enhanced and younger than 750 ms,
with `publishedTransactions == readback->Completed()`. One of these gates is not
being met. The next run needs a log line on the *refusal* side of that
condition; today a silent non-arm is indistinguishable from a probe that never
saw the resource.

Note the acceptance gap this exposes: the 17 preserved controls were **offline
replay of retained volumes**, which never exercised live volume acquisition.
That path is unproven and is what is failing.

## Side finding

DMM 2.8.1 did **not** fix the stale-label bug. Deployed
`crimson-desert-telemetry.deps.cfg` still reads `2.1.10`, dated 21:16:35;
deployment is 5/6 exact. Cosmetic metadata, no runtime effect on this test.
