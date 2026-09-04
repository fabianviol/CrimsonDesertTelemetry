# API reference

Public contract for Crimson Desert Telemetry, HTTP API **v1**, snapshot schemas
**1.1** and **1.2**. These are separate version numbers. This document describes the
shipped implementation, not planned features or historical memory-scanning tools.

## Connect

The ASI package starts a separate local telemetry host. Your mod or application
connects to that host; it does not need to load a .NET assembly into the game,
install the HUD, or implement a memory reader. The supported integration boundary
is HTTP/WebSocket/JSON Lines, not a native C ABI or an in-process callback API.

Default addresses:

- HTTP: `http://127.0.0.1:27311`
- WebSocket: `ws://127.0.0.1:27311/v1/stream`

The server listens on IPv4 loopback only. There is no API key, authentication,
TLS, game-control endpoint or runtime-configuration endpoint. Do not expose it to
the network through a proxy. CORS/origin checks are not authentication of local apps.

Configure the ASI before starting the game:

```ini
[Server]
Enabled=1
Port=27311
SampleRateHz=60

[Lights]
Enabled=0
NearbyRadius=100

[Overlay]
Enabled=0
```

`Port` supports 1024–65535; `SampleRateHz` supports 1–240. `NearbyRadius` supports
1–100000 game units. The ASI clamps values
outside those ranges. Changes require a game restart. HUD enablement is independent
of telemetry. Keep the `.cfg` runtime metadata unchanged; it is not user configuration.

Alternatively, from a source checkout with the .NET 8 SDK:

```powershell
dotnet run --project .\src\CrimsonDesertTelemetry.Cli -- serve 27311 60 --lights --light-radius 100
```

The CLI rejects out-of-range arguments instead of clamping them. Do not start a
second host on a port already used by the ASI. A manually started `serve` process
can wait for the game and survive game restarts. An ASI-owned host normally exits
with the game: clients must also handle a disconnected socket, not only a `stopped`
message. A final state message is not guaranteed before process termination.

## HTTP endpoints

GET responses are JSON (`application/json`), except the schema's
`application/schema+json` content type and the plain-text WebSocket error below.
Responses have camelCase keys. There are no mandatory custom request headers.

| Method and path | Response |
|---|---|
| `GET /` | 200: object containing `name`, `schemaVersion` and an `endpoints` array. |
| `GET /v1/health` | 200: health object, even when the game is absent or unsupported. |
| `GET /v1/snapshot` | 200: latest published snapshot, including a `loading` or `stopped` snapshot. 503: health object if **no snapshot has been published yet**. |
| `GET /v1/schema` | 200: embedded [JSON Schema](../schema/telemetry-v1.schema.json), draft 2020-12, for snapshot payloads. It does not describe health responses. |
| `GET /v1/stream` with WebSocket upgrade | 101: WebSocket connection. 400 without an upgrade (`A WebSocket connection is required.`). 403 for a supplied, disallowed `Origin`. |

There is no pagination, history query, replay cursor, per-client rate parameter or
delta format. Repeated snapshot GETs can return the same `sequence`.

**HTTP 200 is not proof of live gameplay data.** Check the payload's state and
timestamp. The snapshot endpoint retains the last published object; a health-only
change (for example `error`) does not clear it. Use health and a freshness timeout
as well. A missing host results in a connection failure, not an HTTP 503.

### Health response

```json
{
  "schemaVersion": "1.1",
  "status": "playing",
  "gameRunning": true,
  "supportedBuild": true,
  "gameBuild": "24994088",
  "sampleRateHz": 60,
  "lastSequence": 42,
  "lastCapture": "2026-08-31T18:00:00Z",
  "connectedClients": 1,
  "discoveredCopies": 1,
  "discoveryMilliseconds": 1.64,
  "error": null,
  "compatibility": {
    "mode": "tested",
    "executableSha256": "B596A498701DFCDC49C486D890C42755DABC8C174314C7F26F7329394452446D",
    "executableVersion": "1.0.0.2658",
    "referenceBuild": "24994088"
  }
}
```

| Field | Type | Meaning |
|---|---|---|
| `schemaVersion` | string | `1.1` with lights disabled; additive `1.2` when the light module is requested. |
| `status` | string | Sampler status; see below. |
| `gameRunning` | boolean | Sampler's current process-presence observation, not a guarantee of playable data. |
| `supportedBuild` | boolean or null | `null` before support is known; `false` when compatibility checks rejected the executable; `true` after a runtime was opened through either compatibility mode. On `error`, do not interpret it as successful live-data validation. |
| `gameBuild` | string or null | Installed Steam build ID when available, not the telemetry product version. `"unknown"` can appear in snapshots if an automatically recognized installation has no readable manifest. Null means no runtime is currently identified. |
| `sampleRateHz` | integer | Configured target rate, not a measured delivery rate. |
| `lastSequence` | integer or null | Last published snapshot sequence, including unavailable-state snapshots; null before the first publication. |
| `lastCapture` | date-time string or null | Timestamp of that publication. Retained across health-only updates. |
| `connectedClients` | integer | Active WebSocket subscriptions, including the HUD when enabled. HTTP polling is not counted. |
| `discoveredCopies` | integer | Current native reader reports one source when resolved; zero before resolution/reset. Historical name, not a current heap-scan count. |
| `discoveryMilliseconds` | number or null | Reference-resolution time in milliseconds; not total game startup, executable hashing time or per-sample latency. |
| `error` | string or null | Human-readable diagnostic; wording is not a stable machine-readable error code. |
| `compatibility` | object or null | Executable identity and selection result for the current runtime; null before selection or after rejection/exit. See below. |

`compatibility.mode` is `"tested"` only for an exact, locally validated executable
SHA-256. `"automatic"` means a different executable was accepted by unique code,
data-section, player-type and multi-slot object-table guards using `referenceBuild`
as its layout. It does **not** mean that executable received a manual in-game test.
`executableSha256` always describes the installed EXE; `executableVersion` can be
null. This health-only object is additive metadata and is not part of snapshot schema 1.1.

Possible `status` values:

| Status | Interpretation |
|---|---|
| `waiting-for-game` | No usable runtime is open; retrying. Initially the latest snapshot is absent. |
| `unsupported-build` | Executable failed exact identification and automatic compatibility checks; no guessed offsets are used. |
| `discovering` | Supported runtime is open and camera sampling is starting; can be very brief. |
| `playing` | The last publication contained validated player position and camera data. |
| `loading` | Required data is unavailable or failed validation; see the important distinction below. |
| `stopped` | A game-exit snapshot was published. Can quickly change to `waiting-for-game`. |
| `error` | An outer sampler operation failed; consult `error` and wait for recovery. |

`loading` is a **telemetry availability state**, not a reverse-engineered loading-screen
flag. Loading screens, changing objects, invalid reads and a stalled camera render
context can all produce it. It must not be used as proof that a particular game UI
or worldspace transition is occurring. Statuses may be skipped between polls.

## Snapshot contract

The same snapshot shape is used by HTTP 200 responses, WebSocket text messages,
and successful CLI `snapshot`/`track` output. The following examples are synthetic.

```json
{
  "schemaVersion": "1.1",
  "sequence": 42,
  "capturedAt": "2026-08-31T18:00:00Z",
  "game": { "build": "24994088", "state": "playing" },
  "coordinateSystem": { "unit": "game-unit", "handedness": "right", "upAxis": "y" },
  "capabilities": ["player.position", "camera.transform", "camera.projection", "player.orientation"],
  "player": {
    "position": { "x": 0, "y": 0, "z": 0 },
    "orientation": {
      "source": "player-physics-root",
      "forward": { "x": 0, "y": 0, "z": 1 },
      "up": { "x": 0, "y": 1, "z": 0 },
      "headingDegrees": 0
    }
  },
  "camera": {
    "position": { "x": 0, "y": 2, "z": -5 },
    "up": { "x": 0, "y": 1, "z": 0 },
    "right": { "x": 1, "y": 0, "z": 0 },
    "forward": { "x": 0, "y": 0, "z": 1 },
    "nearPlane": 0.2,
    "farPlane": null,
    "verticalFovDegrees": 50,
    "aspectRatio": 1.7777778
  },
  "quality": {
    "consensusCopies": 1,
    "validCopies": 1,
    "distinctStates": 1,
    "rediscovered": false,
    "captureDurationMicroseconds": 128
  }
}
```

### Envelope and availability

All top-level fields in the 1.1 example are required. Schema 1.2 additionally
requires `lights`. `vector3` below means an object
with three required, finite JSON numbers: `x`, `y`, `z`. There are no string-encoded
numbers, NaN values, addresses, process handles or memory blobs in this contract.

| Field | Type | Meaning |
|---|---|---|
| `schemaVersion` | string | `1.1` normally or `1.2` with requested light telemetry. Compare versions as strings/components, not floating-point numbers. |
| `sequence` | nonnegative integer | Publication counter in this host/command invocation, starting at zero. Not an engine frame number. It continues across game restarts if the same host survives, and resets when a new host starts. |
| `capturedAt` | date-time string | Sampling timestamp with a UTC offset, currently emitted in UTC. Not an engine timestamp or time elapsed since game launch. |
| `game.build` | string | Supported game's Steam build ID. |
| `game.state` | string | Exactly `playing`, `loading` or `stopped`; health has additional statuses. |
| `coordinateSystem` | object | Currently `unit=game-unit`, `handedness=right`, `upAxis=y`. |
| `capabilities` | array of unique strings | Recognized capabilities for this payload; see below. Ordering is not meaningful. |
| `player` | object or null | Null when required telemetry is unavailable. |
| `camera` | object or null | Null when required telemetry is unavailable. |
| `quality` | object or null | Null when required telemetry is unavailable. |
| `lights` | object (schema 1.2 only) | Current nearby engine-light result and diagnostics; never emitted in schema 1.1. |

Currently known capabilities:

- `player.position`: world position.
- `camera.transform`: camera position and basis vectors.
- `camera.projection`: near plane, vertical FOV, aspect ratio; **not** a promise of a known far plane.
- `player.orientation`: validated player physics-root orientation for this sample.
- `lights.engine`: the exact-build Engine-Light reader is available. Its presence
  does not mean `lights.status` is currently `available`.

Important: `loading`/`stopped` messages still list the three base capabilities, but
their `player`, `camera`, and `quality` fields are null. Capabilities alone are not
a validity check. Require `game.state == "playing"` and the objects you need.

### Optional engine lights (schema 1.2)

The light module is disabled by default and currently gated to the exact validated
Steam build `25050808` executable hash. Requesting it on another build produces
`lights.status="unavailable"` with reason `unsupported-build`; it never guesses
offsets. A changing or unreadable scene walk likewise publishes no source array.

An available result has `status="available"`, `source="engine-light-source-array"`,
the configured `nearbyRadius`, a possibly empty `sources` array and diagnostics.
Each source can contain:

| Field | Meaning |
|---|---|
| `position` | Validated world position in the same game coordinates as the player. |
| `kind` | `point` or `spot` only when the engine encoding is recognized; otherwise omitted. |
| `colorLinear` | Engine record's emitted linear RGB base color. |
| `recordActive` | Record-maintenance flag; **not** a promise that the light is visibly emitting. |
| `rendererSelected` | Whether this record is selected for the mapped renderer path. |
| `rendererScale`, `rendererRgbLinear` | Both present only for a selected record with a positive finite scalar. The scalar is authored renderer data, not physical lumens; RGB is `colorLinear * rendererScale`. |

No address, durable ID, range, lumens, direction, generic `enabled` field or
fire/effect source is exposed. Array order and engine handles must not be used as
identity across snapshots or restarts. Multiple records at one position are retained.

Diagnostics distinguish malformed records, valid records outside the configured
radius, unknown kind encodings, unavailable renderer fields and non-positive renderer
scales. `walkChanged`, `walkRetrySucceeded` and `walkUnavailable` are cumulative for
the current game runtime. The reader resolves `module → P → C → S → A` afresh,
checks its backlink and vector bounds, copies the contiguous records, then resolves
the complete walk again. One complete retry is allowed; no previous light snapshot
is reused after failure. This protects against scene transitions but is not an
engine-frame atomicity guarantee.

```json
{
  "schemaVersion": "1.1",
  "sequence": 43,
  "capturedAt": "2026-08-31T18:00:01Z",
  "game": { "build": "24994088", "state": "loading" },
  "coordinateSystem": { "unit": "game-unit", "handedness": "right", "upAxis": "y" },
  "capabilities": ["player.position", "camera.transform", "camera.projection"],
  "player": null,
  "camera": null,
  "quality": null
}
```

### Player

| Field | Type | Meaning |
|---|---|---|
| `player.position` | vector3 | Authoritative world position in game units. Shared coordinate space with `camera.position`; not the physics object's rebased local position. |
| `player.orientation` | object or null; schema also allows omission | Can be unavailable while position/camera remain valid. Current publisher emits null and omits the capability when validation fails. |
| `orientation.source` | string | `player-physics-root`. Root/physics facing, not animated body, bones, aiming direction or camera facing. |
| `orientation.forward` | vector3 | Validated forward unit vector in world axes, already sign-corrected by the reader; do not negate it again. |
| `orientation.up` | vector3 | Validated root up unit vector in world axes. |
| `orientation.headingDegrees` | number or null | Heading in `[0,360)`. Null if the forward vector's XZ length is less than 0.1; the orientation itself may still be valid. |

Do not retain a previous orientation when the field is null/missing or the
`player.orientation` capability is absent. Rolls and gliding animations can occur
while the physics root stays upright. No animated pitch/roll or quaternion is
published. A zero coordinate or a heading of zero is valid, not missing data.

### Camera and quality

| Field | Type | Meaning |
|---|---|---|
| `camera.position` | vector3 | Native render-camera world position in game units. |
| `camera.up`, `.right`, `.forward` | vector3 each | World-space basis vectors; approximately unit length and mutually orthogonal, with `cross(right, up) ≈ forward`. Independent of player facing. |
| `camera.nearPlane` | positive number | Near distance in game units. |
| `camera.farPlane` | positive number or null | Always null in the current native reader: unverified/unknown. **Do not interpret null as a validated infinite far plane.** |
| `camera.verticalFovDegrees` | positive number | Full vertical perspective field of view in degrees, not radians, horizontal FOV or half-angle. Schema permits up to 180; the reader uses stricter plausibility limits. |
| `camera.aspectRatio` | positive number | Projection width divided by height, not a pixel resolution. |
| `quality.consensusCopies`, `.validCopies`, `.distinctStates` | positive integers | Each is 1 for the current single native source. Historical names retained for compatibility, not confidence scores or frame ages. |
| `quality.rediscovered` | boolean | Whether the resolved camera object chain changed relative to the previous accepted chain; normally false on the first capture. |
| `quality.captureDurationMicroseconds` | nonnegative integer | Read/validation duration, excluding transport and client processing. CLI one-shot `snapshot` currently reports 0 rather than measuring it. |

## Coordinate recipes

Positions share world axes with Y up. A game-unit-to-meter conversion and mapping
of X/Z onto compass north/east have **not** been established. Worldspace/scene ID
is not available, so do not infer it from altitude alone.

For either published forward vector `f`, horizontal heading is:

```text
heading = ((atan2(f.x, f.z) * 180 / pi) % 360 + 360) % 360
```

This gives **+Z = 0°, +X = 90°, -Z = 180°, -X = 270°**. Ignore heading near a
vertical forward vector. When deriving camera pitch, use
`atan2(f.y, sqrt(f.x*f.x + f.z*f.z)) * 180/pi`; positive pitch points upward.
Camera Euler angles are derived conveniences, not additional API fields.

To express a world-space point `p` in the published camera basis:

```text
d = p - camera.position
localX = dot(d, camera.right)
localY = dot(d, camera.up)
localZ = dot(d, camera.forward)
```

These are basis coordinates, not screen pixels. When building an engine-specific
view matrix/quaternion, explicitly account for row/column order, handedness and
whether that engine uses +Z or -Z for forward. No matrix-storage convention is
implicitly specified by this vector API. Horizontal FOV can be derived as
`2 * atan(tan(verticalFovRadians / 2) * aspectRatio)` in radians.

## WebSocket delivery and freshness

Connect without a subscription message or subprotocol. The host sends one full
snapshot per UTF-8 text message, not newline-separated records within a message.
Low-level WebSocket libraries must assemble fragments through end-of-message before
JSON parsing. Client text/binary messages are ignored; they do not control the game
or sampling rate. There is no application-level ping command or acknowledgement.

- A new connection receives the last published snapshot if one exists, then new
  publications. That first snapshot can already be old or unavailable. With no
  publications yet, the socket can open successfully and remain silent.
- All clients share one sampler. There is a **one-item, drop-oldest queue per
  subscriber**. Slow clients skip samples; this is a latest-state feed, not a
  lossless recording channel. Already-sent transport buffers can still add latency.
- Do not assume exactly-once delivery: the initial snapshot can overlap with the
  subscription queue. Ignore duplicate/non-increasing sequence values within a
  connection, and reset that comparison when reconnecting to a potentially new host.
- Stationary positions still produce new sequences while the render context advances.
  Sampling faster than rendering can publish the same transform more than once.
- Unavailable-state messages are generally emitted on transition, not continuously
  at the configured rate. A quiet socket does not establish fresh gameplay data.
- The native reader rejects a render-context counter stalled for one second.
  This is separate from your own transport freshness deadline. Double reads and
  pointer checks reduce torn reads; they do not guarantee an atomic engine frame.

Recommended client behavior:

1. Start unavailable. Check schema major version, state, required objects and
   required capabilities before consuming a snapshot.
2. Check `capturedAt` as well as local time since the last **new** sequence. Repeated
   HTTP responses or duplicate messages must not reset a freshness watchdog.
3. Clear dependent output on unavailable data, stale timestamps, socket closure or
   relevant health errors. Do not keep driving a device from the last known transform.
4. Choose a timeout suited to the configured rate (for example
   `max(1500 ms, 3 * 1000 / sampleRateHz)`), and use a monotonic local clock for silence
   detection. Wall-clock changes can affect comparisons with `capturedAt`.
5. Reconnect with a bounded delay when the host restarts. There is no resume/replay.

The 60 Hz default and 240 Hz limit are target polling rates, **not** hard real-time
guarantees, promised FPS, or a promise to capture every rendered state.

## Working examples

- [Python HTTP example](../examples/http_snapshot.py): Python 3.10+, standard library
  only; checks health, distinguishes HTTP 503 health bodies, validates freshness,
  and handles optional orientation. Run `python examples/http_snapshot.py`.
- [JavaScript WebSocket example](../examples/websocket_client.mjs): Node.js 22+,
  built-in WebSocket, no packages. Run `node examples/websocket_client.mjs`.
  It reconnects, rejects unavailable/old/duplicate samples, and clears state after
  a silence timeout. Its callback prints a small display summary, not the public
  JSON Lines contract. Adapt the timeout for low sample rates.

Both accept an optional first argument overriding their default HTTP/WS URL.
They demonstrate consuming the API; they do not launch the host or change game data.
Example regression checks can be run without the game using
`node --test tests/api_examples.test.mjs` (Python must be on PATH, or set `CDT_PYTHON`
to its executable). These check the examples, not overall game compatibility.

For a browser client, serve your page from a loopback origin such as
`http://127.0.0.1:8080`. HTTP responses allow loopback origins through CORS; other
origins do not receive that permission. WebSocket handshakes with a non-loopback
`Origin` receive 403. Desktop clients without an Origin header are accepted.
Opening an HTML file directly (`Origin: null`) is not supported by these checks.
Browser mixed-content/private-network rules may impose additional restrictions.

### CLI / JSON Lines

For clean redirected JSON output, build first, then invoke the host directly:

```powershell
dotnet build .\CrimsonDesertTelemetry.sln -c Release
dotnet .\src\CrimsonDesertTelemetry.Cli\bin\Release\net8.0-windows\crimson-desert-telemetry.dll snapshot
dotnet .\src\CrimsonDesertTelemetry.Cli\bin\Release\net8.0-windows\crimson-desert-telemetry.dll track 600 60 > telemetry.jsonl
```

`track [samples] [hz]` defaults to `0 60`; 0 means unlimited, otherwise the sample
limit is 1–100000. Each stdout line is one complete snapshot; diagnostics go to
stderr. PowerShell redirection encoding varies by version; configure your reader
accordingly. Do not parse build-tool output as telemetry JSON.

Unlike `serve`, `snapshot` and `track` require valid game data immediately and
can terminate on a loading/read error. They do not reconnect or emit a continuous
loading-state stream. Exit codes: 0 success, 1 unexpected failure, 2 invalid command
arguments, 3 unavailable/invalid operation, 4 invalid data or unsupported build.
`diagnose` prints human-readable build checks; `discover` and `trace-camera-copies`
are diagnostics, not this public snapshot contract.

## Compatibility and limits

Ignore unknown optional fields/capability strings and handle null or missing
optional fields. The published schema is strict for the supported 1.1/1.2 shapes
(`additionalProperties: false`); do not use an old strict
schema to reject a future additive revision that your consumer can otherwise handle.
Reject an unsupported major version explicitly. Breaking contract changes require
a new major API/product version; build offsets are not part of the public API.

Not included: fire/effect illumination, weather/time of day, worldspace identifiers, bones,
animated pose, input injection, event history, engine frame IDs or GPU timestamps.
The current camera validator rejects positions more than 50 game units from the
player; distant free-camera/cutscene setups may therefore report unavailable data.
The [documented game build](../README.md#current-support) is manually validated.
Other executable hashes are accepted only when the complete guarded layout is
resolved uniquely and live values continue to pass runtime validation. This is
reported as automatic compatibility, not manual validation. All failures remain
closed. AMD/Intel hardware compatibility has not yet been tested.

## Troubleshooting

| Symptom | Check |
|---|---|
| Connection refused | Game/ASI host is running, `[Server] Enabled=1`, configured port, ASI loader and .NET 8 ASP.NET Core Runtime x64. Inspect bootstrap/host logs. |
| 503 snapshot response | Read the health object; no snapshot has been published yet. Do not parse it as a player/camera object. |
| 200 but null objects or old timestamp | Check `game.state`, health and freshness; wait for valid data, do not reuse old values. |
| `unsupported-build` | The exact hash was unknown and automatic checks failed. Record the health error, EXE hash/version and installed build ID when reporting it; do not bypass the checks. |
| Skipped sequences | Expected for slower clients; reduce client work or configured sampling rate. Not a lossless replay API. |
| Heading seems unrelated to animation | Root facing and camera facing are independent; neither describes all body/bone animations. Check the axis recipe. |
| Browser fails while a desktop client works | Check loopback Origin/CORS and browser security rules. Use a local HTTP origin, not a directly opened file. |
| Upgrade behaves inconsistently | Close the game; ensure DLLs and both `.cfg` metadata companions come from the same package. Preserve only customized INI settings. |
