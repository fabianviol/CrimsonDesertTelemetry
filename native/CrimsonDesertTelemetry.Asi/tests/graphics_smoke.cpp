#include "overlay.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace cdt::overlay;
namespace
{
void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("D3D12 smoke operation failed"); }
void Require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
struct Gpu
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12Fence> fence;
    UINT64 sequence{};
    void Wait()
    {
        Check(queue->Signal(fence.Get(), ++sequence));
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        Require(event != nullptr, "Fence event");
        Check(fence->SetEventOnCompletion(sequence, event));
        const DWORD result = WaitForSingleObject(event, 5000);
        CloseHandle(event);
        Require(result == WAIT_OBJECT_0, "Smoke GPU fence timeout");
    }
    void Begin() { Wait(); Check(allocator->Reset()); Check(commands->Reset(allocator.Get(), nullptr)); }
    void Submit()
    {
        Check(commands->Close());
        ID3D12CommandList* list[] = {commands.Get()};
        queue->ExecuteCommandLists(1, list);
    }
    void Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commands->ResourceBarrier(1, &barrier);
    }
};
View Fixture()
{
    View view;
    view.connected = view.hasSample = true;
    view.received = Clock::now();
    view.rateHz = 60;
    auto& s = view.sample;
    s.sequence = 1234; s.build = "TEST FIXTURE - NOT LIVE GAME DATA"; s.state = "playing";
    s.playerPosition = Vec3{-11157.0f, 761.3f, -5969.5f};
    s.playerForward = Vec3{0.866025f, 0, 0.5f}; s.playerHeading = 60.0f;
    s.playerUp = Vec3{0, 1, 0};
    s.cameraForward = Vec3{-0.482963f, 0.258819f, 0.836516f}; s.cameraHeading = 330.0f;
    s.pitch = 15.0f; s.fov = 50.0f; s.consensus = 48; s.validCopies = 100; s.distinctStates = 7; s.captureUs = 510;
    return view;
}
void SaveBuffer(Gpu& gpu, ID3D12Resource* buffer, const wchar_t* path)
{
    const auto desc = buffer->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 total{};
    gpu.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &total);
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readbackDesc{};
    readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = total; readbackDesc.Height = 1; readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1; readbackDesc.SampleDesc.Count = 1; readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    Check(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));
    gpu.Begin();
    gpu.Transition(buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION destination{}; destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; destination.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource = buffer; source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    gpu.commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    gpu.Transition(buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    gpu.Submit(); gpu.Wait();
    unsigned char* mapped{};
    D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
    Check(readback->Map(0, &range, reinterpret_cast<void**>(&mapped)));
    const auto width = static_cast<UINT>(desc.Width);
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; ++y)
        for (UINT x = 0; x < width; ++x)
        {
            const auto input = mapped + footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch + x * 4;
            const auto output = pixels.data() + (static_cast<size_t>(y) * width + x) * 4;
            output[0] = input[2]; output[1] = input[1]; output[2] = input[0]; output[3] = 255;
        }
    D3D12_RANGE noWrite{}; readback->Unmap(0, &noWrite);
    BITMAPFILEHEADER file{}; file.bfType = 0x4D42; file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size());
    BITMAPINFOHEADER info{}; info.biSize = sizeof(info); info.biWidth = static_cast<LONG>(width);
    info.biHeight = -static_cast<LONG>(desc.Height); info.biPlanes = 1; info.biBitCount = 32;
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(&file), sizeof(file));
    stream.write(reinterpret_cast<const char*>(&info), sizeof(info));
    stream.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    Require(stream.good(), "Could not save smoke image");
}
}
int wmain(int argc, wchar_t** argv)
{
    try
    {
        Config config; config.details = true;
        Require(!InstallGraphics(config), "Disabled HUD installed graphics hooks");
        config.enabled = true;
        Require(InstallGraphics(config), "Graphics hooks not installed");
        WNDCLASSW windowClass{}; windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = GetModuleHandleW(nullptr); windowClass.lpszClassName = L"CDTGraphicsSmoke";
        Require(RegisterClassW(&windowClass) != 0, "Window registration");
        // Hidden throughout: no focus changes or interference with the user's desktop.
        HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"CDT automated render test",
            WS_OVERLAPPEDWINDOW, 0, 0, 1000, 760, nullptr, nullptr, windowClass.hInstance, nullptr);
        Require(window != nullptr, "Hidden window creation");
        ComPtr<IDXGIFactory4> factory;
        Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter> adapter;
        Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        Gpu gpu;
        Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gpu.device)));
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        Check(gpu.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&gpu.queue)));
        Check(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gpu.allocator)));
        Check(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gpu.allocator.Get(), nullptr, IID_PPV_ARGS(&gpu.commands)));
        Check(gpu.commands->Close());
        Check(gpu.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gpu.fence)));
        DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width = 960; desc.Height = 720;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.BufferCount = 3;
        desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> chain1;
        Check(factory->CreateSwapChainForHwnd(gpu.queue.Get(), window, &desc, nullptr, nullptr, &chain1));
        ComPtr<IDXGISwapChain3> chain; Check(chain1.As(&chain));
        MaintainGraphics();
        std::cout << GraphicsStatus() << '\n';
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{}; heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; heapDesc.NumDescriptors = 1;
        ComPtr<ID3D12DescriptorHeap> rtvHeap;
        Check(gpu.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap)));
        const auto frame = [&](bool present1, const wchar_t* screenshot)
        {
            Publish(Fixture());
            ComPtr<ID3D12Resource> buffer;
            Check(chain->GetBuffer(chain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&buffer)));
            const auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
            gpu.device->CreateRenderTargetView(buffer.Get(), nullptr, rtv);
            gpu.Begin();
            gpu.Transition(buffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
            const float color[] = {0.10f, 0.15f, 0.21f, 1};
            gpu.commands->ClearRenderTargetView(rtv, color, 0, nullptr);
            gpu.Transition(buffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
            gpu.Submit();
            DXGI_PRESENT_PARAMETERS parameters{};
            Check(present1 ? chain->Present1(0, 0, &parameters) : chain->Present(0, 0));
            gpu.Wait();
            if (screenshot) SaveBuffer(gpu, buffer.Get(), screenshot);
        };
        for (int i = 0; i < 8; ++i) frame(i % 2 == 0, i == 7 && argc > 1 ? argv[1] : nullptr);
        Require(RenderedFrames() == 8, "Present/Present1 did not render exactly once per frame");
        std::cout << "PASS D3D12 Present/Present1 and multi-frame resource reuse\n";
        const auto before = RenderedFrames();
        Check(chain->Present(0, DXGI_PRESENT_TEST));
        Require(RenderedFrames() == before, "Test-only present changed output");
        SetVisibleForTest(false); frame(false, nullptr);
        Require(RenderedFrames() == before, "Hidden HUD drew a frame");
        SetVisibleForTest(true);
        Check(chain->ResizeBuffers(2, 1280, 800, DXGI_FORMAT_R8G8B8A8_UNORM, 0));
        MaintainGraphics(); frame(false, nullptr);
        Require(RenderedFrames() == before + 1, "HUD did not resume after ResizeBuffers");
        const UINT masks[] = {0, 0}; IUnknown* queues[] = {gpu.queue.Get(), gpu.queue.Get()};
        Check(chain->ResizeBuffers1(2, 960, 720, DXGI_FORMAT_R8G8B8A8_UNORM, 0, masks, queues));
        MaintainGraphics(); frame(true, nullptr);
        Require(RenderedFrames() == before + 2, "HUD did not resume after ResizeBuffers1");
        std::cout << "PASS visibility, test-only present, ResizeBuffers and ResizeBuffers1\n";
        Check(chain->ResizeBuffers(2, 3840, 2160, DXGI_FORMAT_R8G8B8A8_UNORM, 0));
        MaintainGraphics();
        frame(false, argc > 2 ? argv[2] : nullptr);
        Require(RenderedFrames() == before + 3, "4K HUD did not render after resize");
        std::cout << "PASS 4K HUD rendering after resize with resolution-scaled font atlas\n";
        Check(gpu.device->GetDeviceRemovedReason());
        DestroyWindow(window);
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
