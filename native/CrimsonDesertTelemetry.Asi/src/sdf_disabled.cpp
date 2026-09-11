// Stand-ins for the experimental Variant A line-of-sight test, compiled instead
// of sdf_visibility.cpp when CDT_RESEARCH is OFF. See research_disabled.cpp for
// why release packages are built that way.
//
// The HUD calls CurrentStatus() and Trace() unconditionally and renders whatever
// they report, so an unavailable status is all that is needed here: the SDF panel
// shows its normal "no volume" state. Publish() cannot be reached at all without
// the spatial probe that produces the volume.
#include "sdf_visibility.h"

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
