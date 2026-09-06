#include "overlay.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <numbers>
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
View LightFixture(float aspect)
{
    auto view = Fixture();
    auto& sample = view.sample;
    sample.schemaVersion = "1.4";
    sample.orientationSource = "scene-constants-test-fixture";
    const Vec3 camera{-10528, 611, -4354};
    sample.cameraPosition = camera;
    sample.cameraForward = Vec3{0, 0, 1}; sample.cameraRight = Vec3{1, 0, 0}; sample.cameraUp = Vec3{0, 1, 0};
    sample.cameraHeading = 0.0f; sample.pitch = 0.0f; sample.fov = 60.0f;
    sample.aspectRatio = aspect; sample.nearPlane = 0.1f;
    sample.playerPosition = Vec3{camera.x, camera.y - 1, camera.z + 0.5f};
    sample.playerForward = Vec3{0, 0, 1}; sample.playerHeading = 0.0f;
    const auto position = [&](float x, float y, float z) { return Vec3{camera.x + x, camera.y + y, camera.z + z}; };
    // Five visible contributions, including a close pair with independent RGB,
    // one behind the camera and one before its near plane. Slots are not IDs.
    const std::vector<LightRecord> records{
        {0, position(0, 0, 10), {0.85f, 0.30f, 0.08f}, 0.40f, "point", std::nullopt, std::nullopt},
        {1, position(-3, 2, 12), {1.20f, 0.65f, 0.12f}, 0.74f, "point", std::nullopt, std::nullopt},
        {2, position(4, 1, 14), {0.30f, 0.60f, 1.80f}, 0.63f, "spot", Vec3{0, -1, 0}, 27.0f},
        {3, position(-4, -2, -8), {0.85f, 0.05f, 1.20f}, 0.30f, "point", std::nullopt, std::nullopt},
        {4, position(0.01f, 0.005f, 0.02f), {0, 2, 0}, 1.40f, "point", std::nullopt, std::nullopt},
        {5, position(2, -2, 8), {0.06f, 0.15f, 0.85f}, 0.19f, "point", std::nullopt, std::nullopt},
        {6, position(0, .03f, 10), {2.4f, .75f, .19f}, 1.05f, "point", std::nullopt, std::nullopt}
    };
    sample.authoredLights.status = "available";
    sample.authoredLights.publishedRecords = 2u;
    sample.renderedLights.status = "available";
    sample.renderedLights.publishedRecords = static_cast<std::uint32_t>(records.size());
    sample.renderedLights.ageMilliseconds = 12;
    sample.renderedLights.records = std::make_shared<const std::vector<LightRecord>>(records);
    return view;
}
View PitchedLightFixture(float aspect)
{
    auto view = LightFixture(aspect);
    auto& sample = view.sample;
    sample.cameraPosition->y += 6;
    sample.cameraPosition->z -= 5;
    // Down40 degrees with15 degrees of roll; this cannot be represented by yaw.
    const float pitch = -40*std::numbers::pi_v<float>/180;
    const float roll = 15*std::numbers::pi_v<float>/180;
    const float sp = std::sin(pitch), cp = std::cos(pitch), sr = std::sin(roll), cr = std::cos(roll);
    sample.cameraForward = Vec3{0,sp,cp};
    sample.cameraRight = Vec3{cr,cp*sr,-sp*sr};
    sample.cameraUp = Vec3{-sr,cp*cr,-sp*cr};
    sample.pitch = -40.f;
    return view;
}
struct Pixels
{
    UINT width{}, height{};
    std::vector<unsigned char> bgra;
    size_t Changed(int left, int top, int right, int bottom) const
    {
        size_t count = 0;
        for (int y = std::max(0, top); y < std::min(static_cast<int>(height), bottom); ++y)
            for (int x = std::max(0, left); x < std::min(static_cast<int>(width), right); ++x)
            {
                const auto* pixel = bgra.data() + (static_cast<size_t>(y) * width + static_cast<size_t>(x)) * 4;
                // The UNORM clear is RGBA(0.10,0.15,0.21,1). Allow rounding.
                if (std::abs(static_cast<int>(pixel[0]) - 54) + std::abs(static_cast<int>(pixel[1]) - 38) +
                    std::abs(static_cast<int>(pixel[2]) - 26) > 9) ++count;
            }
        return count;
    }
    size_t Different(const Pixels& other, int left, int top, int right, int bottom) const
    {
        Require(width == other.width && height == other.height, "Pixel comparison dimensions differ");
        size_t count = 0;
        for (int y = std::max(0, top); y < std::min(static_cast<int>(height), bottom); ++y)
            for (int x = std::max(0, left); x < std::min(static_cast<int>(width), right); ++x)
            {
                const size_t offset = (static_cast<size_t>(y) * width + static_cast<size_t>(x)) * 4;
                if (std::abs(static_cast<int>(bgra[offset]) - other.bgra[offset]) +
                    std::abs(static_cast<int>(bgra[offset + 1]) - other.bgra[offset + 1]) +
                    std::abs(static_cast<int>(bgra[offset + 2]) - other.bgra[offset + 2]) > 20) ++count;
            }
        return count;
    }
    size_t Around(float x, float y, float radius) const
    {
        return Changed(static_cast<int>(std::floor(x - radius)), static_cast<int>(std::floor(y - radius)),
            static_cast<int>(std::ceil(x + radius)), static_cast<int>(std::ceil(y + radius)));
    }
};
void RequireLightPixels(const Pixels& image, const Config& config, bool hudVisible)
{
    const float width = static_cast<float>(image.width), height = static_cast<float>(image.height);
    // Independently calculated pinhole projection for the fixture's vertical60°
    // FOV. Deliberately do not call ProjectWorld: this is a renderer integration
    // check, not a second assertion using the implementation's own result.
    const float focal = height / (2 * std::tan(std::numbers::pi_v<float> / 6));
    const float scale = HudScale(width, height, config, true);
    const float radius = 13 * scale;
    Require(image.Around(width / 2 + focal * 4 / 14, height / 2 - focal / 14, radius) > 10,
        "Projected blue spot marker missing");
    Require(image.Around(width / 2 + focal * 2 / 8, height / 2 + focal * 2 / 8, radius) > 10,
        "Projected lower light marker missing");
    if (!hudVisible)
    {
        Require(image.Around(width / 2, height / 2, radius) > 10, "Projected central light marker missing");
        Require(image.Around(width / 2 - focal * 3 / 12, height / 2 - focal * 2 / 12, radius) > 10,
            "Projected elevated light marker missing");
    }
    const float spotX = width / 2 + focal * 4 / 14, spotY = height / 2 - focal / 14;
    Require(image.Changed(static_cast<int>(spotX - 4 * scale), static_cast<int>(spotY + 16 * scale),
        static_cast<int>(spotX + 4 * scale), static_cast<int>(spotY + 40 * scale)) > 4,
        "Downward spot direction arrow missing");
    if (hudVisible)
        Require(image.Changed(static_cast<int>(36 * scale), static_cast<int>(138 * scale),
            static_cast<int>(514 * scale), static_cast<int>(386 * scale)) > 100,
            "Combined light HUD radar area was not rendered");
}
void RequireNoLightPixels(const Pixels& image)
{
    const float width = static_cast<float>(image.width), height = static_cast<float>(image.height);
    const float focal = height / (2 * std::tan(std::numbers::pi_v<float> / 6));
    Require(image.Around(width / 2, height / 2, 14) == 0 &&
        image.Around(width / 2 + focal * 4 / 14, height / 2 - focal / 14, 14) == 0 &&
        image.Around(width / 2 + focal * 2 / 8, height / 2 + focal * 2 / 8, 14) == 0,
        "Missing/stale/hidden light data retained fullscreen ghosts");
}
Pixels SaveBuffer(Gpu& gpu, ID3D12Resource* buffer, const wchar_t* path)
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
    if (path)
    {
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
    return Pixels{width, desc.Height, std::move(pixels)};
}
}
int wmain(int argc, wchar_t** argv)
{
    try
    {
        Config config; config.details = true;
        Require(!InstallGraphics(config), "Disabled HUD installed graphics hooks");
        const bool noticesOnly = argc > 1 && _wcsicmp(argv[1], L"--notices") == 0;
        const bool lightsOnly = argc > 1 && _wcsicmp(argv[1], L"--lights-only") == 0;
        const bool lightsMode = lightsOnly || (argc > 1 && _wcsicmp(argv[1], L"--lights") == 0);
        config.enabled = !noticesOnly && !lightsOnly;
        config.notifications = noticesOnly;
        config.lightOverlay = lightsMode;
        // Regression control: InitiallyVisible=0 must still allow the runtime
        // F10 state to enable drawing; the immutable startup config stays false.
        config.lightOverlayVisible = !lightsMode;
        config.radar3D = true;
        const int imageArgument = noticesOnly || lightsMode ? 2 : 1;
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
        const auto frame = [&](bool present1, const wchar_t* screenshot, const View* supplied = nullptr, bool inspect = false)
        {
            DXGI_SWAP_CHAIN_DESC1 current{}; Check(chain->GetDesc1(&current));
            auto view = supplied ? *supplied : lightsMode ?
                LightFixture(static_cast<float>(current.Width) / static_cast<float>(current.Height)) : Fixture();
            Publish(std::move(view));
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
            if (screenshot || inspect) return SaveBuffer(gpu, buffer.Get(), screenshot);
            return Pixels{};
        };
        if (lightsMode)
        {
            SetVisibleForTest(false);
            RequireNoLightPixels(frame(false, nullptr, nullptr, true));
            Require(RenderedFrames() == 0, "Initially hidden light overlay submitted a draw");
            SetLightVisibleForTest(true);
            SetVisibleForTest(!lightsOnly);
        }
        Pixels referenceLights;
        for (int i = 0; i < 8; ++i)
        {
            const auto image = frame(i % 2 == 0, i == 7 && argc > imageArgument ? argv[imageArgument] : nullptr,
                nullptr, lightsMode && i == 7);
            if (lightsMode && i == 7)
            {
                RequireLightPixels(image, config, !lightsOnly);
                referenceLights = image;
            }
        }
        Require(RenderedFrames() == 8, "Present/Present1 did not render exactly once per frame");
        std::cout << "PASS D3D12 Present/Present1 and multi-frame resource reuse\n";
        if (lightsMode && !lightsOnly)
        {
            auto flat = LightFixture(960.0f / 720.0f);
            auto records = *flat.sample.renderedLights.records;
            for (auto& record : records) record.position.y = flat.sample.playerPosition->y;
            flat.sample.renderedLights.records = std::make_shared<const std::vector<LightRecord>>(std::move(records));
            const auto flattenedImage = frame(false, nullptr, &flat, true);
            const float scale = HudScale(960, 720, config, true);
            Require(referenceLights.Different(flattenedImage, static_cast<int>(36 * scale), static_cast<int>(138 * scale),
                static_cast<int>(514 * scale), static_cast<int>(386 * scale)) > 10,
                "Light radar ignored record height changes");
            std::cout << "PASS 3D light radar responds to changing light elevations\n";
        }
        const auto before = RenderedFrames();
        Check(chain->Present(0, DXGI_PRESENT_TEST));
        Require(RenderedFrames() == before, "Test-only present changed output");
        SetVisibleForTest(false);
        const auto hiddenHud = frame(false, nullptr, nullptr, lightsMode);
        Require(RenderedFrames() == before + (noticesOnly || lightsMode ? 1 : 0),
            "Independent notices/light markers did not survive hiding the full HUD");
        if (lightsMode)
        {
            RequireLightPixels(hiddenHud, config, false);
            // Isolate each excluded source with the HUD hidden. A legitimate
            // detail card in the mixed scene can otherwise occupy its would-be
            // projection, making a blank-pixel clipping assertion invalid.
            for (const size_t excludedIndex : std::array<size_t,2>{3,4})
            {
                auto excludedOnly = LightFixture(960.0f/720.0f);
                const std::vector<LightRecord> excludedRecord{
                    excludedOnly.sample.renderedLights.records->at(excludedIndex)};
                excludedOnly.sample.renderedLights.records =
                    std::make_shared<const std::vector<LightRecord>>(excludedRecord);
                excludedOnly.sample.renderedLights.publishedRecords = 1u;
                const auto beforeExcluded = RenderedFrames();
                const auto excludedImage = frame(false,nullptr,&excludedOnly,true);
                Require(RenderedFrames() == beforeExcluded+1,"Clipping fixture did not render its active light overlay");
                const float excludedWidth = static_cast<float>(excludedImage.width);
                const float excludedHeight = static_cast<float>(excludedImage.height);
                const float focal = excludedHeight/(2*std::tan(std::numbers::pi_v<float>/6));
                Require(excludedImage.Around(excludedWidth/2+focal/2,excludedHeight/2-focal/4,12) == 0,
                    "An isolated behind-camera or near-plane light produced its screen ghost");
                Require(excludedImage.Changed(0,0,static_cast<int>(excludedImage.width),
                    static_cast<int>(excludedImage.height)-100) == 0,
                    "An excluded-only fixture painted a marker or detail card outside the status legend");
            }
            auto centerOnly = LightFixture(960.0f / 720.0f);
            const std::vector<LightRecord> oneRecord{centerOnly.sample.renderedLights.records->front()};
            centerOnly.sample.renderedLights.records = std::make_shared<const std::vector<LightRecord>>(oneRecord);
            centerOnly.sample.renderedLights.publishedRecords = 1u;
            const auto centerImage = frame(false, nullptr, &centerOnly, true);
            const float centerX = static_cast<float>(centerImage.width) / 2;
            const float centerY = static_cast<float>(centerImage.height) / 2;
            // Allow either right-side or upper placement on compact surfaces.
            // Source rings and their short connector do not reach this region.
            Require(centerImage.Changed(static_cast<int>(centerX + 50), static_cast<int>(centerY - 180),
                static_cast<int>(centerX + 430), static_cast<int>(centerY + 180)) > 100,
                "Selected central light detail card was blocked by the reticle");
            auto grouped = LightFixture(960.0f / 720.0f);
            std::vector<LightRecord> pair{grouped.sample.renderedLights.records->front(),
                grouped.sample.renderedLights.records->back()};
            grouped.sample.renderedLights.records = std::make_shared<const std::vector<LightRecord>>(pair);
            grouped.sample.renderedLights.publishedRecords = 2u;
            const auto groupedImage = frame(false,nullptr,&grouped,true);
            pair[1].colorLinear = Vec3{7.7f,1.7f,.7f}; pair[1].luminanceLinear = 2.9f;
            grouped.sample.renderedLights.records = std::make_shared<const std::vector<LightRecord>>(pair);
            grouped.received = Clock::now();
            const auto changedSecondRow = frame(false,nullptr,&grouped,true);
            Require(groupedImage.Different(changedSecondRow,static_cast<int>(centerX+50),static_cast<int>(centerY-180),
                static_cast<int>(centerX+430),static_cast<int>(centerY+180)) > 5,
                "Grouped central detail card did not render the second contribution's own RGB");
            auto stale = LightFixture(960.0f / 720.0f);
            stale.sample.renderedLights.ageMilliseconds = 650;
            RequireNoLightPixels(frame(false, nullptr, &stale, true));
            auto missing = LightFixture(960.0f / 720.0f);
            missing.sample.renderedLights = LightSummary{};
            RequireNoLightPixels(frame(false, nullptr, &missing, true));
            SetLightVisibleForTest(false);
            const auto beforeOff = RenderedFrames();
            RequireNoLightPixels(frame(false, nullptr, nullptr, true));
            Require(RenderedFrames() == beforeOff, "All UI hidden still submitted an overlay draw");
            SetLightVisibleForTest(true);
            RequireLightPixels(frame(false, nullptr, nullptr, true), config, false);
            std::cout << "PASS projected lights, spot arrow, depth clipping, stale/missing clearing and independent light visibility\n";
        }
        const auto afterHidden = RenderedFrames();
        SetVisibleForTest(!lightsOnly && !noticesOnly);
        Check(chain->ResizeBuffers(2, 1280, 800, DXGI_FORMAT_R8G8B8A8_UNORM, 0));
        MaintainGraphics(); frame(false, nullptr);
        Require(RenderedFrames() == afterHidden + 1, "HUD/notifications did not resume after ResizeBuffers");
        const UINT masks[] = {0, 0}; IUnknown* queues[] = {gpu.queue.Get(), gpu.queue.Get()};
        Check(chain->ResizeBuffers1(2, 960, 720, DXGI_FORMAT_R8G8B8A8_UNORM, 0, masks, queues));
        MaintainGraphics(); frame(true, nullptr);
        Require(RenderedFrames() == afterHidden + 2, "HUD/notifications did not resume after ResizeBuffers1");
        std::cout << "PASS visibility, test-only present, ResizeBuffers and ResizeBuffers1\n";
        Check(chain->ResizeBuffers(2, 3840, 2160, DXGI_FORMAT_R8G8B8A8_UNORM, 0));
        MaintainGraphics();
        const auto largeImage = frame(false, argc > imageArgument + 1 ? argv[imageArgument + 1] : nullptr, nullptr, lightsMode);
        Require(RenderedFrames() == afterHidden + 3, "4K HUD/notifications did not render after resize");
        if (lightsMode) RequireLightPixels(largeImage, config, !lightsOnly);
        std::cout << "PASS 4K HUD rendering after resize with resolution-scaled font atlas\n";
        if (lightsMode && !lightsOnly)
        {
            auto pitched = PitchedLightFixture(3840.0f/2160.0f);
            const auto pitchedImage = frame(false,argc > imageArgument+2 ? argv[imageArgument+2] : nullptr,&pitched,true);
            auto level = pitched;
            level.sample.cameraForward = Vec3{0,0,1}; level.sample.cameraRight = Vec3{1,0,0};
            level.sample.cameraUp = Vec3{0,1,0}; level.sample.pitch = 0.f;
            level.received = Clock::now();
            const auto levelImage = frame(false,nullptr,&level,true);
            const float radarScale = HudScale(3840,2160,config,true);
            Require(pitchedImage.Different(levelImage,static_cast<int>(36*radarScale),static_cast<int>(138*radarScale),
                static_cast<int>(514*radarScale),static_cast<int>(386*radarScale)) > 100,
                "Camera radar frustum ignored pitch and roll while yaw was unchanged");
            auto missingBasis = pitched; missingBasis.sample.cameraRight.reset(); missingBasis.received = Clock::now();
            const auto missingImage = frame(false,nullptr,&missingBasis,true);
            Require(pitchedImage.Different(missingImage,static_cast<int>(36*radarScale),static_cast<int>(138*radarScale),
                static_cast<int>(514*radarScale),static_cast<int>(386*radarScale)) > 100,
                "Camera radar frustum was guessed without a valid camera basis");
            Check(chain->ResizeBuffers(2,800,480,DXGI_FORMAT_R8G8B8A8_UNORM,0));
            MaintainGraphics();
            const auto compactImage = frame(false,argc > imageArgument+3 ? argv[imageArgument+3] : nullptr,nullptr,true);
            Require(compactImage.Changed(440,190,760,310) > 100,
                "Compact combined surface lost its grouped light detail card");
            std::cout << "PASS full-width radar camera pitch/roll, missing-basis clearing and grouped contribution RGB\n";
        }
        Check(gpu.device->GetDeviceRemovedReason());
        DestroyWindow(window);
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
