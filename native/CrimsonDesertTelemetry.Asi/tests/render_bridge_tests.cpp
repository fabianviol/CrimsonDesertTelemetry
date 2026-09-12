#include "render_bridge.h"
#include "sdf_visibility.h"
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
void Check(bool ok, const char* what) { if (!ok) { std::cerr << what << '\n'; ExitProcess(1); } }
template<size_t N, class T> void Put(std::array<uint8_t, N>& scene, size_t at, T value)
{ memcpy(scene.data() + at, &value, sizeof(value)); }

std::array<uint8_t, 768> GiConstants()
{
    std::array<uint8_t, 768> gi{};
    Put(gi, 0x10, std::array<float, 3>{1.f / 32, 1.f / 16, 1.f / 32});
    for (unsigned level = 0; level < 8; ++level)
        Put(gi, 0x14c + level * 16, 4.f / static_cast<float>(1u << level));
    return gi;
}
std::vector<uint8_t> Field(uint16_t half)
{
    std::vector<uint8_t> bytes(size_t{128} * 64 * 1040 * 2);
    for (size_t i = 0; i < bytes.size(); i += 2)
    { bytes[i] = static_cast<uint8_t>(half); bytes[i + 1] = static_cast<uint8_t>(half >> 8); }
    return bytes;
}
void Light(std::vector<uint8_t>& bytes, unsigned index, const std::array<float, 3>& relative)
{
    auto* record = bytes.data() + size_t{index} * cdt::render::RecordStride;
    const float marker = 3.14159265f;
    const std::array<float, 3> rgb{.125f, .25f, .5f};
    const uint16_t pointKind = 0xbc00; // binary16 -1
    memcpy(record, relative.data(), sizeof(relative));
    memcpy(record + 12, &marker, sizeof(marker));
    memcpy(record + 16, rgb.data(), sizeof(rgb));
    memcpy(record + 38, &pointKind, sizeof(pointKind));
}

void VisibilityControls(const cdt::render::Mapping* source,
    std::array<uint8_t, cdt::render::SceneBytes> scene)
{
    using namespace cdt::render;
    namespace sdf = cdt::sdf;
    const auto gi = GiConstants();
    Put(scene, 0x80, std::array<float, 4>{0, 0, 0, 0});
    Put(scene, 0x90, std::array<float, 4>{0, 0, 1, 0});
    std::vector<uint8_t> lights(LightBytes);
    std::array<uint8_t, CounterBytes> counters{};
    Light(lights, 0, {0, 0, -5}); // behind the capture camera looking along +Z
    Light(lights, 1, {0, 0, 5});
    Light(lights, 2, {101, 0, 0});
    Light(lights, 3, {std::numeric_limits<float>::quiet_NaN(), 0, 0});
    Light(lights, 4, {0, 0, -6}); // retained capacity tail, outside this capture's count
    Put(counters, 4, uint32_t{4});
    lights.back() = 0x5a;
    uint32_t frame = 120;
    auto publish = [&]
    {
        Put(scene, 0x20, ++frame);
        PublishSample(scene.data(), lights.data(), counters.data(), GetTickCount64(),
            4000 + frame, 5000 + frame, 6000, frame % 2);
        Check(!(source->header.seqlock & 1) && source->header.frameNumber == frame,
            "visibility publication did not finish its capture seqlock");
        Check(memcmp(source->scene, scene.data(), SceneBytes) == 0 &&
            memcmp(source->lights, lights.data(), LightBytes) == 0 &&
            memcmp(source->counters, counters.data(), CounterBytes) == 0,
            "source visibility altered raw scene, RGB/record bytes or counters");
    };
    auto field = [&](uint16_t value, uint32_t contextFrame, uint64_t tick, bool bracketed)
    {
        // This stored camera is outside every clipmap. TraceBatch must use the
        // paired light-capture camera at zero, not the SDF snapshot's old camera.
        sdf::Publish(Field(value), gi, {5000, 0, 0}, contextFrame, tick, bracketed);
    };

    SetSourceVisibilityEnabled(true);
    field(0x3c00, 777, GetTickCount64(), true);
    const auto clearStatus = sdf::CurrentStatus(GetTickCount64());
    publish();
    Check(source->header.version == 3 && source->header.visibilityVersion == 1 &&
        source->header.visibilityEntryBytes == sizeof(VisibilityEntry) &&
        source->header.visibilityMaximumAgeMs == sdf::ProductionMaximumAgeMilliseconds &&
        source->header.visibilityTraceBudget == sdf::MaximumBatchTargets,
        "v3 additive visibility ABI or budgets mismatch");
    Check(source->header.visibilityVolumeSequence == clearStatus.sequence &&
        source->header.visibilityVolumeTickMs == clearStatus.capturedTickMilliseconds &&
        source->header.visibilityContextFrame == 777 && source->header.frameNumber != 777,
        "light-capture frame and SDF context were conflated");
    Check(source->visibility[0].code == 1 && source->visibility[1].code == 1 &&
        source->visibility[0].closest == 1 && source->visibility[1].closest == 1,
        "camera-paired clear trace failed for a behind-camera target");
    Check(source->visibility[2].code == 8 && source->visibility[3].code == 6 &&
        source->visibility[4].code == 0,
        "range/invalid metadata or counter-prefix isolation failed");

    SetSourceVisibilityEnabled(false);
    publish();
    Check(source->visibility[0].code == 11 && source->visibility[RecordCount - 1].code == 11 &&
        source->header.visibilityVolumeSequence == 0 && source->header.visibilityVolumeTickMs == 0 &&
        source->header.visibilityContextFrame == 0, "disabled source visibility retained a prior verdict");
    SetSourceVisibilityEnabled(true);
    field(0xbc00, 778, GetTickCount64(), true);
    publish();
    Check(source->visibility[0].code == 2 && source->visibility[1].code == 2 &&
        source->visibility[0].closest == -1 && source->visibility[1].closest == -1,
        "negative SDF failed to block front and behind-camera targets");

    static_assert(RecordCount > sdf::MaximumBatchTargets + 2);
    lights.assign(LightBytes, 0);
    for (unsigned index = 0; index < sdf::MaximumBatchTargets + 2; ++index)
        Light(lights, index, {0, 0, -5});
    Put(counters, 4, uint32_t{sdf::MaximumBatchTargets + 2});
    field(0x3c00, 779, GetTickCount64(), true);
    publish();
    for (unsigned index = 0; index < sdf::MaximumBatchTargets; ++index)
        Check(source->visibility[index].code == 1, "trace budget failed to retain its nearest deterministic prefix");
    Check(source->visibility[sdf::MaximumBatchTargets].code == 7 &&
        source->visibility[sdf::MaximumBatchTargets + 1].code == 7,
        "overflow targets were given a geometric verdict instead of budget-unknown");

    Put(counters, 4, uint32_t{2});
    const auto staleTick = GetTickCount64() - sdf::ProductionMaximumAgeMilliseconds - 100;
    field(0x3c00, 780, staleTick, true);
    const auto staleStatus = sdf::CurrentStatus(GetTickCount64());
    publish();
    Check(source->visibility[0].code == 9 && source->visibility[1].code == 9 &&
        source->header.visibilityVolumeTickMs == staleTick &&
        source->header.visibilityVolumeSequence == staleStatus.sequence,
        "stale SDF was published as fresh/clear or lost its original source age");
    field(0x3c00, 781, GetTickCount64(), false);
    publish();
    Check(source->visibility[0].code == 10 && source->visibility[1].code == 10,
        "unbracketed field produced source visibility");
    sdf::Clear();
    publish();
    Check(source->visibility[0].code == 0 && source->header.visibilityVolumeSequence == 0,
        "missing field retained previous source metadata");

    // A reader must accept either complete capture, including the matching SDF
    // sequence and verdicts, while the producer alternates geometric evidence.
    const auto sequenceBase = source->header.sampleSequence;
    field(0x3c00, 900, GetTickCount64(), true);
    const auto volumeSequenceBase = sdf::CurrentStatus(GetTickCount64()).sequence;
    std::atomic<bool> done{};
    std::thread writer([&]
    {
        auto writerScene = scene;
        std::vector<uint8_t> writerLights(LightBytes);
        std::array<uint8_t, CounterBytes> writerCounters{};
        Put(writerCounters, 4, uint32_t{1});
        for (uint32_t n = 1; n <= 12; ++n)
        {
            field(static_cast<uint16_t>(n % 2 ? 0x3c00 : 0xbc00), 2000 + n, GetTickCount64(), true);
            Put(writerScene, 0x20, 1000 + n);
            Light(writerLights, 0, {static_cast<float>(n), 0, -5});
            writerLights.back() = static_cast<uint8_t>(n);
            PublishSample(writerScene.data(), writerLights.data(), writerCounters.data(), GetTickCount64(),
                4000 + n, 5000 + n, 6000, n % 2);
        }
        done = true;
    });
    auto copy = std::make_unique<Mapping>();
    unsigned accepted{};
    while (!done || !accepted)
    {
        const LONG64 before = source->header.seqlock;
        MemoryBarrier();
        if (before & 1) continue;
        memcpy(copy.get(), source, MappingBytes);
        MemoryBarrier();
        if (before != source->header.seqlock || copy->header.frameNumber < 1001) continue;
        const auto n = copy->header.frameNumber - 1000;
        uint32_t sceneFrame{}, count{}; float relativeX{};
        memcpy(&sceneFrame, copy->scene + 0x20, sizeof(sceneFrame));
        memcpy(&count, copy->counters + 4, sizeof(count));
        memcpy(&relativeX, copy->lights, sizeof(relativeX));
        Check(n >= 1 && n <= 12 && sceneFrame == copy->header.frameNumber && count == 1 &&
            relativeX == static_cast<float>(n) && copy->lights[LightBytes - 1] == n &&
            copy->header.sampleSequence == sequenceBase + n && copy->header.outputResource == 4000 + n &&
            copy->header.counterResource == 5000 + n, "v3 seqlock accepted torn raw capture fields");
        Check(copy->header.visibilityVolumeSequence == volumeSequenceBase + n &&
            copy->header.visibilityContextFrame == 2000 + n &&
            copy->header.visibilityVolumeTickMs <= copy->header.publishedTickMs &&
            copy->visibility[0].code == (n % 2 ? 1u : 2u) &&
            copy->visibility[0].closest == (n % 2 ? 1.f : -1.f) && copy->visibility[1].code == 0,
            "v3 seqlock accepted visibility from a different capture or SDF snapshot");
        ++accepted;
    }
    writer.join();
    SetSourceVisibilityEnabled(false);
    sdf::Clear();
}
}
int main()
{
    using namespace cdt::render;
    std::array<uint8_t, SceneBytes> scene{};
    Put(scene, 0xAC0, 6360000.0f);
    Put(scene, 0x30, std::array<float,4>{3840,2160,1.0f/3840,1.0f/2160});
    Put(scene, 0x80, std::array<float,4>{-10528,611,-4354,0});
    Put(scene, 0x90, std::array<float,4>{0,0,1,0});
    Check(ValidateScene(scene.data()), "valid scene rejected");
    auto bad = scene; Put(bad, 0xAC0, 0.0f);
    Check(!ValidateScene(bad.data()), "unvalidated scene accepted");
    bad = scene; Put(bad, 0x38, 1.0f);
    Check(!ValidateScene(bad.data()), "wrong reciprocal accepted");
    bad = scene; Put(bad, 0x20, uint32_t{2});
    Check(!SameScene(scene.data(), bad.data()), "mixed frame accepted");
    bad = scene; Put(bad, 0x80, 19.0f);
    Check(!SameScene(scene.data(), bad.data()), "mixed camera accepted");
    Check(OpenBridge(), "mapping create failed");
    const auto name = L"Local\\CrimsonDesertTelemetry.Render." + std::to_wstring(GetCurrentProcessId());
    HANDLE handle = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
    Check(handle != nullptr, "mapping read open failed");
    const auto* source = static_cast<const Mapping*>(MapViewOfFile(handle, FILE_MAP_READ, 0, 0, MappingBytes));
    Check(source && source->header.magic == Magic && source->header.totalBytes == MappingBytes, "ABI mismatch");
    std::atomic<bool> done{};
    std::thread writer([&]
    {
        std::vector<uint8_t> lights(LightBytes);
        std::array<uint8_t, CounterBytes> counters{};
        for (uint32_t n = 1; n <= 120; ++n)
        {
            Put(scene, 0x20, n);
            memset(lights.data(), static_cast<int>(n), lights.size());
            counters.fill(static_cast<uint8_t>(n));
            PublishSample(scene.data(), lights.data(), counters.data(), GetTickCount64(), n + 1000, n + 2000, 3000, n % 2);
        }
        done = true;
    });
    auto copy = std::make_unique<Mapping>();
    unsigned accepted = 0;
    while (!done || !accepted)
    {
        const LONG64 before = source->header.seqlock;
        MemoryBarrier();
        if (before & 1) continue;
        memcpy(copy.get(), source, MappingBytes);
        MemoryBarrier();
        if (before != source->header.seqlock || copy->header.state != Status::Active) continue;
        const auto frame = copy->header.frameNumber;
        uint32_t sceneFrame{}; memcpy(&sceneFrame, copy->scene + 0x20, 4);
        Check(frame == sceneFrame && copy->lights[0] == frame && copy->lights[LightBytes-1] == frame,
            "seqlock accepted torn camera/light pair");
        Check(copy->header.version == 3 && copy->header.flags == 15 && copy->header.sampleSequence == frame,
            "sample metadata mismatch");
        Check(copy->visibility[0].code == 11 && copy->visibility[RecordCount - 1].code == 11 &&
            copy->header.visibilityVolumeSequence == 0, "disabled metadata disturbed raw concurrent publication");
        Check(copy->header.counterBytes == CounterBytes && copy->counters[0] == frame &&
            copy->counters[CounterBytes-1] == frame && copy->header.outputResource == frame + 1000 &&
            copy->header.counterResource == frame + 2000 && copy->header.owner == 3000 &&
            copy->header.bufferIndex == frame % 2, "seqlock accepted torn light/counter resource pair");
        ++accepted;
    }
    writer.join();
    VisibilityControls(source, scene);
    PublishStatus(Status::Fault, WAIT_TIMEOUT, ExactBuild);
    Check(source->header.state == Status::Fault && source->header.error == WAIT_TIMEOUT && !(source->header.seqlock & 1),
        "fault failed to invalidate old sample");
    PublishStatus(Status::Stopped);
    Check(source->header.state == Status::Stopped && source->header.flags == 0, "stop failed to invalidate sample");
    UnmapViewOfFile(source); CloseHandle(handle);
    std::cout << "render bridge validation, mixed-frame rejection, concurrent publication and stale-state invalidation passed ("
        << accepted << " coherent reads)\n";
}
