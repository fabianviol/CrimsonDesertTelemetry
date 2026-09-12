#pragma once
#include <array>
#include <cstdint>
#include <d3d12.h>

namespace cdt::sdf::acquire
{
void Start();
void ObserveContext(ID3D12GraphicsCommandList* list,
    const std::array<std::uint8_t, 768>& gi, const std::array<float, 3>& camera,
    std::uint32_t frame, std::uint64_t tick, bool stable);
void Poll();
void Submission(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists, bool after);
void Stop();
bool OwnsCodeAddress(std::uint64_t address);
}
