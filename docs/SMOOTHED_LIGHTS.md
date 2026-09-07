# Smoothed local-light stream (development)

An additional consumer signal for CrimsonHue; **raw `/v1/snapshot` and
`/v1/stream` payloads and their schema versions remain unchanged.** Not ambient.
Available in private `2.0.1-local-lights.1`; not in the published 2.0.0 or
ambient-probe.2 ZIP. Install the local-lights preview alone through DMM for the
normal ManyLights/Hue test. Do not simultaneously activate the ambient package.

- HTTP: `GET http://127.0.0.1:27311/v1/lights/smoothed`
- WebSocket: `ws://127.0.0.1:27311/v1/lights/smoothed/stream`

Both return the same separate envelope, `schemaVersion: "1.0"`. HTTP returns
200 even when unavailable; inspect `status`, never infer availability from HTTP.
WebSocket shares the existing loopback-only browser-origin policy and bounded,
latest-only queue. Enabling `[Lights] Enabled=1` is required for its source.

## Contract for CrimsonHue

Envelope fields: `schemaVersion`, `status` (`available`/`unavailable`), `source`
(`spatially-grouped-filtered-manylights`), `coverage`
(`view-filtered-not-complete-360`), `publishedAt`, `settings`.
Available data also includes `sourceCaptureSequence`, `sourceFrameNumber`,
`capturedAt`, `ageMilliseconds`, `sources`. Unavailable data omits those fields
and includes `unavailableReason`. An **available empty sources array is valid**.

Each source group contains:

| Field | Meaning |
| --- | --- |
| `trackingId` | Approximate spatial track, unique within this host session; NOT engine/object identity. |
| `position` | Current unweighted centroid in existing world/game-unit axes. No temporal position lag. |
| `colorLinear` | Smoothed sum of contributing linear HDR RGB values (`x/y/z` = R/G/B). No clamping to 0..1. |
| `luminanceLinear` | Weighted luminance of that smoothed RGB, same weights as raw light decoder; NOT lumens/nits. |
| `rawSumColorLinear` | Sum before temporal filtering, useful for diagnostics. |
| `contributions` | Current raw rendered records including sample index, position, RGB, kind, direction and cone. These are NOT additional lights to add again. |

For a simple Hue signal, consume each group's **`colorLinear` once** at its
position. Do not add its `contributions` again, or also add the raw light stream.
Hue gamut mapping, exposure/brightness scaling, lamp geometry and output-rate
limits belong to CrimsonHue. No physical lamps are driven by telemetry.

Do not average conflicting spot directions into a fabricated cone. Contributions
retain their own directions; the aggregate has no single `kind`/cone/direction.
The RGB sum is a useful signal, **not predicted on-screen color or illumination
at a receiver**. Distance, attenuation, occlusion, material, exposure and
tonemapping still matter. A scalar luminance-weighted color average would lose
energy; these HDR contributions are summed in linear space instead.

## Grouping, tracking, smoothing

Default grouping extent is 0.15 game units, reusing the HUD's proximity scale.
Groups conservatively fit inside a box whose diagonal is at most that extent.
No transitive chains: A near B and B near C does not imply one large group.
This may split some close contributions; two separate lamps closer than the
threshold may also merge. **Spatial grouping is not proven physical identity.**
Only the current filtered ManyLights array is used, not the separate authored
list (which could double count). Off-screen/culled contributions remain absent.

Tracks are one-to-one nearest spatial matches to the previous capture, within
max(0.5, 2 × GroupRadius) game units. Sample indices are never identities.
Nearby moving/crossing lights and merges/splits may change or exchange tracks;
IDs do not survive culling, faults, loading or host restarts. The ID should not
be used as a saved physical lamp mapping.

Sum first, then smooth RGB with `alpha = 1 - exp(-dt / timeConstant)` using
elapsed source capture time. **Only a new GPU capture advances the filter**;
60 Hz host publication/extra HTTP requests do not repeatedly smooth 20 Hz data.
New tracks start at their observed value. Single lights use the same filter.
Default time constant 200 ms reaches about 63% of a step after 200 ms; it is not
a fixed 200 ms delay. Real fire pulses are attenuated, not claimed to be noise.

Missing groups disappear immediately; there is no invented fade-out/afterglow.
Unavailable, invalid, stale (>500 ms) or discontinuous source data resets state.
Health-only errors/loading/stop also invalidate and notify derived subscribers.
This does not prove an absent light was switched off: it may have been culled.

**Consumer watchdog:** stop using values when `status != available`, clear absent
groups on each fresh array, and expire all output after at most 500 ms without
fresh **`capturedAt`/`sourceCaptureSequence`** progress. `publishedAt` can advance
while the source sample is repeated. Network silence/host failure cannot deliver
an explicit error message, so a local freshness watchdog remains mandatory.

## Configuration

In future packages (restart after edits):

```ini
[LightSmoothing]
TimeConstantMilliseconds=200
GroupRadius=0.15
```

Time constant: 0..2000 ms; 0 disables temporal filtering but retains grouping.
Group radius: 0.01..1 game units, decimal point. Configuration is global per
host and returned in `settings`; raw streams/HUD remain untouched.
Standalone development host:

```powershell
dotnet run --project C:\DEV\CrimsonDesertTelemetry\src\CrimsonDesertTelemetry.Cli -- serve 27311 60 --lights --light-smoothing-ms 200 --light-group-radius 0.15
```

Do not run a second host on an occupied port. The ambient diagnostic deliberately
pauses ManyLights, so this stream is unavailable during that experiment. A live
game/Hue acceptance test must use restored normal ManyLights capture.
