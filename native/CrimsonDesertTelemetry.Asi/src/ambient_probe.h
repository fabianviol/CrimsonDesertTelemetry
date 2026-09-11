#pragma once
#include <array>
#include <cstdint>
#include "exposure_probe.h"

namespace cdt::render
{
// Private research file, NOT a public telemetry schema. Followed by the paired
// 2816-byte SceneConstantBuffer, 1024-byte ambient output and (v2 only) the
// ExposureCacheContext appendix. Original v1 files remain valid, unchanged.
constexpr uint32_t AmbientBytes = 1024;
struct AmbientRecordHeader
{
    uint32_t magic = 0x41445443, version = 2, recordBytes = 64 + 2816 + AmbientBytes + sizeof(ExposureCacheContext), pid = 0;
    uint32_t frame = 0, producerRva = 0, flags = 0, sequence = 0;
    uint64_t capturedTick = 0, resource = 0, outer = 0, sky = 0;
};
static_assert(sizeof(AmbientRecordHeader) == 64);
// Live-code evidence: binding lookup -> [sky+98] -> UAV binder -> Dispatch(1,1,1).
// Build 25246367 relocated both paths by +0x21C0 from build 25116796. Each hook
// signature is still unique image-wide and every surrounding anchor matched at the
// shifted address; offline evidence in artifacts/recovery/20260911-build-25246367/.
// These are exact-executable addresses: promote them with the build, never alone.
constexpr std::array<uint32_t, 2> AmbientHookRvas{0x384BD77, 0x384ED63};
// The [sky+0x98] source-field load preceding each hook. Held here rather than
// inline so one relocation touches one place.
constexpr std::array<uint32_t, 2> AmbientSourceRvas{0x384BA6F, 0x384EC9B};
constexpr std::array<uint8_t, 15> AmbientSignatureA{
    0x48,0x8B,0xCB,0xE8,0x81,0xD0,0xF8,0xFF,0x48,0x8B,0x03,0x48,0x8B,0xCB,0xFF};
constexpr std::array<uint8_t, 15> AmbientSignatureB{
    0x48,0x8B,0xCF,0xE8,0x95,0xA0,0xF8,0xFF,0x48,0x8B,0x07,0x48,0x8B,0xCF,0xFF};
bool CheckAmbientPreflight(uint64_t moduleBase);
// Mutually exclusive with regular ManyLights. Reuses the tested copy/fence
// machinery; never publishes ambient bytes as lights. Opt-in, bounded, no API.
// Starts IDLE. A process-specific named event explicitly starts each bounded run;
// no loading/menu heuristic. Only cleanly completed runs can be restarted.
bool StartAmbientProbe(uint64_t moduleBase, const wchar_t* outputDirectory);
}
