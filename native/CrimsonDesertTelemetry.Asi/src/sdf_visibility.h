#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cdt::sdf
{
enum class Verdict { Unavailable, Clear, Blocked, Uncovered, TooShort, IterationBound };

struct Status
{
    bool available{};
    std::uint64_t sequence{}, ageMilliseconds{};
    std::uint32_t contextFrame{};
    bool cpuContextBracketed{};
    std::string reason = "waiting-for-sdf-volume";
};

struct TraceResult : Status
{
    Verdict verdict = Verdict::Unavailable;
    double length{}, at{}, value{}, closest{}, closestAt{};
    unsigned level{}, samples{};
};

// Process-local diagnostic bridge. The producer publishes only a fenced R16 copy
// with validated dimensions plus the CPU GI context that bracketed its recording.
void Publish(std::vector<std::uint8_t> volume, const std::array<std::uint8_t, 768>& constants,
    const std::array<float, 3>& camera, std::uint32_t contextFrame,
    std::uint64_t completedTick, bool cpuContextBracketed);
void Clear() noexcept;
Status CurrentStatus(std::uint64_t nowTick);

// Fixed Variant A parameters from the controlled torch test: tolerance 0,
// minimum step .05 gu, 400 iterations, .6 gu camera offset and 1 gu end margin.
TraceResult Trace(const std::array<float, 3>& target, std::uint64_t nowTick);
const char* VerdictName(Verdict verdict) noexcept;
}
