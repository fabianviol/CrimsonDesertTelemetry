# Player orientation research

Date: 2026-08-30. Steam build 24994088, executable SHA-256
`B596A498701DFCDC49C486D890C42755DABC8C174314C7F26F7329394452446D`.

Status: **camera-independence, opposing-direction, roll, and lifecycle tests pass**.
The working tree now contains the build-bound read-only `player.orientation` API
implementation. It is not part of the installed package until it has passed a new
build and in-game stream validation.

## Public leads, not copied implementations

- [CD Companion reader](https://github.com/andreikapica-dot/CD_Companion/blob/main/server/memory/reader.py),
  `get_player_heading`: reads the physics object's raw Z-basis components at +0x80
  and +0x88. Despite the reader's "forward" wording, the actual
  [heading helper](https://github.com/andreikapica-dot/CD_Companion/blob/main/shared/coord_math.py)
  identifies these as **backward** and negates X/Z. The first local movement test
  supports that sign convention; do not expose raw BasisZ as player-forward.
  Its physics hook captures RBX as the object and R13 as position.
- [CD Companion hook definitions](https://github.com/andreikapica-dot/CD_Companion/blob/main/server/memory/engine.py)
  and [capture implementation](https://github.com/andreikapica-dot/CD_Companion/blob/main/server/memory/cave_builder.py)
  identify the physics-delta code path. We used these as a research lead, without
  installing their hooks or copying implementation code.
- [CrimsonDesertCoop player manager](https://github.com/blizz3010/CrimsonDesertCoop/blob/main/src/player/player_manager.cpp)
  calls position_struct+0xA0 a likely quaternion needing verification, although
  its documentation calls the rotation verified. It is not accepted as proof.

## Current-build static evidence

- Physics lead uniquely matches at RVA 0x3BB102F. Position write is at
  RVA 0x3BB103C (`movups [r13],xmm0`).
- Containing function starts at RVA 0x3BB0260. It retains incoming RCX in RBX
  and assigns `r13 = rbx + 0x90` at RVA 0x3BB07A5.
- Both identified direct callers load the physics pointer at owner+0x298
  (call sites RVA 0x3077415 and 0x307B894), not the older public +0x248.
- Function RVA 0x3BB0230 passes object+0x60 and object+0xA0 to RVA 0x3A65030.
  The latter uses three vectors at transform+0x00/+0x10/+0x20 to transform a
  min/max box. This supports a transform at object+0x60 and bounding-box data
  at +0xA0/+0xB0, not the proposed quaternion at +0xA0 for this object.

All offsets above are build-specific evidence, not a supported cross-build API.

## Read-only object path candidate

Resolve the existing `world-system-p1` RIP-relative signature to the WorldSystem
global, read that pointer, then dereference these fields in order:

`WorldSystem -> +0x30 -> +0x50 -> +0x68 -> +0x40 -> +0x140 -> +0x298`

Observed RTTI identities:

- After +0x30: `pa::ClientActorManager`.
- After +0x50: `pa::ClientChildOnlyInGameActor`.
- After +0x40 following +0x68: `pa::ClientCharacterControlActorComponent`.

The read-only bounded graph probe searched 25,000 objects in approximately 1.02 s.
It reached its node budget; this is not an exhaustive uniqueness claim. One result
followed the statically identified +0x298 owner link. A separate stack/temporary
identity-matrix result was not selected as the physics source.

The selected physics object contains:

| Offset | Observed role |
|---|---|
| +0x60 / +0x70 / +0x80 | Three three-component basis vectors (16-byte stride) |
| +0x90 | Physics-local position |
| +0xA0 / +0xB0 | Min/max box candidate; not used as rotation |

Only XYZ components of the basis are used; fourth lanes are not assumed zero.

## Initial stationary capture

Ten saved samples, game PID 16600:

- Authoritative player position: (-11157.049, 761.2722, -5969.539).
- Physics position: (-157.04869, 761.3222, -969.53925).
- Difference: (-11000, -0.049987793, -5000).
- Basis X: (0.6618677, 0, -0.7496208).
- Basis Y: (0, 1, 0).
- Basis Z: (0.7496208, 0, 0.6618677).
- Determinant: 1.0000001. Basis and reference camera-forward vector were stable
  across all ten samples.

Local research capture (not distributed):
`player-basis-baseline-20260830.json`.
Contains timestamps, path pointers/RTTI, raw transform bytes, static position, and
reference snapshots from the running telemetry service. Session addresses are
diagnostic data only, never reusable constants.

The experimental reader resolves the signature/path afresh, checks manager/actor
types and basis orthonormality, and writes new capture files without overwriting
old ones. Its position filter currently permits whole-1000-unit origin differences
and a small height offset; that is a research heuristic, not a production validator.
Nine research-host tests pass, including invalid/non-orthogonal/reflected/NaN
basis rejection. No game-memory writes, hooks, or debugger attachment were used.

## Required next validation

1. Camera-only rotation: initial test passed, see below.
2. Player turn/movement: initial test passed, see below; repeat direction/sign checks.
3. Opposing-direction test: initial test passed, see below.
4. Roll test supports upright root/physics orientation rather than animated body
   pose: the basis stayed upright throughout confirmed rolls. Other movement
   modes remain untested; do not advertise animated-body pitch/roll.
5. Post-load reacquisition: initial in-game reload, full restart, and post-teleport
   tests passed, see below. Transitions during loading and playable-character
   changes remain.
6. Only after validation: integrate into the standalone core, versioned build
   definition, schema/capability flags, and API tests. Any quaternion conversion
   must use the verified full basis and documented conventions, not invented axes.

## Controlled camera and player tests

The initial camera/movement captures use the baseline process and physics object.
Reload/restart captures below explicitly track object/process replacement. Each
capture has ten samples from the same supported build. Files are in the local research
workspace's `artifacts` directory and contain full observations, not just summaries.

### Camera-only right turn

Capture: `player-basis-camera-right-20260830.json`.

- Camera-forward changed by 131.7583 degrees (3D vector angle, not pure yaw).
- Player position, physics position, and all three basis vectors were numerically
  unchanged across all ten samples compared with the stationary baseline.
- The source therefore passed this first camera-independence check. This check
  alone would not exclude a stale/frozen transform; the player turn below does.

### Player steps forward and turns toward the camera's viewing direction

Capture: `player-basis-player-forward-20260830.json`.
The user explicitly observed the character turning during the movement.

- Authoritative movement delta: (-2.922, -0.6133, +2.7133), distance 4.0344 game units.
- Physics movement delta: (-2.92195, -0.615, +2.71351). The two deltas differ by
  only 0.001714 game units in norm.
- Raw player BasisZ rotated by 88.3049 degrees; camera-forward changed by
  14.9022 degrees despite no requested right-stick input.
- Raw BasisZ points 176.0166 degrees from the horizontal displacement, whereas
  **negative BasisZ** points 3.9834 degrees from it. This agrees with CD Companion's
  explicit backward-vector convention. Final facing need not exactly match an
  entire curved movement path's net displacement.
- Final basis X=(-0.72971404,0,-0.6837526), Y=(0,1,0),
  Z=(0.6837526,0,-0.72971404); determinant 1.0000002. The final basis was stable
  throughout the ten saved samples.

### Player moves back in the opposite direction

Capture: `player-basis-player-back-20260830.json`.
Compared with the previous forward-movement capture:

- Authoritative movement delta: (+1.3300, +0.6971, -2.9438), distance 3.3047 game units.
- Physics and authoritative movement deltas differ by only 0.001783 game units.
- Player BasisZ rotated by 159.2794 degrees, while camera-forward changed by
  only 0.0632 degrees. This provides a particularly clear independent-player-turn check.
- Negative BasisZ is 1.8963 degrees from the horizontal movement delta, supporting
  the facing sign established above.
- Final basis X=(0.9244329,0,0.3813448), Y=(0,1,0),
  Z=(-0.3813448,0,0.9244329); determinant 1. The same physics object was resolved,
  all ten final BasisZ samples were identical, and reference game state was playing.

These tests support live player-root/physics orientation independent of camera
orientation. They do not yet establish animated-body pitch/roll, other playable
characters, or full restart/transition compatibility. No new public capability is enabled.

### In-game save reload: fresh object and changed world-origin offset

Capture: `player-basis-player-reload-20260830.json`, ten samples taken after the
user reported the reload complete, same PID 16600 and supported executable hash.

- The world/actor-manager pointers remained unchanged, but every path pointer from
  manager+0x50 through the physics object changed. The reader followed the path
  afresh without using the previous object address.
- Physics object changed from decimal 6202899571456 to 6207094603392.
- Authoritative player position is now (-10502.611, 610.5284, -4373.8613), matching
  physics position (-502.61166, 610.57916, -373.86127) after origin/height offset.
- World-minus-physics offset changed from approximately (-11000,-0.05,-5000) to
  (-10000,-0.05078125,-4000). The research position filter accepted the new offset.
- All ten samples resolved the same new physics object with a stable basis and
  determinant 1; all reference game states were playing.
- Basis X=(-0.9848242,0,0.17355508), Y=(0,1,0),
  Z=(-0.17355508,0,-0.9848242).

This passes one post-load reacquisition test, including object replacement and a
changed world-origin offset. It does not test invalidation during the loading
screen, a continuous reader across that transition, or a new game process.

### Full game restart: fresh process and heap pointers

Capture: `player-basis-player-restart-20260830.json`, ten samples after the user
confirmed the game was controllable again. Game PID changed from 16600 to 25732;
the executable hash remained identical and supported.

- Every resolved heap pointer in the path changed, including WorldSystem and
  actor manager. Physics object is now 0x553244BF000 (decimal 5854649380864).
- The unchanged signature/path reader reacquired the object without a new graph
  scan or any saved session addresses.
- Authoritative position (-11157.049,761.2722,-5969.539) agrees with physics
  position (-157.04866,761.3222,-969.53925) at offset
  (-11000,-0.049987793,-5000).
- All ten samples have one stable basis, determinant 1.0000001, and reference
  state playing. Basis X=(0.6618677,0,-0.7496208), Y=(0,1,0),
  Z=(0.7496208,0,0.6618677), matching the initial spawn baseline.

This passes one full-restart reacquisition test on the supported build. It does
not establish support for another game build or continuous transition invalidation.

Separate packaging observation: bootstrap log reports host starts PID 10956 at
10:32:05.777 and PID 25952 at 10:32:06.101, then a host exit code 1 at 10:32:08.296.
At inspection, PID 10956 was still dotnet, PID 25952 was absent, and HTTP health
reported playing, 60 Hz, supported build, no error. The host log had no text.
Duplicate-launch cause is not established; investigate startup coordination before
release. This is separate from the successful external orientation-reader test.

### Fast travel: retained object with updated transform and origin offset

Capture: `player-basis-player-teleport-20260830.json`, ten samples after the user
reported arrival. Same game PID 25732 and supported executable hash as the restart.

- All path pointers, including physics object 0x553244BF000, remained unchanged.
  The object itself now holds the destination's position and a different basis.
- Authoritative position (-10791.692,616.5388,-5495.5527) agrees with physics
  position (-791.6924,616.5888,-495.55273) at offset
  (-10000,-0.049987793,-5000). The X origin offset changed by 1000 game units
  compared with the pre-travel capture despite the retained object address.
- All ten samples have one stable basis, determinant 1, and reference state playing.
  Basis X=(-0.4771608,0,-0.878816), Y=(0,1,0), Z=(0.878816,0,-0.4771608).

This passes one post-fast-travel read at the destination. It does not test the
transition itself, and orthonormality/position agreement alone is not a fresh
visual confirmation of the facing sign at this destination.

### Confirmed dodge rolls: upright root basis, not animated body pose

The first motion capture, `player-basis-player-roll-20260830.json`, is **not a
valid roll test**: the user explicitly reported being unable to perform the rolls
and asked to abort. Retain it only as unvalidated diagnostic data.

The user subsequently confirmed completion for the fresh capture
`player-basis-player-roll-retry-20260830.json`. This capture has 1929 valid samples,
zero rejected samples, and duration 60004.0412 ms in PID 25732. The requested
interval was 20 ms; actual mean spacing was 31.1052 ms (approximately 32.15 Hz),
with maximum gap 32.2390 ms. This is timed sampling, not a per-frame hook.

- Every BasisY is exactly (0,1,0), giving zero measured tilt from world up.
- BasisZ has 24 distinct states and the player position changes. The source was
  therefore live rather than a constant/frozen transform.
- The user clarified that rolls alternated forward/backward and the character
  stood facing approximately 180 degrees away after each roll, without stick input.
  This matches three measured horizontal turns: stable headings approximately
  -125.0047 -> +54.9398 -> -125.1156 -> +54.8290 degrees, using
  atan2(-BasisZ.X,-BasisZ.Z). Transition samples occur around 27.495..27.682,
  29.393..29.579, and 31.477..31.664 seconds. No exact animation-phase alignment
  is asserted without synchronized visual capture. The upright BasisY finding
  concerns lack of tilt, not lack of yaw rotation.
- There are 132 adjacent position steps exceeding 0.02 game units, between
  27.3393 and 32.6592 seconds into the recording. Position changes from
  (-11178.759,764.1278,-5986.5312) to (-11172.996,763.2718,-5982.4746).
- Determinants range from 0.9999998 to 1.0000007.
- No synchronous camera HTTP requests are made in this motion mode, so no
  simultaneous camera-independence claim is derived from this capture.

Together with the user's roll confirmation, this supports **player root/physics
orientation**, not the visible rolling body/bone pose. The public semantic contract
must identify that source explicitly. A quaternion derived from this basis would
represent this root orientation; it would not recover animated pitch/roll.
Sampling cannot exclude motion entirely between samples; this is evidence for
the tested movement mode and build, not a claim that every possible pose is upright.

### Abyss teleport-point baseline: readable, worldspace identity unknown

User-confirmed location: a teleport point in the Abyss (not a Nexus). Capture:
`player-basis-player-abyss-teleport-20260830.json`, ten stationary samples,
same PID 25732 and supported build as the confirmed roll capture.

- Authoritative position is constant at (-10665.498,1795.1217,-3700.3325).
- Physics position is (-665.49805,1795.1718,-700.3325); world-minus-physics
  offset is (-10000,-0.050048828,-3000), constant across all ten samples.
- WorldSystem, manager, and every player-path pointer, including physics object
  0x553244BF000, are unchanged from the preceding surface capture.
- All ten basis observations pass validation (determinant 1..1.0000001).
  BasisY remains (0,1,0). Heading varies only from 134.918150 to 134.919579
  degrees, so distinct raw basis values do not imply substantial player rotation.
- Camera/player API references are present and report playing; camera/player
  separation is approximately 6.42464 game units.

The existing coordinate/path convention works at this Abyss location without
special-case offsets. Neither the origin offset nor retaining a WorldSystem
pointer proves that Abyss is the same logical worldspace as the surface. No
validated worldspace identifier is currently exposed by this telemetry reader.
Continuity during the planned jump/fall remains to be measured; even continuous
coordinates alone would not establish logical scene/worldspace identity.

### Abyss descent, flight and surface landing

Capture: `player-basis-player-abyss-flight-20260830.json`; approximate user-event
notes: `player-abyss-flight-20260830-markers.md` in the local artifacts directory.
User reports: falling then flying, a brief stamina-limited slow-flight activation,
the visible cloud/fog transition, fast fall near the end, then slow flight to land.
Initial slow-flight and transition messages were composed approximately 3–4 s
after their events; host receipt markers have additional unknown latency. Do not
treat them as exact animation or engine-transition timestamps.

- PID 25732, supported build; 4157/4157 valid physics observations, zero rejected
  reads. Recording lasted 129.3195 s and stopped normally on the landing request.
  Mean sample spacing 31.1069 ms (about 32.15 Hz), maximum 34.7539 ms.
- 639 asynchronous camera/API snapshots, zero HTTP errors or null cameras;
  every sampled API state reports playing. Mean reference spacing 202.497 ms,
  maximum 219.287 ms; sequences always advance, by 11..14 between polls.
  These are independent samples with timestamps, not synchronized render frames.
- Position starts at (-10677.206,1794.767,-3685.3948) and ends at
  (-10679.92,636.7214,-3680.0503): approximately 1158.046 game units of descent.
  The last position distinct from the final resting position occurs at 96.244 s.
- Every resolved path pointer, including physics object 0x553244BF000, stays the
  same. All 4157 BasisY values are (0,1,0): the measured root remains upright
  during this user-confirmed flight sequence, not aligned with the flying body.
- Horizontal world-minus-physics origin remains near (-10000,-3000), with maximum
  horizontal residual norm 0.03913 game units. The vertical difference varies
  from -0.05115 to +0.78058 game units. Do not hardcode the stationary -0.05
  height difference; the reads are not synchronized and its dynamic cause is not
  established.
- Maximum adjacent world-position step is 2.08746 game units in 30.547 ms,
  entirely vertical, at 88.995 s. No large coordinate reset or read outage is
  observed, including around the approximate cloud/fog transition window.
- Position-derived one-second averages show roughly 30 units/s descent at
  73..85 s, about 50 units/s at 87..90 s, then about 2 units/s at 93..95 s before
  settling. This is consistent with the reported late fast-fall/braking sequence,
  not a discovered in-game movement-mode flag. The initial brief slow-flight
  phase is not independently identified from the approximate text markers.

Conclusion: the tested Abyss-to-surface descent uses continuous readable
coordinates and the same player object/path. A LOD/asset-streaming transition is
compatible with this evidence, but not proven; distant LODs are also rendered.
Logical worldspace/scene identity remains unknown without a validated identifier.
The captures establish the evidence for the build-bound capability; they do not
establish animated body/bone pitch or roll. The API therefore publishes the
physics-root basis only and reports orientation as unavailable during invalid or
changing actor chains.
