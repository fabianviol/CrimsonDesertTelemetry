#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include "native_contract.generated.h"

namespace cdt::render
{
constexpr uint32_t Magic = 0x52445443;
constexpr uint32_t Version = 2;
constexpr uint32_t SceneBytes = native_contract::SceneBytes;
constexpr uint32_t RecordCount = native_contract::RecordCount;
constexpr uint32_t RecordStride = native_contract::RecordStride;
constexpr uint32_t LightBytes = RecordCount * RecordStride;
constexpr uint32_t CounterBytes = native_contract::CounterBytes;
constexpr uint32_t MappingBytes = 256 + SceneBytes + LightBytes + CounterBytes;
enum class Status : uint32_t { Waiting, Active, Incompatible, Fault, LegacyConflict, Stopped };
enum Flags : uint32_t { ExactBuild = 1, FenceCompleted = 2, PairedScene = 4, PairedCounter = 8 };

// Little-endian inter-process ABI. Readers acquire seqlock before/after copying
// and reject odd or changed values. Do not expose std::atomic across the ABI.
struct alignas(8) Header
{
    uint32_t magic, version, headerBytes, totalBytes;
    volatile LONG64 seqlock;
    uint32_t pid;
    Status state;
    uint64_t processStartFileTime, sampleSequence, capturedTickMs, publishedTickMs;
    uint32_t frameNumber, sceneBytes, rawCount, stride, error, flags;
    uint64_t outputResource, counterResource, owner;
    uint32_t bufferIndex, counterBytes;
    uint8_t reserved[136];
};
struct Mapping
{
    Header header;
    uint8_t scene[SceneBytes];
    uint8_t lights[LightBytes];
    uint8_t counters[CounterBytes];
};
static_assert(sizeof(Header) == 256 && sizeof(Mapping) == MappingBytes);
static_assert(offsetof(Header, seqlock) == 16 && offsetof(Header, flags) == 84);
static_assert(offsetof(Header, outputResource) == 88 && offsetof(Header, counterBytes) == 116);
static_assert(offsetof(Mapping, scene) == 256 && offsetof(Mapping, lights) == 3072);
static_assert(offsetof(Mapping, counters) == 3072 + LightBytes);

bool OpenBridge();
void PublishStatus(Status state, uint32_t error = 0, uint32_t flags = 0);
void PublishSample(const void* scene, const void* lights, const void* counters, uint64_t capturedTickMs,
    uint64_t outputResource, uint64_t counterResource, uint64_t owner, uint32_t bufferIndex);
// Validation independent of game pointers, also used by the smoke test.
bool ValidateScene(const void* scene);
bool SameScene(const void* first, const void* second);
}
