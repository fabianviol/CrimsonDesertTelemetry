# What triggers antivirus detections on this ASI — measured, 2026-09-11

Nexus rejected the 2.0.1 package on its scan. This records what was actually
measured afterwards, so the next person does not repeat the guessing. Every row
below is a VirusTotal result on a preserved binary, not an inference.

The tools: `scripts/Get-VirusTotalVerdict.py` (hash lookup or upload, keyed from
`.env`) and `scripts/Compare-PeSurface.py` (sections, entropy, imports).

## Two independent detections, two different answers

### Microsoft `Trojan:Win32/Wacatac.B!ml` — NOT ours, do not cut code for it

**The control that settles it:** the 2.0.0 source, checked out at tag `v2.0.0`
and rebuilt with today's toolchain, is flagged.

| binary | built | verdict |
|---|---|---|
| 2.0.0 as published | 2026-09-06 | 4/70, no Microsoft (Trellix instead) |
| **2.0.0 source, rebuilt** | **2026-09-11** | **4/71, Microsoft `Wacatac.B!ml`** |

Identical source, different compiler, different answer. The rebuilt binary is
1,344,512 bytes against the original's 1,353,216, and the import table shows the
toolchain moved underneath us: `CreateFile2` and `GetSystemTimePreciseAsFileTime`
gone, `TlsAlloc`/`TlsFree`/`TlsGetValue`/`TlsSetValue` and
`InitializeCriticalSectionAndSpinCount` arrived. None of those are APIs this code
calls; they are CRT internals.

It also **flickers**, which no feature-caused detection does:

| build | date | Microsoft |
|---|---|---|
| 2.0.0 published | 09-06 | — |
| local-lights.1 | 09-07 | yes |
| ambient-probe.1 | 09-08 | yes |
| spatial-probe.1 | 09-08 | yes |
| spatial-readback.5 | 09-09 | **—** |
| local-ambient.1 | 09-10 | — |
| 2.0.1 / 2.0.2 | 09-11 | yes |

`!ml` is a machine-learning verdict and behaves like one. **No amount of removing
functionality fixes this.** The only real remedies are a false-positive report to
Microsoft and, structurally, an Authenticode signature. Do not trade features for
it.

### Bitdefender `Gen:Variant.Application.Barys.73277` — ours, and pinned

This one is deterministic: absent in every build before a point, present in every
build after it. Eight brands report it (ALYac, Arcabit, BitDefender, CTX,
Emsisoft, GData, MicroWorld-eScan, VIPRE) because they share one engine; six give
the identical signature id. It is what took the 2.0.1 **ZIP** from 0/65 to 8/66
and therefore what Nexus reacted to.

Bisected over the preserved packages:

| build | time | Barys |
|---|---|---|
| spatial-probe.1 | 09-08 21:46 | clean |
| spatial-readback.5 | 09-09 **13:04** | **clean (3/70)** |
| spatial-series.1 | 09-09 **14:28** | **13/70, full family** |
| spatial-sampler.2 | 09-09 17:06 | 13/71 |
| local-ambient.1 | 09-10 20:19 | 12/70 |
| 2.0.1 | 09-11 | 12/70 |

Exactly one native change sits between the last clean and the first flagged
build. The package was built at 14:28:59 and the work committed a minute later as
**`6937fa9` "lift the one-shot into a bounded repeated measurement series"**:
`spatial_probe.cpp` +117, `spatial_readback.cpp` +52, matching the +14,848 bytes
the binary grew.

So the signature matches compiled code from the bounded repeated GPU-texture
readback loop. It introduced **no new imports and no new API pattern** — the
change is structural, turning one copy into a repeated series.

## What was ruled out, by testing rather than argument

- **Imports.** The weighted API set is byte-for-byte identical between clean and
  flagged builds: `VirtualProtect`, `WriteProcessMemory`, `CreateRemoteThread`,
  `SetWindowsHookEx` and the rest are either in both or in neither.
- **New DLL dependencies.** None, in any comparison.
- **Entropy and section layout.** Unchanged across the transition.
- **The ~1 MB static `SpatialTrace` buffer.** The obvious structural suspect:
  `.data` grew from 24,272 to 1,041,120 bytes with research compiled in, entropy
  dropping to 1.765. Moving the event array to the heap took `SpatialTrace` from
  995,440 to 12,408 bytes and `.data` back to 58,080 — **and the verdict did not
  change at all**, still 12/70 with the full family. The file size was identical
  before and after, because a BSS reservation costs no bytes on disk, so a
  content-matching signature was never going to care. The change was kept on its
  own merits, not as a fix.

## What this means for shipping

Releases are built with `CDT_RESEARCH=OFF`, which excludes the readback series,
and the 2.0.2 ZIP scans **0/67**. The research functionality is not lost; it is
simply not in public packages, which is where it belongs — it is a private
diagnostic that no released feature reaches.

The open question is what happens when ambient or occlusion become product
features that must ship. The measurement above is encouraging for that: the
trigger is in the **bounded repeated measurement series**, a diagnostic harness,
not in the ambient feed or the distance-field sampling themselves. A production
feature need not carry that harness.

If a future release must ship the series, the options in order of sense are:

1. **Report the false positive to Bitdefender**, now with something worth
   reading: the exact commit, the file, the fact that it is a D3D12 texture
   readback loop, and a clean/flagged binary pair differing only by that change.
   One report covers all eight brands.
2. **Restructure the loop and re-measure.** The feedback cycle is short now --
   build, upload, read the verdict -- but it is empirical, and a signature that
   moves for an unrelated reason teaches nothing.
3. **Sign the binary.** The real structural answer for every ML verdict here,
   including Microsoft's. It costs money and reputation takes time to accrue.

Do not cut features on a hunch. Both detections were attributed wrongly at first
glance, and both attributions were overturned by a measurement that took minutes.
