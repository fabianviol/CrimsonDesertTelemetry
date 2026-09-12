#include "sdf_visibility.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>

namespace cdt::sdf
{
namespace
{
constexpr unsigned Width = 128, Height = 64, ContentDepth = 128, LevelDepth = 130, Levels = 8;
constexpr size_t ExpectedBytes = size_t{Width} * Height * LevelDepth * Levels * 2;
constexpr std::uint64_t MaximumAgeMilliseconds = 8000;
struct Snapshot
{
    std::shared_ptr<const std::vector<std::uint8_t>> volume;
    std::array<std::uint8_t, 768> constants{};
    std::array<float, 3> camera{};
    std::uint32_t frame{};
    std::uint64_t tick{}, sequence{};
    bool bracketed{};
};
std::mutex mutex;
std::shared_ptr<const Snapshot> latest;
std::uint64_t nextSequence{};

float Lane(const std::array<std::uint8_t,768>& bytes, size_t offset, unsigned lane)
{
    float value{};
    std::memcpy(&value, bytes.data() + offset + lane * sizeof(float), sizeof(value));
    return value;
}
double Half(std::uint16_t bits)
{
    const unsigned sign = bits >> 15, exponent = (bits >> 10) & 31, mantissa = bits & 1023;
    double value{};
    if (!exponent) value = std::ldexp(static_cast<double>(mantissa), -24);
    else if (exponent == 31) value = mantissa ? std::numeric_limits<double>::quiet_NaN() :
        std::numeric_limits<double>::infinity();
    else value = std::ldexp(static_cast<double>(1024 + mantissa), static_cast<int>(exponent) - 25);
    return sign ? -value : value;
}
bool Finite(const std::array<float,3>& value)
{
    return std::all_of(value.begin(), value.end(), [](float lane) { return std::isfinite(lane); });
}
int FinestLevel(const Snapshot& snapshot, const std::array<double,3>& point)
{
    constexpr double baseExtent[3]{32.0,16.0,32.0};
    for (unsigned level = 0; level < Levels; ++level)
    {
        const float scale = Lane(snapshot.constants, 0x140 + 16 * level, 3);
        if (!std::isfinite(scale) || scale == 0) continue;
        bool inside = true;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            const double centre = Lane(snapshot.constants, 0x140 + 16 * level, axis) / scale;
            const double half = baseExtent[axis] * static_cast<double>(1u << level) * .5;
            inside = inside && std::isfinite(centre) && std::abs(point[axis] - centre) <= half;
        }
        if (inside) return static_cast<int>(level);
    }
    return -1;
}
double Sample(const Snapshot& snapshot, unsigned level, const std::array<double,3>& world)
{
    double normalised[3]{};
    for (unsigned axis = 0; axis < 3; ++axis)
        normalised[axis] = world[axis] * Lane(snapshot.constants, 0x10, axis) / static_cast<double>(1u << level);
    // The shader uses a normalized, linear WRAP sampler at LOD 0. Texel
    // centres are n + .5, including the two stored Z guard slices per level.
    const unsigned dims[3]{Width,Height,LevelDepth * Levels};
    int low[3]{};
    double fraction[3]{};
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        const double wrapped = normalised[axis] - std::floor(normalised[axis]);
        const double texel = axis == 2 ? level * LevelDepth + .5 + wrapped * ContentDepth :
            wrapped * dims[axis] - .5;
        low[axis] = static_cast<int>(std::floor(texel));
        fraction[axis] = texel - std::floor(texel);
    }
    double result{};
    for (unsigned dz = 0; dz < 2; ++dz)
        for (unsigned dy = 0; dy < 2; ++dy)
            for (unsigned dx = 0; dx < 2; ++dx)
            {
                // low can be -1 at an X/Y wrap boundary. Offset into the
                // positive range before converting to unsigned/modulo.
                const unsigned x = (static_cast<unsigned>(low[0] + static_cast<int>(Width)) + dx) % Width;
                const unsigned y = (static_cast<unsigned>(low[1] + static_cast<int>(Height)) + dy) % Height;
                const unsigned z = (static_cast<unsigned>(low[2]) + dz) % dims[2];
                const size_t offset = ((size_t{z} * Height + y) * Width + x) * 2;
                const auto& bytes = *snapshot.volume;
                const std::uint16_t raw = static_cast<std::uint16_t>(bytes[offset] | bytes[offset + 1] << 8);
                const double weight = (dx ? fraction[0] : 1 - fraction[0]) *
                    (dy ? fraction[1] : 1 - fraction[1]) * (dz ? fraction[2] : 1 - fraction[2]);
                result += weight * Half(raw);
            }
    return result;
}
std::shared_ptr<const Snapshot> Read() { std::lock_guard guard(mutex); return latest; }
Status Describe(const std::shared_ptr<const Snapshot>& snapshot, std::uint64_t now,
    std::uint64_t maximumAge = MaximumAgeMilliseconds)
{
    Status result;
    if (!snapshot) return result;
    result.sequence = snapshot->sequence; result.contextFrame = snapshot->frame;
    result.capturedTickMilliseconds = snapshot->tick;
    result.cpuContextBracketed = snapshot->bracketed;
    if (now < snapshot->tick) { result.reason = "invalid-sdf-clock"; return result; }
    result.ageMilliseconds = now - snapshot->tick;
    if (result.ageMilliseconds > maximumAge) { result.reason = "stale-sdf-volume"; return result; }
    if (!snapshot->bracketed) { result.reason = "unbracketed-sdf-context"; return result; }
    for(unsigned axis=0;axis<3;++axis)
    {
        const float inverse=Lane(snapshot->constants,0x10,axis);
        if(!std::isfinite(inverse)||inverse<=0||inverse>1)
        {result.reason="invalid-sdf-context";return result;}
    }
    result.available = true; result.reason.clear();
    return result;
}
}

void Publish(std::vector<std::uint8_t> volume, const std::array<std::uint8_t,768>& constants,
    const std::array<float,3>& camera, std::uint32_t frame, std::uint64_t tick, bool bracketed)
{
    if (volume.size() != ExpectedBytes || !Finite(camera) || !tick) return;
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->volume = std::make_shared<const std::vector<std::uint8_t>>(std::move(volume));
    snapshot->constants = constants; snapshot->camera = camera; snapshot->frame = frame;
    snapshot->tick = tick; snapshot->bracketed = bracketed;
    std::lock_guard guard(mutex); snapshot->sequence = ++nextSequence; latest = std::move(snapshot);
}
void Clear() noexcept { try { std::lock_guard guard(mutex); latest.reset(); } catch (...) {} }
Status CurrentStatus(std::uint64_t now) { return Describe(Read(),now); }

namespace
{
TraceResult TraceSnapshot(const std::shared_ptr<const Snapshot>& snapshot, const Status& status,
    const std::array<float,3>& origin, const std::array<float,3>& target)
{
    constexpr double StartOffset=.6, EndMargin=1.0, MinimumStep=.05;
    TraceResult result;
    static_cast<Status&>(result)=status;
    if(!result.available||!snapshot)return result;
    if(!Finite(origin)||!Finite(target))
    {result.available=false;result.reason="invalid-trace-position";return result;}
    std::array<double,3> delta{};
    for(unsigned axis=0;axis<3;++axis)delta[axis]=target[axis]-origin[axis];
    result.length=std::sqrt(delta[0]*delta[0]+delta[1]*delta[1]+delta[2]*delta[2]);
    if(!(result.length>StartOffset+EndMargin)){result.verdict=Verdict::TooShort;return result;}
    for(auto& lane:delta)lane/=result.length;
    double travelled=StartOffset,closest=std::numeric_limits<double>::infinity(),closestAt{};
    for(unsigned iteration=0;iteration<400;++iteration)
    {
        std::array<double,3> point{};
        for(unsigned axis=0;axis<3;++axis)point[axis]=origin[axis]+delta[axis]*travelled;
        const int level=FinestLevel(*snapshot,point);
        if(level<0){result.verdict=Verdict::Uncovered;result.at=travelled;result.samples=iteration;return result;}
        const double value=Sample(*snapshot,static_cast<unsigned>(level),point);
        if(!std::isfinite(value)){result.available=false;result.reason="invalid-sdf-sample";return result;}
        if(value<closest){closest=value;closestAt=travelled;}
        result.samples=iteration+1;result.closest=closest;result.closestAt=closestAt;
        if(value<=0){result.verdict=Verdict::Blocked;result.at=travelled;result.value=value;
            result.level=static_cast<unsigned>(level);return result;}
        travelled+=std::max(value,MinimumStep);
        if(travelled>=result.length-EndMargin){result.verdict=Verdict::Clear;return result;}
    }
    result.verdict=Verdict::IterationBound;result.at=travelled;return result;
}
}
TraceResult Trace(const std::array<float,3>& target, std::uint64_t now)
{
    const auto snapshot=Read();
    return TraceSnapshot(snapshot,Describe(snapshot,now),snapshot?snapshot->camera:std::array<float,3>{},target);
}
BatchResult TraceBatch(const std::array<float,3>& origin,
    std::span<const std::array<float,3>> targets,std::uint64_t now)
{
    const auto snapshot=Read();
    BatchResult batch{Describe(snapshot,now,ProductionMaximumAgeMilliseconds),{}};
    if(targets.size()>MaximumBatchTargets||!Finite(origin))
    {batch.status.available=false;batch.status.reason="invalid-trace-request";return batch;}
    batch.traces.reserve(targets.size());
    for(const auto& target:targets)batch.traces.push_back(TraceSnapshot(snapshot,batch.status,origin,target));
    return batch;
}
const char* VerdictName(Verdict verdict) noexcept
{
    switch(verdict){case Verdict::Clear:return "CLEAR";case Verdict::Blocked:return "BLOCKED";
    case Verdict::Uncovered:return "UNCOVERED";case Verdict::TooShort:return "TOO SHORT";
    case Verdict::IterationBound:return "ITERATION LIMIT";default:return "UNKNOWN";}
}
}
