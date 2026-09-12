// Synthetic production-acquisition controls. WARP copies and observer calls,
// never real hook installation, game memory, or evidence of live game behavior.
#include "../src/sdf_acquire.cpp"
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <iostream>
#include <limits>

namespace test
{
using Microsoft::WRL::ComPtr;
unsigned checks{}, forwarded{}, published{}, cleared{};
std::vector<uint8_t> volume;
std::array<uint8_t, 768> constants{};
std::array<float, 3> camera{};
uint32_t frame{};
uint64_t completed{};
bool bracketed{};

void Check(bool value, const char* message)
{
    ++checks;
    if (!value) { std::cerr << "FAIL " << checks << ": " << message << '\n'; ExitProcess(1); }
}
void Hr(HRESULT result, const char* message)
{
    if (FAILED(result)) std::cerr << std::hex << static_cast<unsigned>(result) << std::dec << ' ';
    Check(SUCCEEDED(result), message);
}
void STDMETHODCALLTYPE ForwardBarrier(ID3D12GraphicsCommandList7* list, UINT count,
    const D3D12_BARRIER_GROUP* groups)
{
    ++forwarded;
    list->Barrier(count, groups);
}
void STDMETHODCALLTYPE CountBarrier(ID3D12GraphicsCommandList7*, UINT, const D3D12_BARRIER_GROUP*)
{ ++forwarded; }
HRESULT STDMETHODCALLTYPE ForwardReset(ID3D12GraphicsCommandList* list,
    ID3D12CommandAllocator* allocator, ID3D12PipelineState* pipeline)
{ return list->Reset(allocator, pipeline); }
HRESULT STDMETHODCALLTYPE ForwardClose(ID3D12GraphicsCommandList* list)
{ return list->Close(); }
HRESULT STDMETHODCALLTYPE SimulateReset(ID3D12GraphicsCommandList*,
    ID3D12CommandAllocator*, ID3D12PipelineState*)
{ return S_OK; }

D3D12_BARRIER_GROUP Group(const D3D12_TEXTURE_BARRIER* barrier, UINT count = 1)
{
    D3D12_BARRIER_GROUP group{};
    group.Type = D3D12_BARRIER_TYPE_TEXTURE;
    group.NumBarriers = count;
    group.pTextureBarriers = barrier;
    return group;
}
D3D12_TEXTURE_BARRIER Release(ID3D12Resource* source)
{
    D3D12_TEXTURE_BARRIER barrier{};
    barrier.pResource = source;
    barrier.SyncBefore = D3D12_BARRIER_SYNC_COMPUTE_SHADING;
    barrier.SyncAfter = D3D12_BARRIER_SYNC_NONE;
    barrier.AccessBefore = D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
    barrier.AccessAfter = D3D12_BARRIER_ACCESS_NO_ACCESS;
    barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
    barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_GENERIC_READ;
    barrier.Subresources.IndexOrFirstMipLevel = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}
void PutFloat(std::array<uint8_t, 768>& bytes, size_t offset, float value)
{ std::memcpy(bytes.data() + offset, &value, sizeof(value)); }
std::array<uint8_t, 768> Constants()
{
    std::array<uint8_t, 768> result{};
    // Poison the unused bytes too: publication must preserve all 768 bytes.
    for (size_t i = 0; i < result.size(); ++i) result[i] = static_cast<uint8_t>((i * 37 + 11) % 256);
    PutFloat(result, 0x10, 1.f / 32);
    PutFloat(result, 0x14, 1.f / 16);
    PutFloat(result, 0x18, 1.f / 32);
    PutFloat(result, 0x1c, 0.f);
    for (unsigned level = 0; level < 8; ++level)
    {
        PutFloat(result, 0x140 + 16 * level, 100.f);
        PutFloat(result, 0x144 + 16 * level, -25.f);
        PutFloat(result, 0x148 + 16 * level, 50.f);
        PutFloat(result, 0x14c + 16 * level, 1.f);
    }
    return result;
}

struct Gpu
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12InfoQueue> info;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList7> list;
    ComPtr<ID3D12CommandAllocator> contextAllocator;
    ComPtr<ID3D12GraphicsCommandList> contextList;
    ComPtr<ID3D12Resource> texture, upload, workloadSink;
    ComPtr<ID3D12Fence> idleFence;
    uint64_t idleValue{};
    D3D12_RESOURCE_DESC description{};
    std::vector<uint8_t> expected;

    explicit Gpu(ID3D12Device* sharedDevice = nullptr, unsigned pattern = 0)
    {
        if (sharedDevice) device = sharedDevice;
        else
        {
            ComPtr<IDXGIFactory4> factory;
            Hr(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "DXGI factory");
            ComPtr<IDXGIAdapter> warp;
            Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "WARP adapter");
            Hr(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "WARP device");
        }
        Hr(device.As(&info), "D3D12 info queue");
        D3D12_COMMAND_QUEUE_DESC queueDescription{};
        queueDescription.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        Hr(device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue)), "compute queue");
        Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&allocator)), "compute allocator");
        Hr(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, allocator.Get(), nullptr,
            IID_PPV_ARGS(&list)), "compute list7");
        Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&contextAllocator)), "context allocator");
        Hr(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, contextAllocator.Get(), nullptr,
            IID_PPV_ARGS(&contextList)), "separate exposure context list");
        Hr(contextList->Close(), "close unused context list");
        Hr(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&idleFence)), "idle fence");

        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        description.Width = 128;
        description.Height = 64;
        description.DepthOrArraySize = 1040;
        description.MipLevels = 1;
        description.Format = DXGI_FORMAT_R16_TYPELESS;
        description.SampleDesc.Count = 1;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        Hr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texture)), "R16 volume");

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rows{};
        UINT64 rowBytes{}, allocationBytes{};
        device->GetCopyableFootprints(&description, 0, 1, 0, &footprint, &rows, &rowBytes, &allocationBytes);
        Check(rows == 64 && rowBytes == 256 && footprint.Footprint.Depth == 1040, "measured R16 upload footprint");
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = allocationBytes;
        buffer.Height = 1;
        buffer.DepthOrArraySize = buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        Hr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)), "upload buffer");
        buffer.Width = 4;
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        Hr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&workloadSink)), "benign workload buffer");
        void* mapped{};
        D3D12_RANGE empty{};
        Hr(upload->Map(0, &empty, &mapped), "map upload");
        std::memset(mapped, 0xa5, static_cast<size_t>(allocationBytes));
        expected.resize(size_t{128} * 64 * 1040 * 2);
        for (size_t z = 0; z < 1040; ++z)
            for (size_t y = 0; y < 64; ++y)
                for (size_t x = 0; x < 256; ++x)
                {
                    const auto value = static_cast<uint8_t>((x * 7 + y * 13 + z * 17 + pattern) % 256);
                    expected[(z * 64 + y) * 256 + x] = value;
                    static_cast<uint8_t*>(mapped)[footprint.Offset + (z * 64 + y) * footprint.Footprint.RowPitch + x] = value;
                }
        upload->Unmap(0, nullptr);

        auto barrier = Release(texture.Get());
        barrier.SyncBefore = D3D12_BARRIER_SYNC_NONE;
        barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
        barrier.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
        barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_DEST;
        barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON;
        barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_DEST;
        auto group = Group(&barrier);
        list->Barrier(1, &group);
        D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
        source.pResource = upload.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;
        destination.pResource = texture.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        barrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
        barrier.SyncAfter = D3D12_BARRIER_SYNC_NONE;
        barrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
        barrier.AccessAfter = D3D12_BARRIER_ACCESS_NO_ACCESS;
        barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_COPY_DEST;
        barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_GENERIC_READ;
        list->Barrier(1, &group);
        Hr(list->Close(), "close upload");
        ExecuteAndWait();
    }

    void WaitIdle()
    {
        Hr(queue->Signal(idleFence.Get(), ++idleValue), "signal idle fence");
        const auto deadline = GetTickCount64() + 8000;
        while (idleFence->GetCompletedValue() < idleValue && GetTickCount64() < deadline) Sleep(1);
        Check(idleFence->GetCompletedValue() >= idleValue, "GPU idle within bound");
    }
    void ExecuteAndWait()
    {
        ID3D12CommandList* submitted[] = {list.Get()};
        queue->ExecuteCommandLists(1, submitted);
        WaitIdle();
    }
    void Reset(bool observed)
    {
        Hr(allocator->Reset(), "reset idle allocator");
        if (observed)
            Hr(cdt::sdf::acquire::ResetHook(list.Get(), allocator.Get(), nullptr), "observed Reset");
        else
            Hr(list->Reset(allocator.Get(), nullptr), "unobserved Reset");
    }
    void AcquireShaderResource(ID3D12Resource* resource = nullptr)
    {
        // Discovery/rejection controls still need actual work in their submitted
        // lists. D3D12 flags barrier-only lists as ineffective synchronization.
        list->CopyBufferRegion(workloadSink.Get(), 0, upload.Get(), 0, 4);
        auto barrier = Release(resource ? resource : texture.Get());
        barrier.SyncBefore = D3D12_BARRIER_SYNC_NONE;
        barrier.SyncAfter = D3D12_BARRIER_SYNC_COMPUTE_SHADING;
        barrier.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
        barrier.AccessAfter = D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
        barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_GENERIC_READ;
        barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
        auto group = Group(&barrier);
        list->Barrier(1, &group);
    }
    void CheckDebug()
    {
        const auto count = info->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 index = 0; index < count; ++index)
        {
            SIZE_T bytes{};
            info->GetMessage(index, nullptr, &bytes);
            std::vector<uint8_t> messageBytes(bytes);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(messageBytes.data());
            Hr(info->GetMessage(index, message, &bytes), "read debug message");
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) std::cerr << message->pDescription << '\n';
            Check(message->Severity > D3D12_MESSAGE_SEVERITY_WARNING, "zero D3D12 debug warnings/errors");
        }
        Hr(device->GetDeviceRemovedReason(), "WARP device healthy");
    }
};
}

namespace cdt::sdf
{
void Publish(std::vector<uint8_t> value, const std::array<uint8_t, 768>& gi,
    const std::array<float, 3>& position, uint32_t contextFrame,
    uint64_t completedTick, bool cpuContextBracketed)
{
    ++test::published;
    test::volume = std::move(value);
    test::constants = gi;
    test::camera = position;
    test::frame = contextFrame;
    test::completed = completedTick;
    test::bracketed = cpuContextBracketed;
}
void Clear() noexcept { ++test::cleared; test::volume.clear(); }
}

namespace test
{
void DescriptorAndTupleControls(Gpu& gpu)
{
    using namespace cdt::sdf::acquire;
    Check(ValidShape(gpu.description), "exact production SDF descriptor accepted");
    auto changed = gpu.description;
    changed.Width = 64;
    Check(!ValidShape(changed), "wrong width rejected");
    changed = gpu.description; changed.Height = 32;
    Check(!ValidShape(changed), "wrong height rejected");
    changed = gpu.description; changed.DepthOrArraySize = 1039;
    Check(!ValidShape(changed), "wrong depth rejected");
    changed = gpu.description; changed.MipLevels = 2;
    Check(!ValidShape(changed), "extra mip rejected");
    changed = gpu.description; changed.Format = DXGI_FORMAT_R16_FLOAT;
    Check(!ValidShape(changed), "typed resource rejected");
    changed = gpu.description; changed.Flags = D3D12_RESOURCE_FLAG_NONE;
    Check(!ValidShape(changed), "non-UAV resource rejected");
    changed = gpu.description; changed.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Check(!ValidShape(changed), "non-volume resource rejected");

    const auto release = Release(gpu.texture.Get());
    Check(MatchesRelease(release), "measured compute release accepted");
    auto bad = release; bad.SyncBefore = D3D12_BARRIER_SYNC_ALL;
    Check(!MatchesRelease(bad), "broad sync is not the measured release");
    bad = release; bad.SyncAfter = D3D12_BARRIER_SYNC_COMPUTE_SHADING;
    Check(!MatchesRelease(bad), "changed destination sync rejected");
    bad = release; bad.AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
    Check(!MatchesRelease(bad), "UAV source access rejected");
    bad = release; bad.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
    Check(!MatchesRelease(bad), "COMMON differs from NO_ACCESS");
    bad = release; bad.LayoutBefore = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
    Check(!MatchesRelease(bad), "changed source layout rejected");
    bad = release; bad.LayoutAfter = D3D12_BARRIER_LAYOUT_COMMON;
    Check(!MatchesRelease(bad), "changed destination layout rejected");
    bad = release; bad.Flags = D3D12_TEXTURE_BARRIER_FLAG_DISCARD;
    Check(!MatchesRelease(bad), "discard release rejected");
    bad = release; bad.Subresources.NumMipLevels = 1;
    Check(!MatchesRelease(bad), "noncanonical all-subresource encoding rejected");
    bad = release; bad.Subresources.IndexOrFirstMipLevel = 0;
    Check(!MatchesRelease(bad), "single subresource is not all subresources");

    const auto gi = Constants();
    Check(SameMapping(gi, gi), "identical mapping accepted");
    auto other = gi; other[0x2e0] ^= 0x80;
    Check(SameMapping(gi, other), "unrelated GI field may change between frames");
    other = gi; PutFloat(other, 0x10, 1.f / 64);
    Check(!SameMapping(gi, other), "inverse extent change rejected");
    for (unsigned level = 0; level < 8; ++level)
    {
        other = gi; PutFloat(other, 0x140 + 16 * level, 101.f);
        Check(!SameMapping(gi, other), "each clipmap center participates in stability gate");
        other = gi; PutFloat(other, 0x14c + 16 * level, 2.f);
        Check(!SameMapping(gi, other), "each clipmap scale participates in stability gate");
    }
}

void StartSynthetic()
{
    using namespace cdt::sdf::acquire;
    state = State{};
    Start();
    // Direct callback invocation tests the observer; no process hooks are installed.
    state.hooksReady = true;
    originalBarrier = ForwardBarrier;
    originalReset = ForwardReset;
    originalClose = ForwardClose;
}
void Discover(Gpu& gpu)
{
    using namespace cdt::sdf::acquire;
    gpu.Reset(false);
    gpu.AcquireShaderResource();
    auto release = Release(gpu.texture.Get());
    auto group = Group(&release);
    const auto before = forwarded;
    BarrierHook(gpu.list.Get(), 1, &group);
    Check(state.phase == Phase::Preparing, "first release defers allocation to Poll");
    Check(forwarded == before + 1, "discovery forwards engine packet once");
    Poll();
    Check(state.phase == Phase::Ready, "deferred readback allocation prepared");
    Hr(gpu.list->Close(), "close discovery recording");
    gpu.ExecuteAndWait();
}
void ForwardRelease(Gpu& gpu)
{
    auto release = Release(gpu.texture.Get());
    auto group = Group(&release);
    cdt::sdf::acquire::BarrierHook(gpu.list.Get(), 1, &group);
}

void RejectedBeforeContexts(Gpu& gpu)
{
    using namespace cdt::sdf::acquire;
    const auto gi = Constants();
    const std::array<float, 3> position{100.f, -25.f, 50.f};
    const auto calls = published;
    // A valid context cannot turn an unobserved Reset into a known generation.
    StartSynthetic();
    Discover(gpu);
    gpu.Reset(false);
    ObserveContext(gpu.contextList.Get(), gi, position, 1, GetTickCount64(), true);
    gpu.AcquireShaderResource();
    ForwardRelease(gpu);
    Check(state.phase == Phase::Ready && !state.generationKnown, "unknown Reset never records");
    Hr(gpu.list->Close(), "close unknown generation");
    gpu.ExecuteAndWait();
    Stop();

    for (unsigned control = 0; control < 4; ++control)
    {
        StartSynthetic();
        Discover(gpu);
        gpu.Reset(true);
        auto cameraPosition = position;
        const auto tick = GetTickCount64();
        if (control == 3) cameraPosition[1] = std::numeric_limits<float>::quiet_NaN();
        const auto contextTick = control == 1 ? tick - 1000 : control == 2 ? tick + 1000 : tick;
        ObserveContext(gpu.contextList.Get(), gi, cameraPosition, 1, contextTick, control != 0);
        gpu.AcquireShaderResource();
        const auto before = forwarded;
        ForwardRelease(gpu);
        Check(state.phase == Phase::Ready, "unstable/stale/future/nonfinite context never records");
        Check(forwarded == before + 1, "rejected context forwards only the original release");
        Poll();
        Check(published == calls, "rejected context never publishes");
        Hr(CloseHook(gpu.list.Get()), "close rejected context");
        gpu.ExecuteAndWait();
        Stop();
    }
}

void RecordCopy(Gpu& gpu, const std::array<uint8_t, 768>& gi, uint32_t contextFrame)
{
    using namespace cdt::sdf::acquire;
    gpu.Reset(true);
    ObserveContext(gpu.contextList.Get(), gi, {100.f, -25.f, 50.f}, contextFrame, GetTickCount64(), true);
    gpu.AcquireShaderResource();
    auto release = Release(gpu.texture.Get());
    const auto originalRelease = release;
    D3D12_GLOBAL_BARRIER unrelated{};
    unrelated.SyncBefore = unrelated.SyncAfter = D3D12_BARRIER_SYNC_COMPUTE_SHADING;
    unrelated.AccessBefore = unrelated.AccessAfter = D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
    D3D12_BARRIER_GROUP unrelatedGroup{};
    unrelatedGroup.Type = D3D12_BARRIER_TYPE_GLOBAL;
    unrelatedGroup.NumBarriers = 1;
    unrelatedGroup.pGlobalBarriers = &unrelated;
    std::array<D3D12_BARRIER_GROUP, 2> packet{unrelatedGroup, Group(&release)};
    const auto unchanged = packet;
    const auto before = forwarded;
    BarrierHook(gpu.list.Get(), static_cast<UINT>(packet.size()), packet.data());
    Check(state.phase == Phase::Recorded, "fresh context and known Reset record one copy");
    Check(forwarded == before + 3, "two injected transitions then one original packet");
    Check(std::memcmp(&release, &originalRelease, sizeof(release)) == 0 &&
        std::memcmp(packet.data(), unchanged.data(), sizeof(packet)) == 0, "original engine barrier packet remains byte-identical");
    Check(state.before.gi == gi && state.before.frame == contextFrame, "recording retains complete preceding GI context");
}

void SubmitCopy(Gpu& gpu)
{
    using namespace cdt::sdf::acquire;
    Hr(CloseHook(gpu.list.Get()), "observed Close");
    Check(state.phase == Phase::Closed, "successful Close gates submission");
    ID3D12CommandList* lists[] = {gpu.list.Get()};
    Submission(gpu.queue.Get(), 1, lists, false);
    Check(state.phase == Phase::Submitting, "matching compute queue begins submission");
    gpu.queue->ExecuteCommandLists(1, lists);
    Submission(gpu.queue.Get(), 1, lists, true);
    Check(state.phase == Phase::WaitingGpu, "submission completion still requires own fence");
}

uint64_t FollowingTick()
{
    const auto recorded = cdt::sdf::acquire::state.releaseTick;
    // GetTickCount64 can have coarser resolution than Sleep(1).
    const auto deadline = recorded + 250;
    while (GetTickCount64() <= recorded && GetTickCount64() < deadline) Sleep(1);
    const auto tick = GetTickCount64();
    Check(tick > recorded, "following observation strictly follows recording");
    return tick;
}

void FencedPublication(Gpu& gpu)
{
    using namespace cdt::sdf::acquire;
    StartSynthetic();
    Discover(gpu);
    const auto beforeGi = Constants();
    const auto calls = published;
    RecordCopy(gpu, beforeGi, 10);
    Poll();
    Check(state.phase == Phase::Recorded && published == calls && state.packed.empty(), "no map or publish before Close/Execute");
    ComPtr<ID3D12Fence> gpuGate;
    Hr(gpu.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gpuGate)), "GPU hold gate");
    Hr(gpu.queue->Wait(gpuGate.Get(), 1), "hold queue before measured submission");
    SubmitCopy(gpu);
    Poll();
    Check(state.phase == Phase::WaitingGpu && state.packed.empty() && published == calls,
        "Execute return cannot expose bytes before GPU fence");
    // A post-submit Reset changes the CPU generation but preserves already queued
    // work. The simulated call avoids reusing a GPU-busy allocator in this control.
    originalReset = SimulateReset;
    Hr(ResetHook(gpu.list.Get(), gpu.allocator.Get(), nullptr), "post-submit Reset observer");
    originalReset = ForwardReset;
    Check(state.phase == Phase::WaitingGpu, "post-submit Reset retains in-flight copy");
    Hr(gpuGate->Signal(1), "release held GPU queue");
    gpu.WaitIdle();
    Poll();
    Check(state.phase == Phase::AwaitContext && published == calls, "GPU completion alone still awaits following CPU context");
    Check(state.packed == gpu.expected, "full R16 volume exact through both bytes of final slice");

    // The recorded pre-context cannot also serve as the required following frame.
    const auto afterTick = FollowingTick();
    ObserveContext(gpu.contextList.Get(), beforeGi, {101.f, -24.f, 51.f}, 10, afterTick, true);
    Poll();
    Check(state.phase == Phase::AwaitContext && published == calls, "same frame cannot bracket itself");
    ObserveContext(gpu.contextList.Get(), beforeGi, {101.f, -24.f, 51.f}, 11, afterTick, false);
    Poll();
    Check(state.phase == Phase::AwaitContext && published == calls, "unstable following context cannot publish");
    auto afterGi = beforeGi;
    afterGi[0x2e0] ^= 0x80; // permitted camera-related data must also survive publication
    const std::array<float, 3> afterCamera{101.f, -24.f, 51.f};
    ObserveContext(gpu.contextList.Get(), afterGi, afterCamera, 11, afterTick, true);
    Poll();
    Check(published == calls + 1 && state.phase == Phase::Ready, "one valid bracket publishes and rearms");
    Check(volume == gpu.expected && constants == afterGi && camera == afterCamera && frame == 11,
        "published bytes, full following GI, camera and frame remain paired");
    Check(bracketed && completed == state.releaseTick && completed <= state.completedTick,
        "publication ages from recording, never rejuvenates a delayed fenced copy");
    Poll();
    Check(published == calls + 1, "completed copy publishes exactly once");
    Stop();
}

void RecenteredMapping(Gpu& gpu)
{
    using namespace cdt::sdf::acquire;
    StartSynthetic();
    Discover(gpu);
    auto gi = Constants();
    const auto calls = published;
    RecordCopy(gpu, gi, 20);
    SubmitCopy(gpu);
    gpu.WaitIdle();
    Poll();
    Check(state.phase == Phase::AwaitContext, "recenter control reaches fenced data");
    PutFloat(gi, 0x140 + 7 * 16, 102.f);
    ObserveContext(gpu.contextList.Get(), gi, {100.f, -25.f, 50.f}, 21, FollowingTick(), true);
    const auto clears = cleared;
    Poll();
    Check(published == calls && cleared == clears + 1 && state.phase == Phase::Ready && state.packed.empty(),
        "changed clipmap mapping clears this copy and allows a later transaction");
    Stop();
}

void FinishCopy(Gpu& gpu, uint32_t nextFrame)
{
    using namespace cdt::sdf::acquire;
    SubmitCopy(gpu);
    gpu.WaitIdle();
    Poll();
    ObserveContext(gpu.contextList.Get(), Constants(), {100.f, -25.f, 50.f}, nextFrame, FollowingTick(), true);
    Poll();
    Check(state.phase == Phase::Ready && volume == gpu.expected && frame == nextFrame,
        "completed replacement copy has exact source bytes and following context");
}
void AgeCompletedCopy(Gpu& gpu)
{
    using namespace cdt::sdf::acquire;
    Check(state.phase == Phase::Ready && state.fence && state.fenceValue &&
        state.fence->GetCompletedValue() >= state.fenceValue, "recovery fixture starts with a real completed GPU transaction");
    // Advance only the fixture's age, keeping the real completed fence and
    // resources. No wall-clock sleeps or production timeout overrides needed.
    state.releaseTick = GetTickCount64() - cdt::sdf::ProductionMaximumAgeMilliseconds - 1;
    state.lastCompletedTick = state.releaseTick + 1;
    ObserveContext(gpu.contextList.Get(), Constants(), {100.f, -25.f, 50.f}, 501, GetTickCount64(), true);
}
void ReleaseWithoutCopy(Gpu& gpu)
{
    gpu.Reset(false);
    gpu.AcquireShaderResource();
    ForwardRelease(gpu);
    Hr(gpu.list->Close(), "close replacement discovery/rejection");
    gpu.ExecuteAndWait();
}

void CompletedBindingReplacement(Gpu& gpu)
{
    using namespace cdt::sdf::acquire;
    for (unsigned mode = 0; mode < 3; ++mode)
    {
        Gpu replacement(gpu.device.Get(), 29);
        if (mode == 0) // Retired list, same texture.
        { replacement.texture = gpu.texture; replacement.expected = gpu.expected; }
        else if (mode == 1) // Retired texture, same list and allocator.
        { replacement.list = gpu.list; replacement.allocator = gpu.allocator; }
        StartSynthetic();
        Discover(gpu);
        RecordCopy(gpu, Constants(), 500);
        FinishCopy(gpu, 501);
        const auto* oldList = state.list.Get();
        const auto* oldSource = state.source.Get();
        const auto* oldDestination = state.destination.Get();
        const auto* oldFence = state.fence.Get();
        const auto calls = published;

        ReleaseWithoutCopy(replacement);
        Check(state.phase == Phase::Ready && state.list.Get() == oldList && state.source.Get() == oldSource,
            "fresh completed binding must not follow a different list/resource");
        AgeCompletedCopy(gpu);
        Check(CanReplaceCompletedBinding(GetTickCount64()), "stale completed fixture permits bounded replacement");
        ReleaseWithoutCopy(replacement);
        Check(state.phase == Phase::Preparing && state.list.Get() == replacement.list.Get() &&
            state.source.Get() == replacement.texture.Get() && !state.generationKnown &&
            state.destination.Get() == oldDestination && state.fence.Get() == oldFence && published == calls,
            "unique same-device replacement only discovers; completed readback retained until preparation");
        Poll();
        Check(state.phase == Phase::Ready && !state.generationKnown && !state.fenceValue && !state.lastCompletedTick &&
            state.destination.Get() != oldDestination && state.fence.Get() != oldFence,
            "replacement prepares fresh readback/fence without inheriting the old Reset proof");
        ReleaseWithoutCopy(replacement);
        Check(state.phase == Phase::Ready && !state.generationKnown && !state.fenceValue && published == calls,
            "unobserved Reset on new binding cannot record a copy");
        RecordCopy(replacement, Constants(), 502);
        FinishCopy(replacement, 503);
        Check(published == calls + 1, "replacement publishes once after new observed Reset/Close/Submit/fence/context");
        Stop();
        state = State{};
    }
}

void AmbiguousAndDeviceMismatchReplacement(Gpu& gpu)
{
    using namespace cdt::sdf::acquire;
    Gpu replacement(gpu.device.Get(), 37);
    StartSynthetic();
    Discover(gpu);
    RecordCopy(gpu, Constants(), 600);
    FinishCopy(gpu, 601);
    AgeCompletedCopy(gpu);
    const auto* oldList = state.list.Get();
    const auto* oldSource = state.source.Get();
    replacement.Reset(false);
    replacement.AcquireShaderResource();
    replacement.AcquireShaderResource(gpu.texture.Get());
    std::array<D3D12_TEXTURE_BARRIER, 2> releases{Release(replacement.texture.Get()), Release(gpu.texture.Get())};
    auto group = Group(releases.data(), 2);
    const auto forwards = forwarded;
    BarrierHook(replacement.list.Get(), 1, &group);
    Check(state.phase == Phase::Ready && state.list.Get() == oldList && state.source.Get() == oldSource &&
        forwarded == forwards + 1, "two exact release candidates cannot rebind or inject a copy");
    Hr(replacement.list->Close(), "close ambiguous release");
    replacement.ExecuteAndWait();

    // D3D12CreateDevice for the same WARP adapter shares a device identity.
    // Use another real COM object's identity as the deliberately mismatched
    // verified owner instead of claiming a second WARP construction is foreign.
    const auto verifiedDevice = state.deviceIdentity;
    ComPtr<IUnknown> mismatchedIdentity;
    Hr(replacement.queue.As(&mismatchedIdentity), "different COM identity for device rejection control");
    Check(mismatchedIdentity.Get() != verifiedDevice.Get(), "device mismatch fixture actually differs");
    state.deviceIdentity = mismatchedIdentity;
    AgeCompletedCopy(gpu);
    ReleaseWithoutCopy(replacement);
    Check(state.phase == Phase::Ready && state.list.Get() == oldList && state.source.Get() == oldSource &&
        state.deviceIdentity.Get() == mismatchedIdentity.Get(),
        "exact-shaped release whose device differs from verified owner cannot replace binding");
    state.deviceIdentity = verifiedDevice;
    Stop();
    state = State{};
}

void ReplacementPacketAndBudgetControls(Gpu& gpu)
{
    using namespace cdt::sdf::acquire;
    Gpu replacement(gpu.device.Get(), 39);
    StartSynthetic();
    Discover(gpu);
    RecordCopy(gpu, Constants(), 650);
    FinishCopy(gpu, 651);
    const auto* oldList = state.list.Get();
    const auto* oldSource = state.source.Get();
    const auto* oldDestination = state.destination.Get();
    const auto* oldFence = state.fence.Get();
    const auto* oldDevice = state.deviceIdentity.Get();
    const auto* oldQueue = state.queue.Get();
    const auto ownersUnchanged = [&] {
        return state.list.Get() == oldList && state.source.Get() == oldSource &&
            state.destination.Get() == oldDestination && state.fence.Get() == oldFence &&
            state.deviceIdentity.Get() == oldDevice && state.queue.Get() == oldQueue;
    };
    for (bool known : {false, true})
        for (unsigned mode = 0; mode < 3; ++mode)
        {
            AgeCompletedCopy(gpu);
            auto& candidate = known ? gpu : replacement;
            candidate.Reset(known);
            candidate.AcquireShaderResource();
            std::array<D3D12_TEXTURE_BARRIER, 2> barriers{
                Release(candidate.texture.Get()), Release(candidate.texture.Get())};
            barriers[1].SyncBefore = D3D12_BARRIER_SYNC_ALL;
            std::array<D3D12_BARRIER_GROUP, 2> groups{Group(barriers.data()), Group(nullptr)};
            if (mode == 0) groups[0].NumBarriers = 2;
            if (mode == 2) groups[1] = Group(barriers.data(), 4097);
            const auto forwards = forwarded;
            // Malformed packets must never reach the real runtime. Exercise
            // observer rejection with real owners, then submit the one legal
            // release directly so the GPU texture retains its normal layout.
            originalBarrier = CountBarrier;
            BarrierHook(candidate.list.Get(), mode == 0 ? 1 : 2, groups.data());
            originalBarrier = ForwardBarrier;
            Check(state.phase == Phase::Ready && ownersUnchanged() && forwarded == forwards + 1,
                "mixed same-resource/invalid/oversized packets retain every completed binding owner");
            auto legal = Group(barriers.data());
            candidate.list->Barrier(1, &legal);
            Hr(CloseHook(candidate.list.Get()), "close rejected candidate packet");
            candidate.ExecuteAndWait();
        }
    AgeCompletedCopy(gpu);
    state.inspections = 20000;
    ReleaseWithoutCopy(replacement);
    Check(state.phase == Phase::Ready && ownersUnchanged() && state.inspections == 20000,
        "exhausted discovery budget rejects replacement without changing completed owners");
    RecordCopy(gpu, Constants(), 652);
    Check(state.phase == Phase::Recorded && ownersUnchanged() && state.inspections == 20000,
        "returning known binding still records after replacement discovery budget is exhausted");
    FinishCopy(gpu, 653);
    Stop();
    state = State{};
}

void PendingAndFailedReplacement(Gpu& gpu)
{
    using namespace cdt::sdf::acquire;
    Gpu replacement(gpu.device.Get(), 41);
    StartSynthetic();
    Discover(gpu);
    RecordCopy(gpu, Constants(), 700);
    FinishCopy(gpu, 701);
    AgeCompletedCopy(gpu);
    const auto savedPhase = state.phase;
    for (const auto phase : {Phase::Idle, Phase::Preparing, Phase::Recorded, Phase::Closed,
        Phase::Submitting, Phase::WaitingGpu, Phase::AwaitContext, Phase::Failed})
    {
        state.phase = phase;
        Check(!CanReplaceCompletedBinding(GetTickCount64()), "replacement forbidden outside completed Ready phase");
    }
    state.phase = savedPhase;
    for (auto* pending : {&state.preparing, &state.mapping, &state.resetPending, &state.closePending})
    {
        *pending = true;
        Check(!CanReplaceCompletedBinding(GetTickCount64()), "pending CPU lifecycle work blocks replacement");
        *pending = false;
    }
    ++state.fenceValue;
    Check(!CanReplaceCompletedBinding(GetTickCount64()), "unsignalled completed-fence claim cannot rebind");
    const auto* unsignalledDestination = state.destination.Get();
    const auto* unsignalledFence = state.fence.Get();
    ReleaseWithoutCopy(replacement);
    Check(state.phase == Phase::Ready && state.list.Get() == gpu.list.Get() &&
        state.source.Get() == gpu.texture.Get() && state.destination.Get() == unsignalledDestination &&
        state.fence.Get() == unsignalledFence, "unsignalled fence retains original binding on a real replacement release");
    --state.fenceValue;

    RecordCopy(gpu, Constants(), 702);
    ComPtr<ID3D12Fence> hold;
    Hr(gpu.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&hold)), "replacement GPU hold fence");
    Hr(gpu.queue->Wait(hold.Get(), 1), "hold pending original queue");
    SubmitCopy(gpu);
    const auto* pendingDestination = state.destination.Get();
    replacement.Reset(false);
    replacement.AcquireShaderResource();
    ForwardRelease(replacement);
    Hr(replacement.list->Close(), "close ignored release while original pending");
    Check(state.phase == Phase::WaitingGpu && state.list.Get() == gpu.list.Get() &&
        state.source.Get() == gpu.texture.Get() && state.destination.Get() == pendingDestination,
        "real GPU-pending transaction keeps original list/resource/readback despite replacement release");
    Hr(hold->Signal(1), "release original pending queue");
    gpu.WaitIdle();
    replacement.ExecuteAndWait();
    Poll();
    ObserveContext(gpu.contextList.Get(), Constants(), {100.f, -25.f, 50.f}, 703, FollowingTick(), true);
    Poll();
    Check(state.phase == Phase::Ready, "original pending copy completes normally after ignored replacement");

    AgeCompletedCopy(gpu);
    const auto* completedDestination = state.destination.Get();
    const auto* completedFence = state.fence.Get();
    ReleaseWithoutCopy(replacement);
    Check(state.phase == Phase::Preparing, "valid replacement enters deferred preparation");
    // Force preparation validation failure without releasing or modifying any
    // GPU resource. The old completed readback/fence must remain quarantined.
    state.source = replacement.workloadSink;
    Poll();
    Check(state.phase == Phase::Failed && state.failure && std::strcmp(state.failure, "readback-preparation-failed") == 0 &&
        state.destination.Get() == completedDestination && state.fence.Get() == completedFence,
        "failed replacement preparation preserves prior completed resources");
    const auto* failedSource = state.source.Get();
    ReleaseWithoutCopy(replacement);
    Check(state.phase == Phase::Failed && state.source.Get() == failedSource &&
        state.destination.Get() == completedDestination && state.fence.Get() == completedFence,
        "later exact release cannot reset Failed quarantine");
    Stop();
    state = State{};
}

void IllegalReset(Gpu& gpu)
{
    using namespace cdt::sdf::acquire;
    StartSynthetic();
    Discover(gpu);
    const auto calls = published;
    RecordCopy(gpu, Constants(), 30);
    originalReset = SimulateReset;
    Hr(ResetHook(gpu.list.Get(), gpu.allocator.Get(), nullptr), "pre-submit Reset observer");
    originalReset = ForwardReset;
    Check(state.phase == Phase::Failed && state.failure &&
        std::strcmp(state.failure, "list-reset-before-confirmed-submit") == 0, "Reset before confirmed submission fails closed");
    Poll();
    Check(state.phase == Phase::Failed && published == calls && state.packed.empty(), "failed generation cannot map or publish");
    // These commands never reached a queue. Close and discard that recording;
    // the real texture still has the GENERIC_READ state from discovery.
    Hr(gpu.list->Close(), "close discarded recording");
    gpu.Reset(false);
    Hr(gpu.list->Close(), "close empty replacement recording");
    gpu.WaitIdle();
    Stop();
}
}

int main()
{
    test::ComPtr<ID3D12Debug> debug;
    test::Hr(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)), "debug layer required for GPU proof");
    debug->EnableDebugLayer();
    test::Gpu gpu;
    test::DescriptorAndTupleControls(gpu);
    test::RejectedBeforeContexts(gpu);
    test::FencedPublication(gpu);
    test::RecenteredMapping(gpu);
    test::CompletedBindingReplacement(gpu);
    test::AmbiguousAndDeviceMismatchReplacement(gpu);
    test::ReplacementPacketAndBudgetControls(gpu);
    test::PendingAndFailedReplacement(gpu);
    test::IllegalReset(gpu);
    gpu.CheckDebug();
    // Every submitted list is idle, and the final rejected recording was discarded.
    // Production deliberately retains uncertain resources; this harness knows its queues.
    cdt::sdf::acquire::state = cdt::sdf::acquire::State{};
    std::cout << "PASS " << test::checks << " production SDF WARP/debug-layer controls; byte-exact R16, "
        "CPU context brackets, fence and Reset gates. Synthetic, not game evidence.\n";
}
