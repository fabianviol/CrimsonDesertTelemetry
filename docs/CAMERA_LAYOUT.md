# Render camera layout

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

## Freshness model

Renderer memory contains both current and historical records. The tracker therefore:

1. discovers every structurally valid copy;
2. refreshes those addresses for each sample;
3. discards cleared or invalidated records;
4. quantizes complete camera state and selects the largest consensus group;
5. triggers structural rediscovery if no cached copy remains valid.

This design avoids selecting a record based on a single changing scalar or a session-specific pointer.
