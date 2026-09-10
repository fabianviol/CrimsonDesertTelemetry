#include "render_bridge.h"
#include "sky_bridge.h"
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
    mapping->header.frameNumber = At<uint32_t>(scene, native_contract::FrameOffset);
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
    if (!scene || At<float>(scene, native_contract::EarthRadiusOffset) != native_contract::EarthRadius) return false;
    const auto screen = At<std::array<float, 4>>(scene, native_contract::ScreenOffset);
    if (!std::isfinite(screen[0]) || !std::isfinite(screen[1]) || screen[0] < 64 || screen[1] < 64 ||
        screen[0] > 32768 || screen[1] > 32768 || !std::isfinite(screen[2]) || !std::isfinite(screen[3]) ||
        std::fabs(screen[0] * screen[2] - 1.0f) > 0.002f || std::fabs(screen[1] * screen[3] - 1.0f) > 0.002f) return false;
    const auto position = At<std::array<float, 4>>(scene, native_contract::PositionOffset);
    const auto direction = At<std::array<float, 4>>(scene, native_contract::DirectionOffset);
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
    return At<uint32_t>(first, native_contract::FrameOffset) == At<uint32_t>(second, native_contract::FrameOffset) &&
        memcmp(static_cast<const uint8_t*>(first) + native_contract::PositionOffset,
            static_cast<const uint8_t*>(second) + native_contract::PositionOffset, 32) == 0 &&
        memcmp(static_cast<const uint8_t*>(first) + native_contract::ViewRelativeOffset,
            static_cast<const uint8_t*>(second) + native_contract::ViewRelativeOffset, 64) == 0;
}
}

namespace cdt::sky
{
namespace { HANDLE handle{}; Mapping* mapping{}; SRWLOCK lock = SRWLOCK_INIT;
void Begin() { InterlockedIncrement64(&mapping->header.seqlock); MemoryBarrier(); }
void End() { MemoryBarrier(); InterlockedIncrement64(&mapping->header.seqlock); }
}
bool OpenBridge()
{
    if (mapping) return false;
    const auto name = L"Local\\CrimsonDesertTelemetry.Sky." + std::to_wstring(GetCurrentProcessId());
    handle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Mapping), name.c_str());
    if (!handle) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(handle); handle = nullptr; return false; }
    mapping = static_cast<Mapping*>(MapViewOfFile(handle, FILE_MAP_WRITE, 0, 0, sizeof(Mapping)));
    if (!mapping) { CloseHandle(handle); handle = nullptr; return false; }
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
    { UnmapViewOfFile(mapping); mapping = nullptr; CloseHandle(handle); handle = nullptr; return false; }
    Begin();
    auto& h = mapping->header;
    h.magic = 0x53445443; h.version = Version; h.headerBytes = sizeof(Header); h.totalBytes = sizeof(Mapping);
    h.pid = GetCurrentProcessId(); h.state = render::Status::Stopped;
    h.processStartFileTime = (uint64_t{created.dwHighDateTime} << 32) | created.dwLowDateTime;
    h.sceneBytes = render::SceneBytes; h.payloadBytes = PayloadBytes;
    h.cameraSkyVisibilityWorking = 0.0; h.visibilityState = Visibility::Unavailable;
    h.visibilityFrameNumber = 0; h.visibilityTickMs = 0;
    End();
    return true;
}
void PublishStatus(render::Status state, uint32_t error)
{
    if (!mapping) return;
    AcquireSRWLockExclusive(&lock); Begin();
    mapping->header.state = state; mapping->header.error = error;
    mapping->header.flags = 0; mapping->header.publishedTickMs = GetTickCount64();
    End(); ReleaseSRWLockExclusive(&lock);
}
uint32_t FailureCode()
{
    if (!mapping) return ERROR_INVALID_HANDLE;
    AcquireSRWLockShared(&lock);
    const auto result = mapping->header.error;
    ReleaseSRWLockShared(&lock);
    return result;
}
void PublishSample(const void* scene, const void* data, uint64_t tick, uint64_t resource, uint32_t producerRva)
{
    if (!mapping || !render::ValidateScene(scene) || !data || !tick || !resource || producerRva != 0x3849BB7) return;
    AcquireSRWLockExclusive(&lock); Begin();
    memcpy(mapping->scene, scene, render::SceneBytes); memcpy(mapping->data, data, PayloadBytes);
    auto& h = mapping->header;
    ++h.sampleSequence; h.capturedTickMs = tick; h.publishedTickMs = GetTickCount64();
    memcpy(&h.frameNumber, mapping->scene + native_contract::FrameOffset, sizeof(h.frameNumber));
    h.resource = resource; h.producerRva = producerRva; h.error = 0;
    h.flags = render::ExactBuild | render::FenceCompleted | render::PairedScene;
    h.state = render::Status::Active;
    End(); ReleaseSRWLockExclusive(&lock);
}
void PublishVisibility(double value, Visibility state, uint32_t frameNumber, uint64_t tickMs)
{
    // A different subsystem at a different rate, so this deliberately leaves
    // state, sampleSequence, flags and the sky frame alone. It only borrows the
    // lock and seqlock so a reader cannot observe a half-written block.
    if (!mapping) return;
    if (state != Visibility::Valid) { value = 0.0; frameNumber = 0; tickMs = 0; }
    AcquireSRWLockExclusive(&lock); Begin();
    auto& h = mapping->header;
    h.cameraSkyVisibilityWorking = value; h.visibilityState = state;
    h.visibilityFrameNumber = frameNumber; h.visibilityTickMs = tickMs;
    End(); ReleaseSRWLockExclusive(&lock);
}
}
