# Per-light source visibility

**Paused and excluded from the current stability release.** The user requested a
release of working features first. OFF does not include SDF acquisition/tracing
or expose SourceVisibility/HideOccluded/OcclusionToggleKey; older INIs cannot
activate them. The API retains the additive schema with unknown/disabled metadata.
The following contract describes the preserved experimental implementation, not
an offered release feature. Ambient acceptance is a separate goal.

The current `.3` camp test exposed false-clear lamp verdicts, intermittent shared
light-view dropouts and stopped SDF updates. Corrections are in progress; see the
[measured regressions](SOURCE_VISIBILITY_REGRESSION.md). F11 hiding follows the
geometry metadata and cannot correct an inaccurate clear verdict.

HUD and radar display visible/blocked/unknown metadata. `[LightOverlay] HideOccluded=1`
starts both light views with fresh known blocked sources hidden; `0` shows all.
`OcclusionToggleKey=122` toggles this display mode during play (default F11).
All four HUD shortcuts accept other decimal Windows virtual-key codes or `0` to
disable a shortcut: `[Overlay] ToggleKey` (119/F8), `DetailsKey` (120/F9),
`[LightOverlay] ToggleKey` (121/F10), and `OcclusionToggleKey` (122/F11).
Hiding has no effect on API/raw/smoothed data. Unknown/stale sources remain visible
and the legend still reports counts from all nearby captured records.

Each available `lights.rendered.sources` record can include `sourceVisibility`.
The existing raw HTTP/WebSocket feed carries it, and the smoothed feed preserves
the same object in each group's `contributions`. Source records, linear RGB,
luminance, grouping and RGB smoothing are unchanged. A blocked light remains in
both feeds. The consumer can multiply its contribution by `attenuationFactor`
only when the visibility result is usable. An unknown result has a null factor;
it never means that a light is off or blocked.

| Field | Meaning |
| --- | --- |
| `status` | `clear`, `blocked`, or `unknown`. |
| `attenuationFactor` | `1` for clear, `0` for blocked, `null` for unknown. |
| `reason` | Null for a measured verdict; an explicit reason for unknown. |
| `referencePosition` | Camera position paired with this rendered light capture; actual trace origin. |
| `lightCaptureSequence` | Equals the enclosing rendered capture sequence. |
| `volumeSequence` | Independent SDF volume capture sequence, or null if unavailable. |
| `contextFrame` | SDF volume's bracketed CPU context frame, or null. It need not equal the light frame. |
| `volumeAgeMillisecondsAtCapture` | SDF age at native publication of this light capture, or null. Frozen with the capture. |
| `closestApproach` | Smallest sampled signed distance in game units for a clear/blocked trace; otherwise null. |

The test reuses the calibrated [SDF Variant A](SDF_VARIANT_A.md): a camera-to-light
center segment, zero hit tolerance, 0.05 game-unit minimum step, 400-iteration
bound, 0.6 game-unit start offset and 1 game-unit fixture margin. It selects the
finest covering clipmap at each point. It does not measure a light's entire
illuminated region, account for photometric falloff, or infer surface brightness.
Thin walls, moving geometry and near-boundary verdict stability are limited by
the engine's copied SDF and the retained calibration evidence.

The producer computes metadata atomically for the same immutable rendered sample.
`sampleIndex` identifies a contribution only within that capture. No previous
source positions or renderer slots are retained as physical-light identities.
The SDF volume and light capture have independent cadences; their frames are
reported separately rather than presented as an engine-frame atomic pair.

Freshness is independent of raw source availability. Require an available raw
capture (at most 500 ms old), matching `lightCaptureSequence`, a known visibility
verdict, and:

```text
volumeAgeMillisecondsAtCapture + enclosing rendered ageMilliseconds <= 1500
```

That sum is conservative because the fixed volume age uses native publication
time, whereas rendered age starts at light capture time. If a client retains a
received snapshot, add elapsed time since reception too. Metadata stays fixed
when the API polls a repeated light capture, so polling does not re-smooth or
reclassify the same sample. Unknown/stale visibility must never discard healthy
raw or smoothed source data.

Unknown reasons include `waiting-for-volume`, `uncovered`, `too-short`,
`iteration-limit`, `invalid-sdf-sample`, `trace-budget-exceeded`,
`outside-trace-radius`, `stale-volume`, `invalid-context`, `disabled`, and
`invalid-metadata`. Production bounds are 256 traces per light capture and
100 game units from its camera. Sources beyond the trace budget/radius remain
present with unknown metadata. An invalid optional visibility block degrades
only that metadata, while malformed base light captures retain their existing
unavailable behavior. Legacy native v2 captures omit `sourceVisibility` entirely.

The geometric test uses positions and world geometry, with no projection or
screen-space visibility filter. It therefore accepts off-screen or behind-camera
targets that are in the current captured source list. However, ManyLights source
discovery remains view-filtered and is not a complete 360-degree registry. An
absent record does not prove occlusion. Controlled production validation must
include a real light that remains present when behind the camera; a different
source discovery or explicit target mechanism is required if it disappears.
