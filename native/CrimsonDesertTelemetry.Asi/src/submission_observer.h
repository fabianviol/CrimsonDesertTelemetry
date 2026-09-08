#pragma once
#include <atomic>
#include <d3d12.h>

namespace cdt::render
{
// Optional diagnostics share the existing queue hook. No second detour. A guarded
// private readback may Signal its own fence AFTER the matching original Execute;
// returning from Execute alone never proves GPU completion.
using SubmissionObserver = void(*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*, bool);
inline std::atomic<SubmissionObserver> submissionObserver{};
}
