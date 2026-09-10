#pragma once
#include "render_bridge.h"

namespace cdt::sky
{
constexpr uint32_t PayloadBytes = 1024;
constexpr uint32_t Version = 2;   // 1 carried no visibility block; readers reject a mismatch.

// Sky and camera visibility come from DIFFERENT subsystems at different rates,
// so each carries its own frame, tick and state. Never derive one's freshness
// from the other's.
enum class Visibility : uint32_t
{
    Unavailable = 0,   // no sample yet, or the run has ended
    Valid = 1,         // sampled from the voxel texture
    Fallback = 2,      // the shader's fallback branch, where the value is 1 BY
                       // DEFINITION and is not a measurement. Never treat this
                       // as Valid: it would restore full ambient exactly where
                       // the local measurement is missing, which is indoors.
};

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
    double cameraSkyVisibilityWorking;   // saturate(1 - sampled), raw and unnormalised
    Visibility visibilityState;
    uint32_t visibilityFrameNumber;
    uint64_t visibilityTickMs;           // GetTickCount64, the base the reader also uses
    uint8_t reserved[8];
};
struct Mapping { Header header; uint8_t scene[render::SceneBytes]; uint8_t data[PayloadBytes]; };
static_assert(sizeof(Header) == 128 && offsetof(Header, seqlock) == 16);
static_assert(sizeof(Mapping) == 128 + render::SceneBytes + PayloadBytes);
// The managed reader indexes these by number; a silent shift would misread them.
static_assert(offsetof(Header, cameraSkyVisibilityWorking) == 96);
static_assert(offsetof(Header, visibilityState) == 104);
static_assert(offsetof(Header, visibilityFrameNumber) == 108);
static_assert(offsetof(Header, visibilityTickMs) == 112);
bool OpenBridge();
uint32_t FailureCode();
void PublishStatus(render::Status state, uint32_t error = 0);
void PublishSample(const void* scene, const void* data, uint64_t tick, uint64_t resource, uint32_t producerRva);
// Takes the same lock and seqlock as PublishSample and touches ONLY the
// visibility block, so readers still see consistent snapshots. SRW exclusive is
// not recursive: never call this from a path already holding that lock.
void PublishVisibility(double value, Visibility state, uint32_t frameNumber, uint64_t tickMs);
}
