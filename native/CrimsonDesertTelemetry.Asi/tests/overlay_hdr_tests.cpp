#include "overlay_hdr.h"
#include <d3d12sdklayers.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace cdt::overlay::hdr;
namespace
{
using Color = std::array<float, 4>;
constexpr UINT width = 8, height = 2;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void Check(HRESULT result)
{
    if (FAILED(result)) throw std::runtime_error("HDR WARP operation failed: " + std::to_string(result));
}
void Near(double actual, double expected, double tolerance, const char* message)
{
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance)
        throw std::runtime_error(std::string(message) + ": got " + std::to_string(actual) +
            ", expected " + std::to_string(expected));
}
void Transition(ID3D12GraphicsCommandList* commands, ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before; barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commands->ResourceBarrier(1, &barrier);
}

struct Gpu
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12InfoQueue> diagnostics;
    UINT64 sequence{};

    Gpu()
    {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
        ComPtr<IDXGIFactory4> factory; Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter> adapter; Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        // Optional SDK-layer diagnostics supplement actual pixel readback.
        device.As(&diagnostics);
        D3D12_COMMAND_QUEUE_DESC description{};
        Check(device->CreateCommandQueue(&description, IID_PPV_ARGS(&queue)));
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
            IID_PPV_ARGS(&commands)));
        Check(commands->Close());
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    }
    void Wait()
    {
        Check(queue->Signal(fence.Get(), ++sequence));
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        Require(event != nullptr, "HDR fence event creation");
        const HRESULT status = fence->SetEventOnCompletion(sequence, event);
        const DWORD wait = SUCCEEDED(status) ? WaitForSingleObject(event, 10000) : WAIT_FAILED;
        CloseHandle(event); Check(status);
        Require(wait == WAIT_OBJECT_0, "HDR GPU completion timed out");
    }
    void Begin()
    {
        Wait(); Check(allocator->Reset()); Check(commands->Reset(allocator.Get(), nullptr));
    }
    void Submit()
    {
        Check(commands->Close());
        ID3D12CommandList* lists[] = {commands.Get()}; queue->ExecuteCommandLists(1, lists); Wait();
    }
    void RequireNoErrors()
    {
        Check(device->GetDeviceRemovedReason());
        if (!diagnostics) return;
        const UINT64 count = diagnostics->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 index = 0; index < count; ++index)
        {
            SIZE_T bytes{}; Check(diagnostics->GetMessage(index, nullptr, &bytes));
            std::vector<unsigned char> storage(bytes);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            Check(diagnostics->GetMessage(index, message, &bytes));
            if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)
                throw std::runtime_error(std::string("D3D12 validation: ") + message->pDescription);
        }
        diagnostics->ClearStoredMessages();
    }
};

double Half(std::uint16_t bits)
{
    const double sign = (bits & 0x8000) ? -1.0 : 1.0;
    const auto exponent = (bits >> 10) & 31;
    const auto fraction = bits & 1023;
    if (exponent == 0) return sign * std::ldexp(static_cast<double>(fraction), -24);
    if (exponent == 31) return fraction ? std::numeric_limits<double>::quiet_NaN() :
        sign * std::numeric_limits<double>::infinity();
    return sign * std::ldexp(1.0 + static_cast<double>(fraction) / 1024.0, exponent - 15);
}

struct Pixels
{
    DXGI_FORMAT format{};
    UINT stride{};
    std::vector<unsigned char> bytes;
    std::array<double, 4> At(UINT x, UINT y = 0) const
    {
        const auto* pixel = bytes.data() + (static_cast<size_t>(y) * width + x) * stride;
        if (format == DXGI_FORMAT_R16G16B16A16_FLOAT)
        {
            std::array<std::uint16_t, 4> bits{}; std::memcpy(bits.data(), pixel, sizeof(bits));
            return {Half(bits[0]), Half(bits[1]), Half(bits[2]), Half(bits[3])};
        }
        if (format == DXGI_FORMAT_R10G10B10A2_UNORM)
        {
            std::uint32_t bits{}; std::memcpy(&bits, pixel, sizeof(bits));
            return {static_cast<double>(bits & 1023) / 1023.0,
                static_cast<double>((bits >> 10) & 1023) / 1023.0,
                static_cast<double>((bits >> 20) & 1023) / 1023.0,
                static_cast<double>(bits >> 30) / 3.0};
        }
        const UINT red = format == DXGI_FORMAT_B8G8R8A8_UNORM ? 2u : 0u;
        return {pixel[red] / 255.0, pixel[1] / 255.0, pixel[2 - red] / 255.0, pixel[3] / 255.0};
    }
    bool SamePixel(const Pixels& other, UINT x, UINT y = 0) const
    {
        const size_t offset = (static_cast<size_t>(y) * width + x) * stride;
        return stride == other.stride && std::memcmp(bytes.data() + offset, other.bytes.data() + offset, stride) == 0;
    }
};

Pixels Read(Gpu& gpu, ID3D12Resource* texture)
{
    const auto description = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT64 total{};
    gpu.device->GetCopyableFootprints(&description, 0, 1, 0, &footprint, nullptr, nullptr, &total);
    D3D12_RESOURCE_DESC buffer{}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = total; buffer.Height = 1; buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1; buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
    ComPtr<ID3D12Resource> staging;
    Check(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&staging)));
    gpu.Begin();
    Transition(gpu.commands.Get(), texture, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource = texture;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION destination{}; destination.pResource = staging.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; destination.PlacedFootprint = footprint;
    gpu.commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    Transition(gpu.commands.Get(), texture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    gpu.Submit();
    Pixels result; result.format = description.Format;
    result.stride = description.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8u : 4u;
    result.bytes.resize(static_cast<size_t>(width) * height * result.stride);
    unsigned char* mapped{}; D3D12_RANGE readRange{0, static_cast<SIZE_T>(total)};
    Check(staging->Map(0, &readRange, reinterpret_cast<void**>(&mapped)));
    for (UINT row = 0; row < height; ++row)
        std::memcpy(result.bytes.data() + static_cast<size_t>(row) * width * result.stride,
            mapped + footprint.Offset + static_cast<size_t>(row) * footprint.Footprint.RowPitch,
            static_cast<size_t>(width) * result.stride);
    D3D12_RANGE noWrite{}; staging->Unmap(0, &noWrite);
    return result;
}

struct Fixture
{
    Gpu& gpu;
    Compositor compositor;
    ComPtr<ID3D12Resource> target;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv{}, uiRtv{};
    Fixture(Gpu& graphics, DXGI_FORMAT format) : gpu(graphics)
    {
        Require(compositor.Initialize(gpu.device.Get(), width, height, format), "HDR compositor initialization");
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC texture{}; texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture.Width = width; texture.Height = height; texture.DepthOrArraySize = 1;
        texture.MipLevels = 1; texture.SampleDesc.Count = 1;
        texture.Format = format; texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE clear{}; clear.Format = format;
        Check(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture,
            D3D12_RESOURCE_STATE_PRESENT, &clear, IID_PPV_ARGS(&target)));
        D3D12_DESCRIPTOR_HEAP_DESC descriptors{}; descriptors.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        descriptors.NumDescriptors = 2; Check(gpu.device->CreateDescriptorHeap(&descriptors, IID_PPV_ARGS(&rtvHeap)));
        sceneRtv = rtvHeap->GetCPUDescriptorHandleForHeapStart(); uiRtv = sceneRtv;
        uiRtv.ptr += gpu.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        gpu.device->CreateRenderTargetView(target.Get(), nullptr, sceneRtv);
        gpu.device->CreateRenderTargetView(compositor.OverlayRenderTarget(), nullptr, uiRtv);
    }
    Pixels Background(const std::array<Color, width>& colors)
    {
        gpu.Begin();
        Transition(gpu.commands.Get(), target.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        for (UINT x = 0; x < width; ++x)
        {
            const D3D12_RECT rect{static_cast<LONG>(x), 0, static_cast<LONG>(x + 1), static_cast<LONG>(height)};
            gpu.commands->ClearRenderTargetView(sceneRtv, colors[x].data(), 1, &rect);
        }
        Transition(gpu.commands.Get(), target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        gpu.Submit(); return Read(gpu, target.Get());
    }
    Pixels Draw(const std::array<Color, width>& colors, OutputMode mode, float paperWhite = 200)
    {
        gpu.Begin(); compositor.BeginOverlay(gpu.commands.Get());
        for (UINT x = 0; x < width; ++x)
        {
            // Leave row 1 completely clear: tests compare the whole row bytewise
            // against the raw background as well as the covered row's goldens.
            const D3D12_RECT rect{static_cast<LONG>(x), 0, static_cast<LONG>(x + 1), 1};
            gpu.commands->ClearRenderTargetView(uiRtv, colors[x].data(), 1, &rect);
        }
        Require(!compositor.Composite(gpu.commands.Get(), target.Get(), sceneRtv, OutputMode::Unsupported, paperWhite),
            "Unsupported mode recorded a composite");
        Require(!compositor.Composite(gpu.commands.Get(), target.Get(), sceneRtv, mode,
            std::numeric_limits<float>::quiet_NaN()), "Nonfinite paper white accepted");
        Require(compositor.Composite(gpu.commands.Get(), target.Get(), sceneRtv, mode, paperWhite), "HDR composite failed");
        gpu.Submit(); return Read(gpu, target.Get());
    }
};

void Passthrough(const Pixels& before, const Pixels& after)
{
    for (UINT x = 0; x < width; ++x)
    {
        Require(before.SamePixel(after, x, 1), "Transparent UI changed raw scene bytes");
        Near(after.At(x)[3], before.At(x)[3], 0, "Composite changed background alpha");
    }
    Require(before.SamePixel(after, 0), "Transparent column changed scene bytes");
}

void TestModes()
{
    const std::array formats{DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_UNKNOWN};
    const std::array spaces{DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709,
        DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020,
        DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020, DXGI_COLOR_SPACE_CUSTOM};
    for (size_t f = 0; f < formats.size(); ++f)
        for (size_t s = 0; s < spaces.size(); ++s)
        {
            const OutputMode expected = f < 3 && s == 0 ? OutputMode::Sdr :
                f == 3 && s == 1 ? OutputMode::ScRgb : f == 2 && s == 2 ? OutputMode::Hdr10 : OutputMode::Unsupported;
            Require(ResolveOutput(formats[f], spaces[s]) == expected, "Output mode matrix mismatch");
        }
    Require(DefaultColorSpace(DXGI_FORMAT_R16G16B16A16_FLOAT) == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709,
        "FP16 swapchain default is not linear");
    Require(DefaultColorSpace(DXGI_FORMAT_R10G10B10A2_UNORM) == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,
        "10-bit swapchain guessed HDR without an explicit color space");
    std::cout << "PASS exact output-mode matrix and DXGI format defaults\n";
}

void TestScRgb(Gpu& gpu)
{
    Fixture fixture(gpu, DXGI_FORMAT_R16G16B16A16_FLOAT);
    std::array<Color, width> scene; scene.fill({1.25f, 1.25f, 1.25f, .5f});
    scene[0] = {4, -.5f, 1.25f, .25f};
    scene[5] = {4, -.5f, 8, .75f};
    scene[6] = {0, 16, -2, .125f};
    const std::array<Color, width> ui{{
        {0, 0, 0, 0}, {1, 1, 1, 1}, {.5f, .5f, .5f, .5f}, {.5f, .5f, .5f, 1},
        {1, 0, 0, 1}, {0, 0, 0, .5f}, {.6f, .4f, .2f, 0}, {.04f, .04f, .04f, 1}}};
    for (const float paperWhite : std::array{200.f, 80.f, 500.f})
    {
        const auto before = fixture.Background(scene);
        const auto output = fixture.Draw(ui, OutputMode::ScRgb, paperWhite);
        Passthrough(before, output);
        Require(before.SamePixel(output, 6), "Zero-alpha nonzero RGB changed extended scRGB");
        const double scale = paperWhite / 80.0;
        for (UINT channel = 0; channel < 3; ++channel)
        {
            Near(output.At(1)[channel], scale, .004, "Opaque SDR white has incorrect scRGB nits");
            Near(output.At(2)[channel], .625 + scale * .5, .004, "scRGB alpha was not blended linearly");
            // Independent sRGB .5 reference: 0.21404114048223255 linear.
            Near(output.At(3)[channel], .21404114048223255 * scale, .002, "sRGB gray was not decoded");
            Near(output.At(7)[channel], (.04 / 12.92) * scale, .00004, "sRGB linear toe conversion failed");
        }
        Near(output.At(4)[0], scale, .004, "scRGB red primary changed");
        Near(output.At(4)[1], 0, 0, "scRGB green leakage"); Near(output.At(4)[2], 0, 0, "scRGB blue leakage");
        Near(output.At(5)[0], 2, 0, "Extended scRGB background clipped");
        Near(output.At(5)[1], -.25, 0, "Negative scRGB background clipped");
        Near(output.At(5)[2], 4, 0, "Extended scRGB background lost");
    }
    gpu.RequireNoErrors();
    std::cout << "PASS scRGB nits, sRGB decode, premultiplied alpha, extended/negative background and repeated states\n";
}

double PqNits(double encoded)
{
    // Independent double-precision ST.2084 reference for quantized readback;
    // fixed known PQ code values below also guard against matching shader errors.
    const double p = std::pow(encoded, 32.0 / 2523.0);
    return 10000.0 * std::pow((std::max)(p - 3424.0 / 4096.0, 0.0) /
        (2413.0 / 128.0 - (2392.0 / 128.0) * p), 16384.0 / 2610.0);
}

void TestHdr10(Gpu& gpu)
{
    Fixture fixture(gpu, DXGI_FORMAT_R10G10B10A2_UNORM);
    constexpr float pq100 = .508078421517399f, pq1000 = .751827096247041f;
    std::array<Color, width> scene; scene.fill({pq100, pq100, pq100, 2.f / 3});
    scene[0] = {.1234f, .6543f, .9876f, 1.f / 3};
    scene[5] = {1, 1, 1, 1};
    scene[6] = {0, 1, pq1000, 0};
    scene[7] = {0, 0, 0, 1};
    const std::array<Color, width> ui{{
        {0, 0, 0, 0}, {1, 1, 1, 1}, {.5f, .5f, .5f, .5f}, {.5f, .5f, .5f, 1},
        {1, 0, 0, 1}, {0, 0, 0, .5f}, {.25f, .25f, .25f, .25f}, {0, 0, 0, 1}}};
    for (int frame = 0; frame < 3; ++frame)
    {
        const auto before = fixture.Background(scene);
        const auto output = fixture.Draw(ui, OutputMode::Hdr10);
        Passthrough(before, output);
        for (UINT channel = 0; channel < 3; ++channel)
        {
            Near(output.At(1)[channel], .579133245243520, 1.1 / 1023, "HDR10 white does not encode 200 nits");
            Near(output.At(2)[channel], .549302036131926, 1.1 / 1023, "HDR10 white blend is not 150 nits");
            Near(PqNits(output.At(2)[channel]), (200 + PqNits(before.At(2)[channel])) * .5, 2,
                "HDR10 blended PQ codes instead of linear light");
            Near(PqNits(output.At(3)[channel]), 42.8082280964, .6, "HDR10 gamma gray incorrect");
            Near(output.At(5)[channel], .92654670408263, 1.1 / 1023, "HDR10 10000-nit background blend incorrect");
            Near(output.At(7)[channel], 0, 0, "PQ black is not finite zero after UNORM encoding");
            constexpr std::array<double, 3> extremeTolerance{.8, 60, 8};
            Near(PqNits(output.At(6)[channel]), .75 * PqNits(before.At(6)[channel]) + 50, extremeTolerance[channel],
                "PQ extreme conversion is nonfinite or clipped incorrectly");
        }
        Near(output.At(4)[0], .531029114055, 1.1 / 1023, "Red709 to2020 red conversion incorrect");
        Near(output.At(4)[1], .325794232045, 1.1 / 1023, "Red709 to2020 green conversion incorrect");
        Near(output.At(4)[2], .219092232341, 1.1 / 1023, "Red709 to2020 blue conversion incorrect");
    }
    gpu.RequireNoErrors();
    std::cout << "PASS HDR10 PQ goldens, Rec.709/2020 conversion, linear-light alpha, extrema and raw transparency\n";
}

void TestSdr(Gpu& gpu)
{
    for (const auto format : std::array{DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM})
    {
        Fixture fixture(gpu, format);
        std::array<Color, width> scene; scene.fill({.2f, .4f, .8f, 1.f / 3});
        std::array<Color, width> ui{}; ui[1] = {.5f, 0, 0, .5f};
        const auto before = fixture.Background(scene);
        const auto output = fixture.Draw(ui, OutputMode::Sdr);
        Passthrough(before, output);
        Near(output.At(1)[0], .6, .005, "SDR premultiplied red changed");
        Near(output.At(1)[1], .2, .005, "SDR background green changed");
        Near(output.At(1)[2], .4, .005, "SDR background blue changed");
    }
    gpu.RequireNoErrors(); std::cout << "PASS 8-bit RGB/BGR and 10-bit SDR composition\n";
}
}

int main()
{
    try
    {
        TestModes();
        Gpu gpu;
        Compositor invalid;
        Require(!invalid.Initialize(nullptr, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT), "Null device accepted");
        Require(!invalid.Initialize(gpu.device.Get(), 0, height, DXGI_FORMAT_R16G16B16A16_FLOAT), "Zero extent accepted");
        Require(!invalid.Initialize(gpu.device.Get(), width, height, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB),
            "Implicit sRGB conversion format accepted");
        TestScRgb(gpu); TestHdr10(gpu); TestSdr(gpu);
        std::cout << "All HDR compositor checks passed using offscreen WARP textures; no HDR display or game test.\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
