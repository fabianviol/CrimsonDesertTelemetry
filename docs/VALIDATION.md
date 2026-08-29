# Live validation

## Test environment

- Date: 2026-08-29/30
- Distribution: Steam
- Steam build ID: `24994088`
- Executable version: `1.0.0.2658`
- Executable SHA-256: `B596A498701DFCDC49C486D890C42755DABC8C174314C7F26F7329394452446D`
- Server rate: 240 Hz unless otherwise stated
- Access mode: external process query/read only

## Results

### Address-independent discovery

Cold structural scans after the SIMD/parallel optimization completed in approximately 6.66–12.03 seconds across the observed sessions, down from about 53 seconds in the original scalar implementation. One representative run completed in 11.05 seconds and found 117 valid records, with 58 copies in the winning current-state consensus.

Every complete game restart invalidated the old runtime addresses. The scanner recovered a new coherent address family without using a prior address.

### Sampling and transport

- Requested server rate: 240 Hz
- Measured rates: 240.1 Hz and 241.4 Hz over multi-second windows
- Cached memory refresh median: 117–218 microseconds across measured states
- Cached memory refresh P95: 157–279 microseconds
- WebSocket test: 480 consecutive messages in 1.951 seconds
- WebSocket sequence loss: zero

Rates above the game's render rate may repeat a state. The high sample rate exists to minimize observation latency and support consumers other than lighting systems.

### Controlled camera movement

With the player position bit-identical, a horizontal camera turn changed the forward vector by 121.58 degrees. All three basis-vector lengths remained within approximately `4e-8` of one, and pairwise dot products remained around `1e-8`.

A controlled vertical tilt changed the derived pitch from approximately -7.86 to -81.31 degrees while the player position remained bit-identical. Near plane, vertical FOV, and aspect ratio remained stable at `0.2`, `50` degrees, and `1.775`.

### Controlled player movement

A short player step changed player position by approximately 0.096 game units and camera position by approximately 0.097 game units. The camera followed in the same direction while retaining a plausible third-person separation.

### Teleport

A Nexus teleport produced a 615.80-unit player-position jump. The server, sequence, and WebSocket connection remained active. The post-teleport camera was valid; all 45 remaining valid copies agreed on the same state and no rediscovery was required.

### Complete process restart

The same server and WebSocket client observed `playing -> stopped -> loading -> playing` across a complete game exit, relaunch, and save load. During testing, the game exposed renderer-valid loading-screen cameras while the static world-player globals passed through origin/height-1000 sentinel values. Those states are now emitted as `loading` with null player/camera/quality rather than mislabeled as gameplay telemetry.

### In-process save reload

The final save-reload test observed `playing -> loading -> playing` without publishing a loading sentinel as gameplay. The first new `playing` message contained a real world position. The camera address family had changed, automatic rediscovery was triggered, and sampling resumed at 241.4 Hz without restarting the server or WebSocket client.

## Scope

These results validate one machine and one executable hash. New builds and additional hardware should be independently tested. The project fails closed on unknown executable hashes and records build-specific evidence under `definitions/`.
