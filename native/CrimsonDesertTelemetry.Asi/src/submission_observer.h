#pragma once
#include <atomic>
#include <d3d12.h>

namespace cdt::render
{
// Optional passive diagnostics share the existing queue hook. No second detour,
// no queue mutation, and no claim that an Execute return proves GPU completion.
using SubmissionObserver = void(*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*, bool);
inline std::atomic<SubmissionObserver> submissionObserver{};
}
