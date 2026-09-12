#include "source_visibility_bridge.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class T> void Put(std::array<std::uint8_t, 768>& bytes, size_t offset, T value)
{ std::memcpy(bytes.data() + offset, &value, sizeof(value)); }
std::array<std::uint8_t, 768> Constants()
{
    std::array<std::uint8_t, 768> result{};
    Put(result, 0x10, std::array<float, 3>{1.f / 32, 1.f / 16, 1.f / 32});
    for (unsigned level = 0; level < 8; ++level) Put(result, 0x14c + 16 * level, 1.f);
    return result;
}
std::vector<std::uint8_t> Field(std::uint16_t half)
{
    std::vector<std::uint8_t> result(size_t{128} * 64 * 1040 * 2);
    for (size_t index = 0; index < result.size(); index += 2)
    { result[index] = static_cast<std::uint8_t>(half); result[index + 1] = static_cast<std::uint8_t>(half >> 8); }
    return result;
}
std::uint64_t ProcessStart()
{
    FILETIME created{}, exited{}, kernel{}, user{};
    Check(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != FALSE,
        "process start time unavailable");
    return (std::uint64_t{created.dwHighDateTime} << 32) | created.dwLowDateTime;
}
void WriteQuery(cdt::source_visibility::QueryMapping* query, std::uint64_t sequence)
{
    using namespace cdt::source_visibility;
    InterlockedIncrement64(&query->header.seqlock); MemoryBarrier();
    query->header.magic = QueryMagic; query->header.version = Version;
    query->header.headerBytes = HeaderBytes; query->header.totalBytes = sizeof(QueryMapping);
    query->header.pid = GetCurrentProcessId(); query->header.targetCount = 2;
    query->header.processStartFileTime = ProcessStart(); query->header.querySequence = sequence;
    query->header.publishedTickMs = GetTickCount64(); query->header.receiver = {0, 1, 0};
    query->entries[0] = {11, {0, 1, 5}};
    query->entries[1] = {12, {0, 1, -5}};
    MemoryBarrier(); InterlockedIncrement64(&query->header.seqlock);
}
}

int main()
{
    using namespace cdt::source_visibility;
    try
    {
        const auto queryName = L"Local\\CrimsonDesertTelemetry.VisibilityQuery." +
            std::to_wstring(GetCurrentProcessId());
        HANDLE queryHandle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
            sizeof(QueryMapping), queryName.c_str());
        Check(queryHandle != nullptr && GetLastError() != ERROR_ALREADY_EXISTS, "query mapping create failed");
        auto* query = static_cast<QueryMapping*>(MapViewOfFile(queryHandle, FILE_MAP_WRITE, 0, 0, sizeof(QueryMapping)));
        Check(query != nullptr, "query mapping view failed");
        WriteQuery(query, 1);
        Check(Open(), "result bridge create failed");
        const auto resultName = L"Local\\CrimsonDesertTelemetry.VisibilityResult." +
            std::to_wstring(GetCurrentProcessId());
        HANDLE resultHandle = OpenFileMappingW(FILE_MAP_READ, FALSE, resultName.c_str());
        Check(resultHandle != nullptr, "result mapping read open failed");
        const auto* result = static_cast<const ResultMapping*>(
            MapViewOfFile(resultHandle, FILE_MAP_READ, 0, 0, sizeof(ResultMapping)));
        Check(result != nullptr, "result mapping view failed");

        cdt::sdf::Publish(Field(0x3c00), Constants(), {9000, 9000, 9000}, 50, GetTickCount64(), true);
        Poll();
        Check(!(result->header.seqlock & 1) && result->header.state == State::Active &&
            result->header.querySequence == 1 && result->header.targetCount == 2 &&
            result->header.receiver == std::array<float, 3>{0, 1, 0},
            "clear result header did not preserve the exact player query");
        Check(result->entries[0].id == 11 && result->entries[1].id == 12 &&
            result->entries[0].code == 1 && result->entries[1].code == 1,
            "front/behind clear sources did not both pass without a camera direction");

        WriteQuery(query, 2);
        cdt::sdf::Publish(Field(0xbc00), Constants(), {9000, 9000, 9000}, 51, GetTickCount64(), true);
        Poll();
        Check(result->header.querySequence == 2 && result->header.volumeSequence != 0 &&
            result->entries[0].code == 2 && result->entries[1].code == 2 &&
            result->entries[0].closest == -1 && result->entries[1].closest == -1,
            "new SDF volume did not atomically update both binary source verdicts");

        Close(); cdt::sdf::Clear();
        UnmapViewOfFile(result); CloseHandle(resultHandle);
        UnmapViewOfFile(query); CloseHandle(queryHandle);
        std::cout << "PASS player-to-known-source query/result bridge and direction-independent tracing\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        Close(); cdt::sdf::Clear();
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
