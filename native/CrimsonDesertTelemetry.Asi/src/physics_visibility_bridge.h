#pragma once
#include "physics_query.h"
#include <windows.h>
#include <string>

namespace cdt::physics
{
// Separate from the retained SDF exchange. Both directions are 128-byte seqlocks.
struct alignas(8) VisibilityPacket
{
    std::uint32_t magic{}, version{1}, bytes{128}, state{};
    volatile LONG64 seqlock{};
    std::uint32_t pid{}, code{}; // result: 1 complete, 2 unknown, 3 latched fault
    std::uint64_t processStart{}, sequence{}, issued{}, completed{}, lightSequence{};
    std::uint32_t frame{}, samples{}, clear{};
    float sourceAge{};
    Vec3 player{}, camera{}, target{};
    std::uint32_t reserved{};
};
static_assert(sizeof(VisibilityPacket) == 128);
static_assert(offsetof(VisibilityPacket, seqlock) == 16 && offsetof(VisibilityPacket, camera) == 100);
inline constexpr std::uint32_t VisibilityQueryMagic = 0x50564443, VisibilityResultMagic = 0x53564443;
inline bool ValidVisibilityQuery(const VisibilityPacket& p, DWORD pid, std::uint64_t born, std::uint64_t now)
{
    return p.magic == VisibilityQueryMagic && p.version == 1 && p.bytes == 128 &&
        p.pid == pid && p.processStart == born && p.sequence && p.lightSequence &&
        p.samples == 0 && p.clear == 0 && p.code == 0 && p.completed == 0 &&
        p.issued && p.issued <= now && now - p.issued <= 250 &&
        std::isfinite(p.sourceAge) && p.sourceAge >= 0 && p.sourceAge <= 250 &&
        p.sourceAge + static_cast<double>(now - p.issued) <= 500 &&
        Finite(p.player) && Finite(p.camera) && Finite(p.target) &&
        Distance(p.player, p.camera) <= 12 && Distance(p.camera, p.target) <= 35;
}
class VisibilityBridge
{
    HANDLE queryHandle_{}, resultHandle_{};
    const VisibilityPacket* query_{};
    VisibilityPacket* result_{};
    std::uint64_t born_{};
public:
    bool Open(std::uint64_t born)
    {
        born_ = born;
        const auto name = L"Local\\CrimsonDesertTelemetry.PhysicsVisibilityResult." + std::to_wstring(GetCurrentProcessId());
        resultHandle_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 128, name.c_str());
        if (!resultHandle_) return false;
        if (GetLastError() == ERROR_ALREADY_EXISTS) { Close(); return false; }
        result_ = static_cast<VisibilityPacket*>(MapViewOfFile(resultHandle_, FILE_MAP_WRITE, 0, 0, 128));
        if (!result_) { Close(); return false; }
        VisibilityPacket initial{}; initial.pid = GetCurrentProcessId(); initial.processStart = born;
        Publish(initial); return true;
    }
    bool Read(VisibilityPacket& p, std::uint64_t now)
    {
        if (!result_) return false;
        if (!query_)
        {
            const auto name = L"Local\\CrimsonDesertTelemetry.PhysicsVisibilityQuery." + std::to_wstring(GetCurrentProcessId());
            queryHandle_ = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
            if (!queryHandle_) return false;
            query_ = static_cast<const VisibilityPacket*>(MapViewOfFile(queryHandle_, FILE_MAP_READ, 0, 0, 128));
            if (!query_) { CloseHandle(queryHandle_); queryHandle_ = nullptr; return false; }
        }
        for (unsigned attempt = 0; attempt < 3; ++attempt)
        {
            const auto seq = query_->seqlock;
            MemoryBarrier(); if (seq & 1) continue;
            std::memcpy(&p, query_, sizeof p); MemoryBarrier();
            if (seq == query_->seqlock && p.seqlock == seq)
                return ValidVisibilityQuery(p, GetCurrentProcessId(), born_, now);
        }
        return false;
    }
    void Publish(VisibilityPacket p)
    {
        if (!result_) return;
        p.magic = VisibilityResultMagic;
        InterlockedIncrement64(&result_->seqlock); MemoryBarrier();
        // Never overwrite the live seqlock with the request's value.
        std::memcpy(result_, &p, 16);
        std::memcpy(reinterpret_cast<char*>(result_) + 24, reinterpret_cast<char*>(&p) + 24, 104);
        MemoryBarrier(); InterlockedIncrement64(&result_->seqlock);
    }
    void Close()
    {
        if (result_) UnmapViewOfFile(result_);
        if (resultHandle_) CloseHandle(resultHandle_);
        if (query_) UnmapViewOfFile(query_);
        if (queryHandle_) CloseHandle(queryHandle_);
        result_ = nullptr; query_ = nullptr; resultHandle_ = queryHandle_ = nullptr;
    }
};
}
