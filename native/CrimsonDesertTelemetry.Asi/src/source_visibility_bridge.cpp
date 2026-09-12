#include "source_visibility_bridge.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace cdt::source_visibility
{
namespace
{
constexpr std::uint64_t MaximumQueryAgeMilliseconds = 1500;
HANDLE resultHandle{}, queryHandle{};
ResultMapping* resultMapping{};
const QueryMapping* queryMapping{};
std::uint64_t processStartFileTime{}, lastVolumeSequence{};
SRWLOCK resultLock = SRWLOCK_INIT;

bool Finite(const std::array<float, 3>& value)
{
    return std::all_of(value.begin(), value.end(), [](float lane)
    { return std::isfinite(lane) && std::abs(lane) <= 1000000; });
}
void BeginWrite() { InterlockedIncrement64(&resultMapping->header.seqlock); MemoryBarrier(); }
void EndWrite() { MemoryBarrier(); InterlockedIncrement64(&resultMapping->header.seqlock); }
void DisconnectQuery() noexcept
{
    if (queryMapping) UnmapViewOfFile(queryMapping);
    if (queryHandle) CloseHandle(queryHandle);
    queryMapping = nullptr;
    queryHandle = nullptr;
}
bool ConnectQuery()
{
    if (queryMapping) return true;
    const auto name = L"Local\\CrimsonDesertTelemetry.VisibilityQuery." + std::to_wstring(GetCurrentProcessId());
    queryHandle = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
    if (!queryHandle) return false;
    queryMapping = static_cast<const QueryMapping*>(MapViewOfFile(queryHandle, FILE_MAP_READ, 0, 0, sizeof(QueryMapping)));
    if (!queryMapping) { CloseHandle(queryHandle); queryHandle = nullptr; return false; }
    return true;
}
bool ReadQuery(QueryMapping& copy, std::uint64_t now)
{
    if (!ConnectQuery()) return false;
    for (unsigned attempt = 0; attempt < 3; ++attempt)
    {
        const LONG64 before = queryMapping->header.seqlock;
        MemoryBarrier();
        if (before & 1) continue;
        std::memcpy(&copy, queryMapping, sizeof(copy));
        MemoryBarrier();
        if (before != queryMapping->header.seqlock || copy.header.seqlock != before) continue;
        const auto& header = copy.header;
        if (header.magic != QueryMagic || header.version != Version || header.headerBytes != HeaderBytes ||
            header.totalBytes != sizeof(QueryMapping) || header.pid != GetCurrentProcessId() ||
            header.processStartFileTime != processStartFileTime || header.targetCount > MaximumTargets ||
            !header.querySequence || !header.publishedTickMs || header.publishedTickMs > now ||
            now - header.publishedTickMs > MaximumQueryAgeMilliseconds || !Finite(header.receiver))
        {
            DisconnectQuery();
            return false;
        }
        for (std::uint32_t index = 0; index < header.targetCount; ++index)
            if (!Finite(copy.entries[index].position)) return false;
        return true;
    }
    return false;
}
std::uint32_t Code(const sdf::TraceResult& trace)
{
    if (!trace.available)
        return trace.reason == "stale-sdf-volume" ? 9u :
            trace.reason == "invalid-sdf-sample" ? 6u : 10u;
    switch (trace.verdict)
    {
    case sdf::Verdict::Clear: return 1;
    case sdf::Verdict::Blocked: return 2;
    case sdf::Verdict::Uncovered: return 3;
    case sdf::Verdict::TooShort: return 4;
    case sdf::Verdict::IterationBound: return 5;
    default: return 10;
    }
}
void Publish(const QueryMapping& query, const sdf::BatchResult& batch, std::uint64_t now)
{
    AcquireSRWLockExclusive(&resultLock);
    BeginWrite();
    auto& header = resultMapping->header;
    header.state = State::Active;
    header.querySequence = query.header.querySequence;
    header.publishedTickMs = now;
    header.requestTickMs = query.header.publishedTickMs;
    header.volumeSequence = batch.status.sequence;
    header.volumeTickMs = batch.status.capturedTickMilliseconds;
    header.contextFrame = batch.status.contextFrame;
    header.targetCount = query.header.targetCount;
    header.receiver = query.header.receiver;
    header.error = 0;
    for (std::uint32_t index = 0; index < header.targetCount; ++index)
    {
        auto& destination = resultMapping->entries[index];
        destination.id = query.entries[index].id;
        destination.position = query.entries[index].position;
        if (index < batch.traces.size())
        {
            destination.code = Code(batch.traces[index]);
            destination.closest = static_cast<float>(batch.traces[index].closest);
        }
        else { destination.code = Code(sdf::TraceResult{batch.status}); destination.closest = 0; }
    }
    std::fill(resultMapping->entries + header.targetCount,
        resultMapping->entries + MaximumTargets, ResultEntry{});
    EndWrite();
    ReleaseSRWLockExclusive(&resultLock);
}
}

bool Open()
{
    if (resultMapping) return false;
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return false;
    processStartFileTime = (std::uint64_t{created.dwHighDateTime} << 32) | created.dwLowDateTime;
    const auto name = L"Local\\CrimsonDesertTelemetry.VisibilityResult." + std::to_wstring(GetCurrentProcessId());
    resultHandle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
        sizeof(ResultMapping), name.c_str());
    if (!resultHandle || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (resultHandle) CloseHandle(resultHandle);
        resultHandle = nullptr;
        return false;
    }
    resultMapping = static_cast<ResultMapping*>(MapViewOfFile(resultHandle, FILE_MAP_WRITE, 0, 0, sizeof(ResultMapping)));
    if (!resultMapping) { CloseHandle(resultHandle); resultHandle = nullptr; return false; }
    BeginWrite();
    auto& header = resultMapping->header;
    header.magic = ResultMagic;
    header.version = Version;
    header.headerBytes = HeaderBytes;
    header.totalBytes = sizeof(ResultMapping);
    header.pid = GetCurrentProcessId();
    header.processStartFileTime = processStartFileTime;
    header.state = State::Waiting;
    EndWrite();
    return true;
}

void Poll()
{
    if (!resultMapping) return;
    const std::uint64_t now = GetTickCount64();
    QueryMapping query{};
    if (!ReadQuery(query, now)) return;
    const auto status = sdf::CurrentStatus(now);
    if (!status.sequence || status.sequence == lastVolumeSequence) return;
    std::vector<std::array<float, 3>> targets;
    targets.reserve(query.header.targetCount);
    for (std::uint32_t index = 0; index < query.header.targetCount; ++index)
        targets.push_back(query.entries[index].position);
    const auto batch = sdf::TraceBatch(query.header.receiver, targets, now);
    Publish(query, batch, now);
    lastVolumeSequence = status.sequence;
}

void Close() noexcept
{
    try
    {
        if (resultMapping)
        {
            AcquireSRWLockExclusive(&resultLock);
            BeginWrite();
            resultMapping->header.state = State::Stopped;
            resultMapping->header.publishedTickMs = GetTickCount64();
            EndWrite();
            ReleaseSRWLockExclusive(&resultLock);
        }
        DisconnectQuery();
        if (resultMapping) UnmapViewOfFile(resultMapping);
        if (resultHandle) CloseHandle(resultHandle);
        resultMapping = nullptr;
        resultHandle = nullptr;
        processStartFileTime = 0;
        lastVolumeSequence = 0;
    }
    catch (...) {}
}
}
