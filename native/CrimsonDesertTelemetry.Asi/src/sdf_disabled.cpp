// The stability release excludes unvalidated per-light occlusion. Keep the API
// unavailable and install no SDF hooks, even if an old INI requests the feature.
// The implementation and retained tests remain in sdf_visibility/sdf_acquire.
#include "sdf_visibility.h"
#include "sdf_acquire.h"

namespace cdt::sdf
{
void Publish(std::vector<std::uint8_t>, const std::array<std::uint8_t, 768>&,
    const std::array<float, 3>&, std::uint32_t, std::uint64_t, bool)
{
}

void Clear() noexcept {}

Status CurrentStatus(std::uint64_t)
{
    Status status;
    status.reason = "not-built-in-this-package";
    return status;
}

TraceResult Trace(const std::array<float, 3>&, std::uint64_t)
{
    TraceResult result;
    result.reason = "not-built-in-this-package";
    return result;
}

BatchResult TraceBatch(const std::array<float, 3>&,
    std::span<const std::array<float, 3>>, std::uint64_t now)
{
    return {CurrentStatus(now), {}};
}

// Kept identical to the real implementation: it is pure naming, and the HUD
// prints it whatever the verdict is.
const char* VerdictName(Verdict verdict) noexcept
{
    switch (verdict)
    {
    case Verdict::Clear: return "CLEAR";
    case Verdict::Blocked: return "BLOCKED";
    case Verdict::Uncovered: return "UNCOVERED";
    case Verdict::TooShort: return "TOO SHORT";
    case Verdict::IterationBound: return "ITERATION LIMIT";
    case Verdict::Unavailable: break;
    }
    return "UNKNOWN";
}
}

namespace cdt::sdf::acquire
{
void Start() {}
void ObserveContext(ID3D12GraphicsCommandList*, const std::array<std::uint8_t, 768>&,
    const std::array<float, 3>&, std::uint32_t, std::uint64_t, bool) {}
void Poll() {}
void Submission(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*, bool) {}
void Stop() {}
bool OwnsCodeAddress(std::uint64_t) { return false; }
}
