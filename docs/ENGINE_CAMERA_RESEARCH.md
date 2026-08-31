# Camera source before Streamline

Observed on Steam build **24994088**, Streamline **2.11.1**, NVIDIA hardware.
The native source first shipped in the preview.7 candidate and is used by v1.0.0.
The user confirmed native-camera operation after a restart; the final v1.0.0
installed-file/startup test is recorded in [MOD_MANAGER_VALIDATION.md](MOD_MANAGER_VALIDATION.md).
The historical experiments below do not imply validation on every GPU or game build.

## Direct API test

The bounded `tools/StreamlineProbe` hardware-breakpoint diagnostic observed
`sl.interposer.dll!slSetConstants` and detached after each measurement:

- DLSS on: 242 calls / four seconds, all with recognized constants and plausible cameras.
- Upscaling off: zero calls / four seconds, same process and export, 75 threads armed.
- DLSS on again: 242 calls / four seconds. The game caller return RVA was
  `0x386B64E` for every captured call.

This establishes behavior of this game configuration, not all Streamline integrations.
The actual API function is not intercepted by the existing release reader.

## Upstream object

Game function RVA `0x386B370` receives the source as its third argument, retains
it in RBX, and constructs Streamline constants on its stack. At API entry, RBX
therefore exposes that source. All 242 calls in the second on-trace used the same
source address; addresses remain private, session-specific diagnostic data.

Offsets from this source, derived from that function's instructions:

| Data | Source offsets |
| --- | --- |
| World position, float3 | `0x80, 0x84, 0x88` |
| Forward, float3 | `0x90, 0x94, 0x98` |
| Right, float3 | `0x3E0, 0x3F0, 0x400` |
| Up, float3 | `0x3E4, 0x3F4, 0x404` |
| Projection, float4x4 | `0x4E0` |
| Near value | `0x860` |
| Raw far-related field | `0x864` |

Vertical FOV is computed by the game as `2 * atan(1 / projection[1][1])`.
For the observed symmetric projection, `projection[1][1] / projection[0][0]`
matches the API aspect ratio. This is not a general off-center-projection contract.
The raw far field is **zero** while Streamline receives `FLT_MAX`; do not export
the raw field as a validated far-plane distance.

After disabling DLSS and deliberately changing yaw and pitch, a read-only two-second
sample of the same source returned **65/65 plausible cameras** with a changed
heading (approximately -131.44 to -35.10 degrees) and changed pitch. Sixteen
transform changes were observed during sampling; these may include camera bob.
Direction vectors and projection matched the prior direct API reference while
DLSS was on. The comparisons are not simultaneous atomic engine-frame captures.

Private recordings: `artifacts/streamline-api-dlss-*.jsonl` and
`artifacts/engine-camera-source-dlss-*.jsonl`.

## Native ownership and automatic diagnostic resolver

On the same process, with upscaling still off, bounded write watches traced the
source back to the native renderer camera object. The source pointer is at
camera `+0x428`; camera position at `+0xC8` is copied to source `+0x80` by
the function at RVA `0x34C5690`. The camera setter is at RVA `0x3292720`.
Call-site tracing independently connected this object to the game's main root.

Two routes agree in the current session (offsets below are hexadecimal):

```text
*[EXE + 6259638]                                         -> camera
*[EXE + 62593B0] -> *[+28] -> *[+18] -> *[+E0]           -> camera
camera -> *[+428]                                        -> source block
```

The intermediate context before `+E0` has vtable RVA `0x53E0008`; the camera
has vtable RVA `0x53BED60`. These are build-specific fingerprints, not recovered
class names. The load at RVA `0x284B749` reads global `0x6259638` and immediately
reads the camera position at `+0xC8/+0xD0`. Main-root loads at RVA `0x2EA08A`
and `0x2EA0E3` reference global `0x62593B0`.

`StreamlineProbe --read-engine <PID> <seconds 1..10> <new-output.jsonl>` now
uses these guarded instructions and both pointer routes. It accepts no previous
heap address, re-resolves each sample, checks both vtables, and compares two
successive camera-field reads. It performs no heap scan, debugger attachment,
injection, or game-data writes. Two agreeing reads are **not** a guarantee of
engine-frame atomicity. Instruction guards do not constitute cross-build support.

The first automatic three-second read returned **97/97 valid samples** with
upscaling off. Initial reference resolution took approximately **0.019 ms**,
excluding process/module setup; this is not a full plugin startup benchmark.
The camera was stationary during this recording, so it is not an additional
motion test. Recording: `artifacts/engine-camera-auto-off-01.jsonl`.

Self-tests pass for matching paths, conflicting paths, wrong vtable, missing
context, and source-pointer reallocation. Debugger fixture tests also confirm
normal detachment, hardware-breakpoint removal, and continued target execution.
Native call traces are saved as `artifacts/engine-camera-*-callers-01.jsonl`.

### First cold start without upscaling

After the user fully restarted with upscaling off, a new process and a different
heap source address were found automatically: **97/97 valid samples** over three
seconds, with 47 transform changes (not yet a controlled motion test). Initial
reference resolution took approximately **0.015 ms**, excluding process/module
setup. No old heap address, Streamline call, or debugger was required. Recording:
`artifacts/engine-camera-auto-coldstart-off-01.jsonl`.

After a controlled camera-only turn/tilt in that fresh process, another **97/97**
samples passed. Heading changed from approximately -152.62 to -49.80 degrees,
and pitch from -7.75 to -34.84 degrees. Recording:
`artifacts/engine-camera-auto-coldstart-off-turned-01.jsonl`.

## Preview.7 production integration

`EngineCameraReader` now backs `discover`, `snapshot`, `track`, and `serve`.
Only `trace-camera-copies` retains the old heap scanner. The executable hash,
instruction bytes/RIP targets, both pointer routes and vtable fingerprints are
checked. Each capture makes at most three attempts, comparing camera fields and
the context counter at `context+0x40` around its reads. A counter unchanged for
one second makes the camera unavailable; counter wrapping is supported. These
checks do not guarantee atomic engine-frame snapshots.

Thirty managed tests pass, including native mapping, ASLR/new heap addresses,
instruction mismatch, conflicting roots, invalid data, concurrent writes,
loading/source replacement, and counter stall/recovery/wrap. The production CLI
returned **180/180 playing samples at requested 60 Hz** on the cold-start process
with upscaling off, including player orientation. Capture time averaged **193 us**,
maximum **1736 us** in that short stationary run. The separate loopback HTTP/
WebSocket smoke test passed. This is not yet a packaged HUD or other-upscaler test.

JSON schema 1.1 is unchanged. Native quality counts are 1/1/1, not copy consensus.
The unvalidated raw far field remains unknown/null in the public output.

## Still required

- Repeat cold-start checks during integration; the first upscaling-off cold start passed.
- Verify live yaw/pitch, movement, reload, teleport, and source lifetime/reallocation.
- Check coherent reads and distinguish active from historical or secondary cameras.
- Validate other upscalers; AMD/Intel hardware remains untested.
