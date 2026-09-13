#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <span>
#include <vector>

namespace cdt::sdf
{
enum class Verdict { Unavailable, Clear, Blocked, Uncovered, TooShort, IterationBound };

struct Status
{
    bool available{};
    std::uint64_t sequence{}, ageMilliseconds{};
    std::uint64_t capturedTickMilliseconds{};
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

// Process-local geometry snapshot. The producer publishes only a fenced R16 copy
// with validated dimensions plus the CPU GI context that bracketed its recording.
void Publish(std::vector<std::uint8_t> volume, const std::array<std::uint8_t, 768>& constants,
    const std::array<float, 3>& camera, std::uint32_t contextFrame,
    std::uint64_t capturedTick, bool cpuContextBracketed);
void Clear() noexcept;
Status CurrentStatus(std::uint64_t nowTick);

// Voxel-segment test. The research target uses connected quarter-cell sampling;
// the narrow production fallback is compiled separately with its preserved
// zero-crossing marcher. The caller supplies the receiver position; no camera
// direction or projection participates.
TraceResult Trace(const std::array<float, 3>& target, std::uint64_t nowTick);
inline constexpr std::uint32_t ProductionMaximumAgeMilliseconds = 1500;
inline constexpr unsigned MaximumBatchTargets = 256;
struct BatchResult
{
    Status status;
    std::vector<TraceResult> traces;
};
// All targets use one immutable volume and the explicit receiver position.
// There is no projection test or retained light identity.
BatchResult TraceBatch(const std::array<float, 3>& origin,
    std::span<const std::array<float, 3>> targets, std::uint64_t nowTick);
const char* VerdictName(Verdict verdict) noexcept;
}
