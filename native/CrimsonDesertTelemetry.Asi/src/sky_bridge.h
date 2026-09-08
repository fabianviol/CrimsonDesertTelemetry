#pragma once
#include "render_bridge.h"

namespace cdt::sky
{
constexpr uint32_t PayloadBytes = 1024;
// Independent ABI: ambient can never be decoded as a ManyLights sample.
struct alignas(8) Header
{
    uint32_t magic, version, headerBytes, totalBytes;
    volatile LONG64 seqlock;
    uint32_t pid;
    render::Status state;
    uint64_t processStartFileTime, sampleSequence, capturedTickMs, publishedTickMs;
    uint32_t frameNumber, sceneBytes, payloadBytes, producerRva, error, flags;
    uint64_t resource;
    uint8_t reserved[32];
};
struct Mapping { Header header; uint8_t scene[render::SceneBytes]; uint8_t data[PayloadBytes]; };
static_assert(sizeof(Header) == 128 && offsetof(Header, seqlock) == 16);
static_assert(sizeof(Mapping) == 128 + render::SceneBytes + PayloadBytes);
bool OpenBridge();
uint32_t FailureCode();
void PublishStatus(render::Status state, uint32_t error = 0);
void PublishSample(const void* scene, const void* data, uint64_t tick, uint64_t resource, uint32_t producerRva);
}
