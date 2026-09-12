# Ambient stream — global sky and camera-local visibility

This independent feed complements local lights. Its `sky` object is **global
upper-hemisphere sky** and remains unoccluded. Separate `visibility` metadata
samples sky openness at the camera; `localEnvironmentAmbientEstimateWorking`
multiplies the raw sky mean by that raw visibility. This is a working ambient
estimate, not measured room brightness, physical illumination or a player-root
measurement. Neither signal answers whether an individual fire is behind a wall;
that is the separate [per-light source-visibility contract](SOURCE_VISIBILITY.md).

Camera-local Ambient Occlusion passed controlled open/enclosed/open acceptance
in the OFF v2.1.9 package on build 25246367 on 2026-09-12. Per-light visibility
failed live acceptance and is excluded from the current stability release; its
implementation/evidence are preserved for later work. See [release validation](STABLE_RELEASE_VALIDATION.md).
The camera-sky HUD wording/orientation complaint remains pending; exposure is a
local volume sample, not the fraction of sky pixels on screen. Older packages
must not be assumed to include current behavior.

- HTTP: `GET http://127.0.0.1:27311/v1/ambient`
- WebSocket: `ws://127.0.0.1:27311/v1/ambient/stream`
- JSON Schema: `GET http://127.0.0.1:27311/v1/ambient/schema`

Both transports carry the same separate `schemaVersion: "1.0"` envelope.
Original light records, RGB and smoothing remain unchanged. Streams
use the existing loopback-only origin policy and bounded latest-only queues.
HTTP200 is not proof of available data: inspect `status` and freshness.

## Enable and test

Close the game, install the complete private ZIP through DMM, and use:

```ini
[Lights]
Enabled=1
ManyLights=1
ManyLightsSampleRateHz=20

[Ambient]
Enabled=1
```

The current production profile supplies the enabled ambient path.
`CDT_RESEARCH=OFF` neither offers nor needs Research, Console, Explorer or legacy
`OcclusionTest` settings and ignores them in older INIs. `CDT_RESEARCH=ON` retains
the exclusive `AmbientProbe` experiment, which can pause normal light/sky
publication. See [INI configuration and validation](INI_VALIDATION.md).
No console/explorer switch, Nsight or PIX is required. Ambient depends on enabled
native ManyLights instrumentation. Ambient=0 leaves local lights unchanged;
disabled UI is fine. `[Overlay] ShowAmbient=1` shows the separate sky, visibility
and estimate ages on the diagnostics page (default F9; see [HUD configuration](../README.md#controls-and-configuration)).

The automatic sky capture is nominally2Hz, sharing one in-flight GPU copy/fence
transaction with local lights. Actual cadence can be lower under contention.
HTTP/WS may repeat a capture at host rate; `captureSequence` identifies new data.
Reject data older than1500ms. Missing, changed, malformed or stale mappings and
loading/stopped/unsupported game state produce unavailable data with no sky.
Consumers must also expire their last sample when the transport goes silent.

## Data and limitations

Every envelope states `source: "precompute-ambient-sky"`,
`scope: "global-upper-hemisphere-sky"`, `units: "relative-shader-units"`, and
`localOcclusionIncluded`, `exposureNormalized`, `directSunMoonSeparated`: false.
These legacy scope flags describe the global `sky` source. They do not say that
the separate camera `visibility` or local product estimate is absent.

Available: `captureSequence`, `frameNumber`, `capturedAt` UTC, `ageMilliseconds`,
and `sky`. Unavailable: nonempty `reason`; sample fields/sky are null.
Typical reasons: bridge-missing, bridge-waiting, bridge-changing, bridge-invalid,
bridge-stale, source-stale, unsupported-build, native-fault,
legacy-plugin-conflict, capture-disabled-or-stopped, or host game-state reason.

Camera visibility has its own availability, frame and age. An available global
sky sample does not establish usable local visibility:

| Additional field | Meaning |
| --- | --- |
| `visibility.valueWorking` | Raw camera-local voxel visibility in 0..1. No normalization to an assumed open-sky maximum. |
| `visibility.frameNumber` | Frame paired with the local visibility sample, independent of the sky frame. |
| `visibility.ageMilliseconds` | Age of that visibility capture. The object can remain present while stale; clients must check it. |
| `localEnvironmentAmbientEstimateWorking.rgbWorking` | Raw `sky.upperHemisphereMeanWorking * visibility.valueWorking`; null when either usable input is missing. |
| `localEnvironmentAmbientEstimateWorking.available`, `.stale` | Usable product flag and whether the camera visibility has exceeded 1500 ms. |
| `localEnvironmentAmbientEstimateWorking.basis` | Explicit formula/interpretation; not an exact renderer lighting term. |

`visibility` is null on missing/invalid context or the shader fallback branch.
The fallback value of one is deliberately not presented as measured open sky.
Require fresh sky and visibility independently, and expire retained data when
the transport stops progressing. Capture age does not measure the age of every
engine voxel: the underlying field can refresh spatial regions at different times.

| `sky` field | Meaning |
| --- | --- |
| `coefficientsWorking` | 3 arrays (R/G/B), 9 signed SH coefficients each, copied from measured GPU data |
| `upperHemisphereMeanWorking` | Mean sky radiance in the shader's working RGB; C0/(2×float32(.282095)) |
| `upwardIrradianceOverPiWorking` | Upward-facing cosine-weighted quadrature; -C1/float32(.488603) |
| `inverseMatrixMean` | Algebraic reversal of the measured color matrix, RGB order |
| `inverseMatrixUpwardIrradianceOverPi` | Same matrix reversal for the upward quadrature |
| `rec709MeanLuminanceEstimate` | .2126R+.7152G+.0722B applied to inverseMatrixMean; explicitly an input-primaries estimate |

Working RGB is AP1-like, **not proven display RGB**. The inverse follows a shader
clamp and may be lossy; signed values are preserved. No gamma, automatic exposure
division, temporal smoothing or 0..1 clamp is applied. Not lux, nits, lumens,
final pixels or a calibrated quantity directly additive to local light RGB.
The sky calculation incorporates its engine inputs; independent sun/moon disk
color/intensity and local GI are not separated by the global `sky` object. The cached
exposure diagnostic has unknown GPU age and is deliberately not exported here.

Consumers can use the separately reported visibility to control an ambient model.
Do not assume global sky itself vanishes under roofs, or sum unlike light/exposure
domains as physical units. The product estimate preserves both raw inputs and is
not a calibration of actual light reaching a lamp or the player.

## Safety and evidence

Exact EXE build/hash, native instruction/context checks, source layout guards,
same-device resource checks, actual submitting queue and GPU completion fence,
paired validated scene frame, distinct mapping magic/version/PID/start identity,
seqlock snapshots and freshness checks. Only native pathA has a public decoder;
pathB remains research-only. Failed sky preflight does not disable local lights.
A shared runtime copy/device fault stops both captures; stale data is rejected.

Shader-only updates are not proven safe by EXE hashing. Six inspected current
csPrecomputeAmbient variants share identical bodies; no bound PSO hash is checked
at runtime. Source-only tests and private prior captures do not establish new live
coverage. The bounded current-build acceptance below is separate. See
[measured derivation](AMBIENT_DECODE.md).

### Camera-local ambient acceptance, 2026-09-12

The user installed OFF v2.1.9 through DMM on build 25246367. A controlled route
gave mean visibility 0.33466 outside, 0.00669 under a covered doorway, 0.00006973
inside, then 0.19968 outside again. All 64 samples were fresh with progressing
frames and age at most 47 ms. The return camera was about 1.25 game units from
the first position; sunrise changed sky luminance during the route. This proves
a reversible geometric ambient response in the tested scene, not an exact
same-pose brightness comparison or exhaustive cave/material coverage.
See [the checkpoint and preserved evidence](HANDOVER.md).

### First combined-mode live check, 2026-09-08

Private sky.1 installed on PID22128, exact supported build25116796. One10s
concurrent capture:601 sky envelopes,20 distinct sky samples (about2Hz), all
available, maxage532ms. Local lights progressed through158 matching raw/smoothed
captures (about16Hz with20Hz configured), maxage94ms. One local bridge-changing
message was rejected and recovered on the next message; this resets derived
tracks under existing fail-closed behavior. Not a zero-dropout claim.

All20 unique sky messages satisfy the schema. Local raw/group/EMA/repeated-capture
invariants pass; no native capture fault. Player stayed stationary. Evidence in
`artifacts/light-research/sky-stream-live-20260908-pid22128/`, including original
three streams, HTTP snapshots, native log/INI and validation.json. Global-sky
scope and color/exposure/occlusion limitations above remain unchanged. No GPU-vendor
matrix, long-session endurance or actual Hue-output test is implied.
