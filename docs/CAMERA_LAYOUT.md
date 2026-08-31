# Render camera layout

This document describes the historical Streamline-facing copy scanner (through
preview.6). Preview.7 uses the [native engine source](ENGINE_CAMERA_RESEARCH.md)
instead; the old scanner remains an explicit diagnostic tool only.

## Record

The scanner treats the beginning of the position vector as record offset `0x00`.

| Offset | Type | Meaning |
|---:|---|---|
| `-0x08` | `float` | `FLT_MAX` sentinel |
| `-0x04` | `float` | `FLT_MAX` sentinel |
| `+0x00` | `float3` | Camera world position |
| `+0x0C` | `float3` | Up vector |
| `+0x18` | `float3` | Right vector |
| `+0x24` | `float3` | Forward vector |
| `+0x30` | `float` | Near plane; observed as `0.2` |
| `+0x34` | `float` | Far plane; observed as `FLT_MAX` |
| `+0x38` | `float` | Vertical FOV in radians; observed as `0.87266463` (50 degrees) |
| `+0x3C` | `float` | Aspect ratio; observed as `1.775` |
| `+0x40` | `float` | `FLT_MAX` sentinel |

The three basis vectors are unit length, mutually orthogonal, and right-handed. Consumers should use the vectors directly instead of assuming a particular Euler-angle convention.

## Validation evidence

The record was identified through controlled camera-only pitch and yaw changes while the authoritative player position remained unchanged. Player movement followed by automatic camera movement kept the player-to-camera separation within a plausible third-person distance.

In the first discovery session the structural scanner found 121 valid copies. After a full game exit and restart, every old address was invalid and the same address-independent scanner found 103 new coherent copies. A continuous test refreshed roughly 100 valid copies and selected a 48–49-copy consensus across seven retained states.

The record is believed to be a renderer-facing camera-constants structure rather than a stable engine object. Its absolute address is session-specific and must never be published as a reusable offset.

## Source provenance and hardware coverage

The original CPU write/copy into a candidate was observed in NVIDIA Streamline
`sl.common.dll`. Later changes were not captured by the same CPU write watch;
driver-managed memory is a hypothesis, not a demonstrated ownership mechanism.
The production reader scans memory for the layout rather than calling Streamline.

Validation so far is on the user's NVIDIA setup. AMD/Intel hardware and operation
with DLSS disabled have not been validated. Neither universal GPU compatibility
nor a hard NVIDIA/DLSS requirement has been established. The public transport is
vendor-neutral; player position and physics-root orientation use a separate source.

## Freshness model

Renderer memory contains both current and historical records. The tracker therefore:

1. discovers every structurally valid copy;
2. refreshes those addresses for each sample;
3. discards cleared or invalidated records;
4. initially uses the largest quantized consensus group until temporal evidence exists;
5. counts observed transform/projection changes per address over a two-second window
   (expanded up to eight seconds for sparse sampling, including 1 Hz);
6. qualifies copies with at least three changes and at least 60% of the most active
   copy's count, then reads the most recently changed qualified copy;
7. retains a learned source during stillness instead of restoring an old majority;
8. triggers structural rediscovery when no valid copy remains, or when the learned
   sources disappear and no new active source qualifies.

This design avoids selecting a record based on a single changing scalar or a session-specific pointer.

Temporal ranking is based on reader observations, not an engine frame identifier;
it does **not** prove that the chosen state is the newest rendered frame.
`capturedAt` measures the reader's sampling time, not the transform's production
time. The user-reported lag/backstep on 2026-08-30 was reproduced in a controlled
trace. Preview.6's production-code replay yields 803 right-turn changes without
backwards steps; actual game acceptance after deployment is still pending. No
smoothing or direction clamping is applied. Exact transform/projection changes
count as activity; changing player distance alone does not.

`quality.consensusCopies` counts copies matching the selected state, which no longer
has to be the largest group. `validCopies` and `distinctStates` still cover the
whole valid cached set. See [diagnostic evidence](CAMERA_FRESHNESS_DIAGNOSTICS.md).

The public API exposes `farPlane` as `null` when the record contains `FLT_MAX`, meaning effectively infinite. It exposes the projection angle as `verticalFovDegrees`; basis vectors remain the canonical orientation representation.
