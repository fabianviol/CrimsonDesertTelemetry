#include "render_bridge.h"
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
void Check(bool ok, const char* what) { if (!ok) { std::cerr << what << '\n'; ExitProcess(1); } }
template<class T> void Put(std::array<uint8_t, cdt::render::SceneBytes>& scene, size_t at, T value)
{ memcpy(scene.data() + at, &value, sizeof(value)); }
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
        Check(copy->header.version == 2 && copy->header.flags == 15 && copy->header.sampleSequence == frame,
            "sample metadata mismatch");
        Check(copy->header.counterBytes == CounterBytes && copy->counters[0] == frame &&
            copy->counters[CounterBytes-1] == frame && copy->header.outputResource == frame + 1000 &&
            copy->header.counterResource == frame + 2000 && copy->header.owner == 3000 &&
            copy->header.bufferIndex == frame % 2, "seqlock accepted torn light/counter resource pair");
        ++accepted;
    }
    writer.join();
    PublishStatus(Status::Fault, WAIT_TIMEOUT, ExactBuild);
    Check(source->header.state == Status::Fault && source->header.error == WAIT_TIMEOUT && !(source->header.seqlock & 1),
        "fault failed to invalidate old sample");
    PublishStatus(Status::Stopped);
    Check(source->header.state == Status::Stopped && source->header.flags == 0, "stop failed to invalidate sample");
    UnmapViewOfFile(source); CloseHandle(handle);
    std::cout << "render bridge validation, mixed-frame rejection, concurrent publication and stale-state invalidation passed ("
        << accepted << " coherent reads)\n";
}
