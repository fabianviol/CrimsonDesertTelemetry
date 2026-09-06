#include "render_bridge.h"
#include <array>
#include <cmath>
#include <cstring>
#include <string>

namespace cdt::render
{
namespace
{
HANDLE mappingHandle{};
Mapping* mapping{};
SRWLOCK publishLock = SRWLOCK_INIT;
template<class T> T At(const void* bytes, size_t offset)
{
    T value{};
    memcpy(&value, static_cast<const uint8_t*>(bytes) + offset, sizeof(value));
    return value;
}
void BeginWrite() { InterlockedIncrement64(&mapping->header.seqlock); MemoryBarrier(); }
void EndWrite() { MemoryBarrier(); InterlockedIncrement64(&mapping->header.seqlock); }
}

bool OpenBridge()
{
    const auto name = L"Local\\CrimsonDesertTelemetry.Render." + std::to_wstring(GetCurrentProcessId());
    mappingHandle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, MappingBytes, name.c_str());
    if (!mappingHandle) return false;
    // Never take over another producer's mapping, including duplicate ASI copies.
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mappingHandle); mappingHandle = nullptr; return false; }
    mapping = static_cast<Mapping*>(MapViewOfFile(mappingHandle, FILE_MAP_WRITE, 0, 0, MappingBytes));
    if (!mapping) { CloseHandle(mappingHandle); mappingHandle = nullptr; return false; }
    FILETIME created{}, exited{}, kernel{}, user{};
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    BeginWrite();
    mapping->header.magic = Magic;
    mapping->header.version = Version;
    mapping->header.headerBytes = sizeof(Header);
    mapping->header.totalBytes = MappingBytes;
    mapping->header.pid = GetCurrentProcessId();
    mapping->header.processStartFileTime = (static_cast<uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    mapping->header.sceneBytes = SceneBytes;
    mapping->header.rawCount = RecordCount;
    mapping->header.stride = RecordStride;
    mapping->header.counterBytes = CounterBytes;
    mapping->header.state = Status::Waiting;
    EndWrite();
    return true;
}

void PublishStatus(Status state, uint32_t error, uint32_t flags)
{
    if (!mapping) return;
    AcquireSRWLockExclusive(&publishLock);
    BeginWrite();
    mapping->header.state = state;
    mapping->header.error = error;
    mapping->header.flags = flags;
    mapping->header.publishedTickMs = GetTickCount64();
    EndWrite();
    ReleaseSRWLockExclusive(&publishLock);
}

void PublishSample(const void* scene, const void* lights, const void* counters, uint64_t capturedTickMs,
    uint64_t outputResource, uint64_t counterResource, uint64_t owner, uint32_t bufferIndex)
{
    if (!mapping) return;
    AcquireSRWLockExclusive(&publishLock);
    BeginWrite();
    memcpy(mapping->scene, scene, SceneBytes);
    memcpy(mapping->lights, lights, LightBytes);
    memcpy(mapping->counters, counters, CounterBytes);
    ++mapping->header.sampleSequence;
    mapping->header.capturedTickMs = capturedTickMs;
    mapping->header.publishedTickMs = GetTickCount64();
    mapping->header.frameNumber = At<uint32_t>(scene, 0x20);
    mapping->header.error = 0;
    mapping->header.flags = ExactBuild | FenceCompleted | PairedScene | PairedCounter;
    mapping->header.outputResource = outputResource;
    mapping->header.counterResource = counterResource;
    mapping->header.owner = owner;
    mapping->header.bufferIndex = bufferIndex;
    mapping->header.state = Status::Active;
    EndWrite();
    ReleaseSRWLockExclusive(&publishLock);
}

bool ValidateScene(const void* scene)
{
    if (!scene || At<float>(scene, 0xAC0) != 6360000.0f) return false;
    const auto screen = At<std::array<float, 4>>(scene, 0x30);
    if (!std::isfinite(screen[0]) || !std::isfinite(screen[1]) || screen[0] < 64 || screen[1] < 64 ||
        screen[0] > 32768 || screen[1] > 32768 || !std::isfinite(screen[2]) || !std::isfinite(screen[3]) ||
        std::fabs(screen[0] * screen[2] - 1.0f) > 0.002f || std::fabs(screen[1] * screen[3] - 1.0f) > 0.002f) return false;
    const auto position = At<std::array<float, 4>>(scene, 0x80);
    const auto direction = At<std::array<float, 4>>(scene, 0x90);
    float length = 0;
    for (size_t i = 0; i < 3; ++i)
    {
        if (!std::isfinite(position[i]) || std::fabs(position[i]) > 1000000 || !std::isfinite(direction[i])) return false;
        length += direction[i] * direction[i];
    }
    return length > 0.95f && length < 1.05f;
}

bool SameScene(const void* first, const void* second)
{
    return At<uint32_t>(first, 0x20) == At<uint32_t>(second, 0x20) &&
        memcmp(static_cast<const uint8_t*>(first) + 0x80, static_cast<const uint8_t*>(second) + 0x80, 32) == 0 &&
        memcmp(static_cast<const uint8_t*>(first) + 0xA0, static_cast<const uint8_t*>(second) + 0xA0, 64) == 0;
}
}
