#include "overlay_hdr.h"
#include <d3dcompiler.h>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace cdt::overlay::hdr
{
namespace
{
using Microsoft::WRL::ComPtr;

void Check(HRESULT result)
{
    if (FAILED(result)) throw std::runtime_error("HDR compositor initialization failed");
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

bool SupportedFormat(DXGI_FORMAT format)
{
    return format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM ||
        format == DXGI_FORMAT_R10G10B10A2_UNORM || format == DXGI_FORMAT_R16G16B16A16_FLOAT;
}

// Original compositor. scRGB uses linear Rec.709 with 1.0 = 80 nits; HDR10 uses
// Rec.2020 + ST.2084. Reference conventions:
// https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range
// UI conversion is confined to covered pixels; no whole-scene tone mapping occurs.
constexpr char shader[] = R"hlsl(
Texture2D<float4> overlayTexture : register(t0);
Texture2D<float4> sceneTexture : register(t1);
cbuffer Settings : register(b0) { uint outputMode; float paperWhiteNits; };

float4 VS(uint vertex : SV_VertexID) : SV_Position
{
    float2 corner = float2((vertex << 1) & 2, vertex & 2);
    return float4(corner * float2(2, -2) + float2(-1, 1), 0, 1);
}

float SrgbToLinear(float value)
{
    return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
}

float3 DecodePq(float3 encoded)
{
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 128.0;
    const float c3 = 2392.0 / 128.0;
    float3 p = pow(saturate(encoded), 1.0 / m2);
    return 10000.0 * pow(max(p - c1, 0.0) / max(c2 - c3 * p, 0.000001), 1.0 / m1);
}

float3 EncodePq(float3 nits)
{
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 128.0;
    const float c3 = 2392.0 / 128.0;
    float3 p = pow(saturate(nits / 10000.0), m1);
    return saturate(pow((c1 + c2 * p) / (1.0 + c3 * p), m2));
}

float4 PS(float4 position : SV_Position) : SV_Target
{
    int3 pixel = int3(int2(position.xy), 0);
    float4 scene = sceneTexture.Load(pixel);
    float4 ui = overlayTexture.Load(pixel);
    // Load/return raw scene values: transparent pixels never round-trip through
    // transfer functions, clamp negative scRGB, or change the scene's alpha.
    [branch] if (ui.a <= 0.0 || !all(isfinite(ui))) return scene;
    float alpha = saturate(ui.a);
    float3 rgb = saturate(ui.rgb / ui.a);
    // SDR mode retains stock ImGui's gamma-space blend appearance. HDR paths
    // explicitly decode before composition; PQ values are never alpha-blended.
    if (outputMode == 0) return float4(rgb * alpha + scene.rgb * (1.0 - alpha), scene.a);
    float3 uiLinear = float3(SrgbToLinear(rgb.r), SrgbToLinear(rgb.g), SrgbToLinear(rgb.b));
    if (outputMode == 1)
        return float4(uiLinear * (paperWhiteNits / 80.0) * alpha + scene.rgb * (1.0 - alpha), scene.a);

    float3 rec2020 = float3(
        dot(uiLinear, float3(0.627403896, 0.329283038, 0.043313066)),
        dot(uiLinear, float3(0.069097289, 0.919540395, 0.011362316)),
        dot(uiLinear, float3(0.016391439, 0.088013308, 0.895595253)));
    float3 nits = rec2020 * paperWhiteNits * alpha + DecodePq(scene.rgb) * (1.0 - alpha);
    return float4(EncodePq(nits), scene.a);
}
)hlsl";
}

OutputMode ResolveOutput(DXGI_FORMAT format, DXGI_COLOR_SPACE_TYPE colorSpace) noexcept
{
    if ((format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM ||
        format == DXGI_FORMAT_R10G10B10A2_UNORM) && colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)
        return OutputMode::Sdr;
    if (format == DXGI_FORMAT_R16G16B16A16_FLOAT && colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709)
        return OutputMode::ScRgb;
    if (format == DXGI_FORMAT_R10G10B10A2_UNORM && colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
        return OutputMode::Hdr10;
    return OutputMode::Unsupported;
}

DXGI_COLOR_SPACE_TYPE DefaultColorSpace(DXGI_FORMAT format) noexcept
{
    return format == DXGI_FORMAT_R16G16B16A16_FLOAT
        ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
}

bool Compositor::Initialize(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT backbufferFormat)
{
    if (!device || !width || !height || width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || !SupportedFormat(backbufferFormat)) return false;
    try
    {
        Compositor next;
        next.device_ = device; next.width_ = width; next.height_ = height; next.format_ = backbufferFormat;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC texture{};
        texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture.Width = width; texture.Height = height; texture.DepthOrArraySize = 1;
        texture.MipLevels = 1; texture.SampleDesc.Count = 1;
        texture.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE clear{}; clear.Format = texture.Format;
        Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&next.overlay_)));
        texture.Format = backbufferFormat; texture.Flags = D3D12_RESOURCE_FLAG_NONE;
        Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&next.scene_)));

        D3D12_DESCRIPTOR_HEAP_DESC descriptors{};
        descriptors.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; descriptors.NumDescriptors = 1;
        Check(device->CreateDescriptorHeap(&descriptors, IID_PPV_ARGS(&next.rtvHeap_)));
        device->CreateRenderTargetView(next.overlay_.Get(), nullptr, next.rtvHeap_->GetCPUDescriptorHandleForHeapStart());
        descriptors.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        descriptors.NumDescriptors = 2; descriptors.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Check(device->CreateDescriptorHeap(&descriptors, IID_PPV_ARGS(&next.srvHeap_)));
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; view.Texture2D.MipLevels = 1;
        view.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        auto descriptor = next.srvHeap_->GetCPUDescriptorHandleForHeapStart();
        device->CreateShaderResourceView(next.overlay_.Get(), &view, descriptor);
        descriptor.ptr += device->GetDescriptorHandleIncrementSize(descriptors.Type);
        view.Format = backbufferFormat;
        device->CreateShaderResourceView(next.scene_.Get(), &view, descriptor);

        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; range.NumDescriptors = 2;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        std::array<D3D12_ROOT_PARAMETER, 2> parameters{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable.NumDescriptorRanges = 1;
        parameters[0].DescriptorTable.pDescriptorRanges = &range;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[1].Constants.Num32BitValues = 2;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC root{};
        root.NumParameters = static_cast<UINT>(parameters.size()); root.pParameters = parameters.data();
        root.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
        ComPtr<ID3DBlob> serialized, errors, vertex, pixel;
        Check(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors));
        Check(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
            IID_PPV_ARGS(&next.root_)));
        constexpr UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS |
            D3DCOMPILE_OPTIMIZATION_LEVEL3;
        Check(D3DCompile(shader, sizeof(shader) - 1, "cdt_overlay_hdr", nullptr, nullptr,
            "VS", "vs_5_0", flags, 0, &vertex, &errors));
        Check(D3DCompile(shader, sizeof(shader) - 1, "cdt_overlay_hdr", nullptr, nullptr,
            "PS", "ps_5_0", flags, 0, &pixel, &errors));
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
        pipeline.pRootSignature = next.root_.Get();
        pipeline.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
        pipeline.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
        auto& blend = pipeline.BlendState.RenderTarget[0];
        blend.SrcBlend = D3D12_BLEND_ONE; blend.DestBlend = D3D12_BLEND_ZERO;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE; blend.DestBlendAlpha = D3D12_BLEND_ZERO;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.LogicOp = D3D12_LOGIC_OP_NOOP; blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pipeline.SampleMask = std::numeric_limits<UINT>::max();
        pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pipeline.RasterizerState.DepthClipEnable = TRUE;
        pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pipeline.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        pipeline.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
        pipeline.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
        pipeline.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        pipeline.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        pipeline.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pipeline.DepthStencilState.BackFace = pipeline.DepthStencilState.FrontFace;
        pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline.NumRenderTargets = 1; pipeline.RTVFormats[0] = backbufferFormat;
        pipeline.SampleDesc.Count = 1;
        Check(device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&next.pipeline_)));
        *this = std::move(next);
        return true;
    }
    catch (const std::exception&) { return false; }
}

void Compositor::BeginOverlay(ID3D12GraphicsCommandList* commands)
{
    if (!commands || !overlay_ || overlayOpen_) return;
    const auto barrier = Transition(overlay_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    commands->ResourceBarrier(1, &barrier);
    const auto rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    const float transparent[4]{};
    commands->ClearRenderTargetView(rtv, transparent, 0, nullptr);
    commands->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    overlayOpen_ = true;
}

bool Compositor::Composite(ID3D12GraphicsCommandList* commands, ID3D12Resource* backbuffer,
    D3D12_CPU_DESCRIPTOR_HANDLE backbufferRtv, OutputMode mode, float paperWhiteNits)
{
    if (!commands || !backbuffer || !backbufferRtv.ptr || !pipeline_ || !overlayOpen_ ||
        !std::isfinite(paperWhiteNits) || paperWhiteNits < 80.0f || paperWhiteNits > 500.0f) return false;
    const bool validMode = (mode == OutputMode::ScRgb && format_ == DXGI_FORMAT_R16G16B16A16_FLOAT) ||
        (mode == OutputMode::Hdr10 && format_ == DXGI_FORMAT_R10G10B10A2_UNORM) ||
        (mode == OutputMode::Sdr && format_ != DXGI_FORMAT_R16G16B16A16_FLOAT);
    if (!validMode) return false;
    const auto description = backbuffer->GetDesc();
    if (description.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || description.Width != width_ ||
        description.Height != height_ || description.Format != format_ || description.MipLevels != 1 ||
        description.DepthOrArraySize != 1 || description.SampleDesc.Count != 1) return false;

    const std::array barriers{
        Transition(overlay_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        Transition(backbuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE),
        Transition(scene_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST)};
    commands->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    commands->CopyResource(scene_.Get(), backbuffer);
    const std::array ready{
        Transition(scene_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        Transition(backbuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)};
    commands->ResourceBarrier(static_cast<UINT>(ready.size()), ready.data());
    commands->SetPipelineState(pipeline_.Get());
    commands->SetGraphicsRootSignature(root_.Get());
    ID3D12DescriptorHeap* heaps[] = {srvHeap_.Get()};
    commands->SetDescriptorHeaps(1, heaps);
    commands->SetGraphicsRootDescriptorTable(0, srvHeap_->GetGPUDescriptorHandleForHeapStart());
    struct Constants { UINT mode; float paperWhite; } constants{static_cast<UINT>(mode), paperWhiteNits};
    static_assert(sizeof(Constants) == 2 * sizeof(UINT));
    commands->SetGraphicsRoot32BitConstants(1, 2, &constants, 0);
    const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    commands->RSSetViewports(1, &viewport); commands->RSSetScissorRects(1, &scissor);
    commands->OMSetRenderTargets(1, &backbufferRtv, FALSE, nullptr);
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands->DrawInstanced(3, 1, 0, 0);
    const auto finished = Transition(backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    commands->ResourceBarrier(1, &finished);
    overlayOpen_ = false;
    return true;
}
}
