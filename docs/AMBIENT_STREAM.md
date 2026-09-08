# Global sky stream — private development preview

This feed complements local lights; it is **global upper-hemisphere sky**, not
the light actually reaching the player inside a room. Doorway measurements showed
no large interior suppression in this source. Do not use it as a roof detector.
Not part of public 2.0.0. First combined package: `2.0.1-sky.1`.

- HTTP: `GET http://127.0.0.1:27311/v1/ambient`
- WebSocket: `ws://127.0.0.1:27311/v1/ambient/stream`
- JSON Schema: `GET http://127.0.0.1:27311/v1/ambient/schema`

Both transports carry the same separate `schemaVersion: "1.0"` envelope.
Existing raw snapshots, smoothing, HUD and schema1.4 remain unchanged. Streams
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

[Research]
AmbientProbe=0
```

All except the last diagnostic override are supplied by the new package.
**Remove an old AmbientProbe=1 override:** that private file-capture mode remains
exclusive and pauses normal light/sky publication. No console/explorer switch,
Nsight or PIX is required. Ambient currently depends on enabled native ManyLights
instrumentation. Ambient=0 leaves local lights unchanged; disabled UI is fine.

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

Available: `captureSequence`, `frameNumber`, `capturedAt` UTC, `ageMilliseconds`,
and `sky`. Unavailable: nonempty `reason`; sample fields/sky are null.
Typical reasons: bridge-missing, bridge-waiting, bridge-changing, bridge-invalid,
bridge-stale, source-stale, unsupported-build, native-fault,
legacy-plugin-conflict, capture-disabled-or-stopped, or host game-state reason.

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
color/intensity and local GI/occlusion are not separated by this API. The cached
exposure diagnostic has unknown GPU age and is deliberately not exported here.

For CrimsonHue: use as a separately weighted global background signal, with an
explicit policy for indoor use and output mapping. Do not silently assume the
sky vanishes under roofs or sum unlike light/exposure domains as physical units.

## Safety and evidence

Exact EXE build/hash, native instruction/context checks, source layout guards,
same-device resource checks, actual submitting queue and GPU completion fence,
paired validated scene frame, distinct mapping magic/version/PID/start identity,
seqlock snapshots and freshness checks. Only native pathA has a public decoder;
pathB remains research-only. Failed sky preflight does not disable local lights.
A shared runtime copy/device fault stops both captures; stale data is rejected.

Shader-only updates are not proven safe by EXE hashing. Six inspected current
csPrecomputeAmbient variants share identical bodies; no bound PSO hash is checked
at runtime. Source-only tests and private prior captures are not a game test of
this new combined mode. See [measured derivation](AMBIENT_DECODE.md).

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
