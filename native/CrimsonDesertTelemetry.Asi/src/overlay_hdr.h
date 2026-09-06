#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

namespace cdt::overlay::hdr
{
enum class OutputMode { Sdr, ScRgb, Hdr10, Unsupported };

// Match the declared buffer encoding, not monitor capability or HDR metadata.
OutputMode ResolveOutput(DXGI_FORMAT format, DXGI_COLOR_SPACE_TYPE colorSpace) noexcept;
DXGI_COLOR_SPACE_TYPE DefaultColorSpace(DXGI_FORMAT format) noexcept;

class Compositor
{
public:
    Compositor() = default;
    Compositor(const Compositor&) = delete;
    Compositor& operator=(const Compositor&) = delete;
    Compositor(Compositor&&) noexcept = default;
    Compositor& operator=(Compositor&&) noexcept = default;

    // Worker/idle only: allocates fixed resources and compiles original shaders.
    // The owner must fence all use before destruction or reinitialization.
    bool Initialize(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT backbufferFormat);

    // Record on the swapchain's direct queue. ImGui must target RGBA16_FLOAT and
    // retain its stock gamma-coded RGB / source-alpha blend state. Rendering into
    // transparent black produces premultiplied gamma-coded RGB in this texture.
    void BeginOverlay(ID3D12GraphicsCommandList* commands);
    ID3D12Resource* OverlayRenderTarget() const noexcept { return overlay_.Get(); }

    // No allocation, shader compilation, queue submission or wait. Backbuffer is
    // PRESENT on entry/exit; owned textures finish PIXEL_SHADER_RESOURCE. Repeated
    // frames must execute in recording order on the same direct queue. A false
    // return records no additional commands; the owner must not submit a partial
    // failed frame or recycle resources without its normal completion fence.
    bool Composite(ID3D12GraphicsCommandList* commands, ID3D12Resource* backbuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE backbufferRtv, OutputMode mode, float paperWhiteNits);

private:
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12Resource> overlay_, scene_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_, rtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    UINT width_{}, height_{};
    DXGI_FORMAT format_{DXGI_FORMAT_UNKNOWN};
    bool overlayOpen_{};
};
}
