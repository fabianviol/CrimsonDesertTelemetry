# Sky-visibility stream — contract draft

**Draft, not implemented and not published.** This fixes the shape and the promises
before any code, so the guarantees come from what was measured rather than from
what would be convenient. Every limit below is a measured result; see
[local illumination research](LOCAL_ILLUMINATION_RESEARCH.md) for the evidence.

This feed is vendor-neutral. It carries a geometric occlusion factor, not a lamp
colour and not a consumer-specific model.

## What the value is

Sky visibility at a point in space: the share of directions from that point that
reach the sky, reduced by ground, terrain, buildings, roofs, trees, clouds and
anything else standing in the engine's voxel volume.

Read as a fraction of the **full sphere**, not the upper hemisphere. A point
standing on open ground therefore reads about **0.53, and that is its maximum near
the ground**, because the lower half of the sphere is earth. Measured values run
from 0.53 in the open down to 0.000031 deep under a roof, with 0.000000 below the
surface.

It is **not** irradiance, illuminance, lux, room brightness, a lamp colour, a
percentage of visible sky as a person would judge it, or per-source occlusion. It
answers "how open is this spot to the sky", nothing else. Combining it with the
existing [global sky stream](AMBIENT_STREAM.md) can drive an artistic ambient
effect, but such a product is a consumer's model and must not be presented as a
measurement of room brightness.

## Reference position

The value is anchored to the **camera position**, not the player root. This was
measured, not assumed: in five live series the decoded reference equalled the API
camera position to 0.00 game units. The measured third-person boom is about 6.5 gu,
collapsing to about 2.2 gu when a wall stands behind the camera, so a consumer that
assumes the player position will be wrong by metres — enough to cross from "under a
roof" to "in the open".

## Endpoints

```
GET  http://127.0.0.1:27311/v1/sky-visibility
WS   ws://127.0.0.1:27311/v1/sky-visibility/stream
GET  http://127.0.0.1:27311/v1/sky-visibility/schema
```

Loopback only, latest-only queue, same origin policy as the existing streams.
HTTP 200 is not proof of usable data: inspect `status` and `ageMilliseconds`.

## Payload

```json
{
  "schemaVersion": "1.0",
  "status": "available",
  "reason": null,
  "captureSequence": 412,
  "frameNumber": 36664,
  "capturedAt": "2026-09-09T15:05:59.3078857+00:00",
  "ageMilliseconds": 820,
  "reference": {
    "position": { "x": -10411.0, "y": 614.14, "z": -4419.57 },
    "positionSource": "camera",
    "skyVisibility": 0.158708192
  },
  "samples": [
    { "offset": { "x": 3, "y": 0, "z": 0 }, "skyVisibility": 0.179157649 }
  ],
  "scale": "fraction-of-sphere-candidate",
  "openSkyReference": 0.53
}
```

`samples` is **self-describing**: every entry carries the offset it was taken at,
so a consumer never hardcodes a pattern and a later change of pattern does not
break it.

The default pattern is **26 directions at two radii, 52 positions**. The directions
are every combination of -1, 0 and +1 except the centre, normalised: six axes,
twelve edge directions and eight corners, which puts the widest angular gap at
about 45 degrees. The radii are **3 and 6 game units**.

Both choices follow from measurements rather than taste. A coarser 14-direction set
leaves 55-degree gaps, and the field can swing by a factor of 70 across 180 degrees
at a doorway, so a lamp could fall between two very unequal neighbours. Two radii
are used because the value changes substantially between 2 and 5 gu — 0.0052 to
0.00066 into a building, 0.374 to 0.318 outward — and one radius throws that
falloff away. Sampling denser than this would mostly re-read the same voxels, since
the grid is 1 gu and neighbouring directions at 3 gu radius are about 2.3 gu apart.

Cost is not a reason to be sparing here: each sample is a trilinear read of a volume
already in memory, and 52 of them is a few kilobytes of JSON at about 1 Hz.

Samples are **neighbouring positions, not directions**. The value at an offset is
the sky visibility at that point, which is what a lamp standing over there would
sit in. It is not "sky visibility toward that direction from the camera".

## Scope: this stream is ambient only

What this stream publishes — a sky-visibility value per point — is not an answer to
"is that fire visible". Source visibility is a line of sight between two points,
not a property of one point, so nothing in this payload should be presented as
answering it.

That is a statement about this stream, not about the underlying volume. A first
offline test suggests the same volume can support a segment test, and if that holds
up, per-source occlusion would reuse this machinery rather than needing the
separate depth route. See "Segment occlusion" in
[local illumination research](LOCAL_ILLUMINATION_RESEARCH.md). Until a controlled
experiment settles it, neither approach is committed to.

## Limits a consumer must respect

- **Freshness is bounded by the engine, not by us.** The voxel volume refreshes in
  amortised blocks of 16 z-slices that rotate: about a quarter of the volume changes
  between reads 1.5 s apart, and in every observed step at least one block was
  byte-identical. A point's value only changes when its block is swept, so a reading
  can be **several seconds stale** no matter how fast the feed runs. Sub-second
  responsiveness at a point is not available at all. Smoothing is mandatory.
- **Samples within one payload may be of different ages**, because neighbouring
  offsets can lie in different refresh blocks. They are not simultaneous.
- **Offsets wrap toroidally.** The clipmap spans 64 gu in X and Z and only 32 gu in
  Y. Offsets beyond that wrap into unrelated space: sampling +15 gu in Y returned
  exactly 0.0000 and +30 gu returned precisely the value of -2 gu. The stream will
  refuse offsets outside |x|,|z| <= 16 and |y| <= 8 rather than return wrapped data.
- **Offsets reuse the clipmap selected for the reference** instead of recomputing
  it, which holds for small offsets well inside the clipmap and is part of why the
  bound above exists.
- **No value is published on the shader's fallback branch.** That branch yields 1
  without texture coverage, which is an absence of data and not open sky. The
  stream reports `status: "unavailable"` with a reason instead.
- **The value moves while standing still**, by roughly 10% over seconds even under
  open sky, because the world changes and the volume is refreshed in blocks.
- **Not a complete census.** Like the existing light feeds, this describes what the
  engine's structure holds, not ground truth.

## Freshness and cadence

Publication is opportunistic, at best around 1 Hz. The achieved cadence in live
series was 1031..1531 ms against a 1000 ms configured minimum, limited by the
probe's own throttle and by exposure dispatch selection rather than by the copy.
Given the amortised refresh above, a faster feed would not carry fresher data.
Reject data older than a bound of your choosing and expire the last sample when the
transport goes silent.

## Status of this draft

Nothing here is implemented. The native sampler that would produce these values
exists and is verified: 104 of 104 values in a live series matched the offline
decoder exactly, and the payload measured 1465 bytes against 540672 for the volume
it replaces. What is missing is continuous publication — today the readback runs as
a bounded series per process behind an explicit event — and the server side.
