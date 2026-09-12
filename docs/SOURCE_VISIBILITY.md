# Per-light source visibility

## Goal and current status

For every local source already present in the authored or rendered light feeds,
determine whether solid world geometry blocks the direct segment from the player
to that source. Fire, torch, candle, lamp, point and spot records use the same
test. Camera direction, screen projection, Ambient, image brightness and bounced
light do not participate.

The current implementation is a **research candidate awaiting live acceptance**.
It is compiled only with `CDT_RESEARCH=ON`; production 2.1.10 remains unchanged.
Offline replay separates all 17 preserved labelled controls, and managed/native
protocol tests pass. These results justify a live test but do not prove the game
feature complete.

## Why the previous implementation was replaced

The old path had two concrete contract defects:

1. It traced from the capture camera and could only classify renderer-selected
   contributions. The requested receiver is the player, and every source known to
   either existing light feed must be considered.
2. It required a signed-distance zero crossing. The copied, filtered t233 field
   can represent a blocking wall with samples that remain slightly positive, so
   a zero-crossing sphere trace produced false `clear` verdicts.

The replacement is a separate asynchronous query/result bridge. The managed host
publishes the player receiver and the union of `lights.sources` and
`lights.rendered.sources`. The native SDF worker traces those positions against
one immutable volume and echoes IDs and positions back. The host position-matches
the response onto both arrays. It never changes or removes original light data.

## Trace definition

The receiver is:

```text
(player.x, player.y + 1.0, player.z)
```

The one-unit world-Y lift avoids starting at the physics root's ground contact
point. No camera vector, frustum or on-screen test is sent through the bridge.

t233 is a 128 x 64 x 1040 R16 clipmap with eight levels and cell size
`0.25 * 2^level` game units. The trace:

- ignores the first and last 0.25 game units of the segment;
- selects the finest covering clipmap at each sample;
- advances at one quarter of that level's cell size;
- treats a sample as near-surface when `distance <= 0.5 * cellSize`;
- returns `blocked` after one whole cell of connected near-surface samples;
- otherwise returns `clear` after reaching the target margin.

Connected length rejects isolated interpolation noise. The threshold scales with
the actual clipmap level rather than light type or scene. A blocked trace may have
a positive `closestApproach`; its sign alone is no longer the classifier.

## API contract (schema 1.5 development build)

Optional `sourceVisibility` is attached independently to authored and rendered
source records. The smoothed stream preserves it on the original contributions.

| Field | Meaning |
| --- | --- |
| `status` | `clear`, `blocked`, or `unknown`. |
| `attenuationFactor` | `1` for clear, `0` for blocked, `null` for unknown. |
| `reason` | Null for a usable verdict; explicit reason for unknown. |
| `referencePosition` | Raised player receiver used by the trace. |
| `lightCaptureSequence` | Legacy field name; in schema 1.5 it identifies the independent visibility query. |
| `volumeSequence` | SDF volume sequence, or null if unavailable. |
| `contextFrame` | CPU context frame paired with that SDF volume, or null. |
| `volumeAgeMillisecondsAtCapture` | SDF age when the native result was published. |
| `closestApproach` | Smallest sampled distance for a measured trace; it may be positive for `blocked`. |

Unknown reasons include `waiting-for-volume`, `receiver-moved`, `uncovered`,
`too-short`, `iteration-limit`, `invalid-sdf-sample`, `trace-budget-exceeded`,
`outside-trace-radius`, `stale-volume`, `invalid-context`, `native-fault`,
`game-stopped`, and `waiting-for-current-source`.

The bridge accepts the nearest 256 distinct positions after merging records
within 0.1 game unit. Results expire after 1500 ms. The receiver may move no more
than 0.75 game unit before a response becomes `unknown`. These bounds keep the
research worker finite and ensure stale geometry never silently becomes blocked.

## Preserved evidence

The candidate reproduces 17 labelled controls from retained SDF volumes:

- room: two physically blocked and two physically clear sources;
- same room source: three fresh clear-pose captures;
- Serkis: clear -> blocked -> clear;
- Warspike: two clear -> three blocked -> two clear.

The previous zero-crossing method failed part of this set. The new classifier
passes it in the native implementation and in an independent offline evaluator.
See [live regression evidence](SOURCE_VISIBILITY_REGRESSION.md) for captures and
the earlier [SDF calibration](SDF_VARIANT_A.md) for resource provenance.

## Required live acceptance

Use one fixed player position with at least one clear and one solidly blocked real
fire/candle/lamp. Keep all records shown and verify their positions and verdicts.
Rotate the camera without moving the player; retained sources must keep the same
verdict in front of, behind, and outside the view. Then move so one identified
source follows:

```text
visible -> blocked -> visible
```

The result must be reversible and agree with the wall/building/terrain between
player and source. Repeat across relevant detected light types. A source absent
from both existing light feeds is outside this classifier's input and must not be
reported as blocked.

Only after this live research test passes should the narrow bridge, SDF sampler
and required acquisition be moved into `CDT_RESEARCH=OFF`, followed by the same
controlled production test. `HideOccluded`/F11 may then consume fresh known
verdicts; they remain display-only and must never filter API records.
