#include "sdf_acquire.h"
#include "sdf_visibility.h"
#include <MinHook.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

namespace cdt::sdf::acquire
{
namespace
{
using Microsoft::WRL::ComPtr;
using BarrierFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList7*, UINT, const D3D12_BARRIER_GROUP*);
using ResetFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
using CloseFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*);
BarrierFn originalBarrier{};
ResetFn originalReset{};
CloseFn originalClose{};
constexpr UINT Width = 128, Height = 64, Depth = 1040;
constexpr size_t RowBytes = Width * 2, VolumeBytes = RowBytes * Height * Depth;
constexpr std::uint64_t IntervalMs = 500, ContextAgeMs = 750, GpuTimeoutMs = 5000;
enum class Phase { Idle, Preparing, Ready, Recorded, Closed, Submitting, WaitingGpu, AwaitContext, Failed };
struct Context
{
    std::array<std::uint8_t, 768> gi{};
    std::array<float, 3> camera{};
    std::uint32_t frame{};
    std::uint64_t tick{};
    bool valid{};
};
struct State
{
    bool started{}, enabled{}, hooksReady{}, installing{}, preparing{}, mapping{};
    std::array<void*, 3> candidates{}, targets{};
    Phase phase = Phase::Idle;
    const char* failure{};
    Context latest{}, before{};
    ComPtr<ID3D12GraphicsCommandList7> list;
    ComPtr<ID3D12Resource> source, destination;
    ComPtr<ID3D12Fence> fence;
    ComPtr<IUnknown> deviceIdentity;
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    std::vector<std::uint8_t> packed;
    std::uint64_t generation{}, recordedGeneration{}, nextFence{}, fenceValue{};
    std::uint64_t releaseTick{}, submitTick{}, completedTick{}, lastCompletedTick{};
    bool generationKnown{}, resetPending{}, closed{}, closePending{};
    DWORD recordingThread{}, submissionThread{};
    unsigned inspections{};
};
// One process-lived owner: a failed or interrupted submission can still refer to
// its resources. Neither Stop nor a timeout frees or reuses that pending copy.
State& state = *new State;
std::mutex gate;

bool ValidShape(const D3D12_RESOURCE_DESC& desc)
{
    return desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D && desc.Width == Width &&
        desc.Height == Height && desc.DepthOrArraySize == Depth && desc.MipLevels == 1 &&
        desc.Format == DXGI_FORMAT_R16_TYPELESS && desc.SampleDesc.Count == 1 &&
        desc.SampleDesc.Quality == 0 && (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
}
bool MatchesRelease(const D3D12_TEXTURE_BARRIER& b)
{
    const auto& s = b.Subresources;
    return b.pResource && b.SyncBefore == D3D12_BARRIER_SYNC_COMPUTE_SHADING &&
        b.SyncAfter == D3D12_BARRIER_SYNC_NONE && b.AccessBefore == D3D12_BARRIER_ACCESS_SHADER_RESOURCE &&
        b.AccessAfter == D3D12_BARRIER_ACCESS_NO_ACCESS && b.LayoutBefore == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE &&
        b.LayoutAfter == D3D12_BARRIER_LAYOUT_GENERIC_READ && b.Flags == D3D12_TEXTURE_BARRIER_FLAG_NONE &&
        s.IndexOrFirstMipLevel == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES && s.NumMipLevels == 0 &&
        s.FirstArraySlice == 0 && s.NumArraySlices == 0 && s.FirstPlane == 0 && s.NumPlanes == 0;
}
float Lane(const std::array<std::uint8_t, 768>& gi, size_t offset)
{
    float value{}; std::memcpy(&value, gi.data() + offset, sizeof(value)); return value;
}
bool ValidContext(const std::array<std::uint8_t, 768>& gi, const std::array<float, 3>& camera)
{
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        const float inverse = Lane(gi, 0x10 + axis * 4);
        if (!(inverse > 0 && inverse <= 1) || !std::isfinite(camera[axis])) return false;
    }
    for (unsigned level = 0; level < 8; ++level)
    {
        const float scale = Lane(gi, 0x140 + level * 16 + 12);
        if (!(scale > 0 && scale <= 1e4f)) return false;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            const float origin = Lane(gi, 0x140 + level * 16 + axis * 4);
            if (!std::isfinite(origin) || std::fabs(origin) >= 1e8f) return false;
        }
    }
    return true;
}
bool SameMapping(const std::array<std::uint8_t, 768>& a, const std::array<std::uint8_t, 768>& b)
{
    // Camera UV/wrapped position may move. The toroidal world mapping and each
    // non-aliasing clipmap window must remain the same across recording.
    return std::memcmp(a.data() + 0x10, b.data() + 0x10, 12) == 0 &&
        std::memcmp(a.data() + 0x140, b.data() + 0x140, 8 * 16) == 0;
}
bool Tracked(ID3D12GraphicsCommandList* list) { return list && list == state.list.Get(); }
void Fail(const char* reason)
{
    state.failure = reason; state.phase = Phase::Failed;
    sdf::Clear();
}

void STDMETHODCALLTYPE BarrierHook(ID3D12GraphicsCommandList7* list, UINT count, const D3D12_BARRIER_GROUP* groups)
{
    {
        std::lock_guard guard(gate);
        if (state.enabled && state.hooksReady && state.phase != Phase::Failed && list && groups &&
            count <= 64 && list->GetType() == D3D12_COMMAND_LIST_TYPE_COMPUTE)
        {
            const D3D12_TEXTURE_BARRIER* match{};
            unsigned hits{};
            for (UINT group = 0; group < count; ++group)
            {
                const auto& packet = groups[group];
                if (packet.Type != D3D12_BARRIER_TYPE_TEXTURE || packet.NumBarriers > 4096 ||
                    !packet.pTextureBarriers) continue;
                for (UINT i = 0; i < packet.NumBarriers; ++i)
                {
                    const auto& b = packet.pTextureBarriers[i];
                    if (state.source)
                    {
                        if (b.pResource == state.source.Get()) { match = &b; ++hits; }
                    }
                    else if (MatchesRelease(b) && state.inspections < 20000)
                    {
                        ++state.inspections;
                        if (ValidShape(b.pResource->GetDesc())) { match = &b; ++hits; }
                    }
                }
            }
            if (hits == 1 && MatchesRelease(*match))
            {
                if (state.phase == Phase::Idle)
                {
                    state.source = match->pResource; state.list = list;
                    state.phase = Phase::Preparing;
                }
                const auto now = GetTickCount64();
                if (state.phase == Phase::Ready && Tracked(list) && state.generationKnown &&
                    !state.resetPending && !state.closed && !state.closePending && state.latest.valid &&
                    now >= state.latest.tick && now - state.latest.tick <= ContextAgeMs &&
                    (!state.lastCompletedTick || now - state.lastCompletedTick >= IntervalMs))
                {
                    state.before = state.latest;
                    auto before = *match;
                    before.SyncAfter = D3D12_BARRIER_SYNC_COPY;
                    before.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
                    before.LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
                    D3D12_BARRIER_GROUP packet{};
                    packet.Type = D3D12_BARRIER_TYPE_TEXTURE; packet.NumBarriers = 1;
                    packet.pTextureBarriers = &before; originalBarrier(list, 1, &packet);
                    D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
                    dst.pResource = state.destination.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    dst.PlacedFootprint = state.footprint;
                    src.pResource = state.source.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                    auto restore = before;
                    restore.SyncBefore = D3D12_BARRIER_SYNC_COPY;
                    restore.SyncAfter = D3D12_BARRIER_SYNC_COMPUTE_SHADING;
                    restore.AccessBefore = D3D12_BARRIER_ACCESS_COPY_SOURCE;
                    restore.AccessAfter = D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
                    restore.LayoutBefore = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
                    restore.LayoutAfter = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
                    packet.pTextureBarriers = &restore; originalBarrier(list, 1, &packet);
                    state.releaseTick = now; state.recordedGeneration = state.generation;
                    state.recordingThread = GetCurrentThreadId(); state.phase = Phase::Recorded;
                }
            }
        }
    }
    originalBarrier(list, count, groups);
}
HRESULT STDMETHODCALLTYPE ResetHook(ID3D12GraphicsCommandList* list, ID3D12CommandAllocator* allocator, ID3D12PipelineState* pipeline)
{
    {
        std::lock_guard guard(gate);
        if (Tracked(list))
        {
            if (state.phase == Phase::Recorded || state.phase == Phase::Closed || state.phase == Phase::Submitting)
                Fail("list-reset-before-confirmed-submit");
            state.generationKnown = false; state.resetPending = true;
            state.closed = false; state.closePending = false;
        }
    }
    const auto hr = originalReset(list, allocator, pipeline);
    {
        std::lock_guard guard(gate);
        if (Tracked(list))
        {
            state.generationKnown = state.resetPending && SUCCEEDED(hr);
            state.resetPending = false; ++state.generation;
        }
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE CloseHook(ID3D12GraphicsCommandList* list)
{
    { std::lock_guard guard(gate); if (Tracked(list)) state.closePending = true; }
    const auto hr = originalClose(list);
    {
        std::lock_guard guard(gate);
        if (Tracked(list))
        {
            state.closed = state.closePending && SUCCEEDED(hr); state.closePending = false;
            if (state.phase == Phase::Recorded)
            {
                if (!state.closed) Fail("list-close-failed");
                else state.phase = Phase::Closed;
            }
        }
    }
    return hr;
}
void InstallHooks()
{
    std::array<void*, 3> targets{};
    {
        std::lock_guard guard(gate);
        if (!state.enabled || state.hooksReady || state.installing || state.phase == Phase::Failed ||
            !state.candidates[0]) return;
        state.installing = true; targets = state.candidates;
    }
    const std::array<void*, 3> callbacks{reinterpret_cast<void*>(BarrierHook),
        reinterpret_cast<void*>(CloseHook), reinterpret_cast<void*>(ResetHook)};
    const std::array<void**, 3> originals{reinterpret_cast<void**>(&originalBarrier),
        reinterpret_cast<void**>(&originalClose), reinterpret_cast<void**>(&originalReset)};
    std::array<bool, 3> created{};
    bool ok = true;
    for (size_t i = 0; i < targets.size() && ok; ++i)
    {
        created[i] = MH_CreateHook(targets[i], callbacks[i], originals[i]) == MH_OK;
        ok = created[i] && MH_EnableHook(targets[i]) == MH_OK;
    }
    if (!ok)
        for (size_t i = 0; i < targets.size(); ++i) if (created[i]) MH_DisableHook(targets[i]);
    bool stopped{};
    {
        std::lock_guard guard(gate);
        state.installing = false;
        if (ok)
        {
            state.targets = targets; state.hooksReady = state.enabled;
            stopped = !state.enabled;
        }
        else Fail("native-hook-installation-failed");
    }
    if (stopped) for (void* target : targets) MH_DisableHook(target);
}
void Prepare()
{
    ComPtr<ID3D12Resource> source;
    ComPtr<ID3D12GraphicsCommandList7> list;
    {
        std::lock_guard guard(gate);
        if (!state.enabled || state.phase != Phase::Preparing || state.preparing) return;
        state.preparing = true; source = state.source; list = state.list;
    }
    ComPtr<ID3D12Device> device, listDevice;
    ComPtr<IUnknown> identity, listIdentity;
    ComPtr<ID3D12Resource> destination;
    ComPtr<ID3D12Fence> fence;
    auto desc = source->GetDesc();
    HRESULT hr = ValidShape(desc) ? S_OK : E_INVALIDARG;
    if (SUCCEEDED(hr)) hr = source->GetDevice(IID_PPV_ARGS(&device));
    if (SUCCEEDED(hr)) hr = list->GetDevice(IID_PPV_ARGS(&listDevice));
    if (SUCCEEDED(hr)) hr = device.As(&identity);
    if (SUCCEEDED(hr)) hr = listDevice.As(&listIdentity);
    if (SUCCEEDED(hr) && (identity.Get() != listIdentity.Get() || device->GetNodeCount() != 1)) hr = E_INVALIDARG;
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 options{};
    if (SUCCEEDED(hr)) hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options, sizeof(options));
    if (SUCCEEDED(hr) && !options.EnhancedBarriersSupported) hr = E_NOTIMPL;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows{}; UINT64 rowBytes{}, bytes{};
    if (SUCCEEDED(hr))
    {
        device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowBytes, &bytes);
        if (rows != Height || rowBytes != RowBytes || footprint.Footprint.Width != Width ||
            footprint.Footprint.Height != Height || footprint.Footprint.Depth != Depth ||
            footprint.Footprint.Format != DXGI_FORMAT_R16_TYPELESS || footprint.Footprint.RowPitch < RowBytes ||
            bytes < VolumeBytes || bytes > 18u * 1024 * 1024) hr = E_INVALIDARG;
    }
    if (SUCCEEDED(hr))
    {
        D3D12_RESOURCE_DESC buffer{}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = bytes; buffer.Height = 1; buffer.DepthOrArraySize = 1; buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
        heap.CreationNodeMask = 1; heap.VisibleNodeMask = 1;
        hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&destination));
        if (SUCCEEDED(hr)) hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    }
    std::lock_guard guard(gate);
    state.preparing = false;
    if (!state.enabled || state.phase != Phase::Preparing) return;
    if (FAILED(hr)) { Fail("readback-preparation-failed"); return; }
    state.destination = destination; state.fence = fence; state.deviceIdentity = identity;
    state.footprint = footprint; state.phase = Phase::Ready;
}
}

void Start()
{
    std::lock_guard guard(gate);
    if (state.started) return;
    state.started = true; state.enabled = true;
}
void ObserveContext(ID3D12GraphicsCommandList* list, const std::array<std::uint8_t, 768>& gi,
    const std::array<float, 3>& camera, std::uint32_t frame, std::uint64_t tick, bool stable)
{
    ComPtr<ID3D12GraphicsCommandList7> list7;
    if (!list || FAILED(list->QueryInterface(IID_PPV_ARGS(&list7)))) return;
    auto** table = *reinterpret_cast<void***>(list7.Get());
    const std::array<void*, 3> methods{table[80], table[9], table[10]};
    std::lock_guard guard(gate);
    if (!state.enabled || state.phase == Phase::Failed) return;
    if (!std::all_of(methods.begin(), methods.end(), [](void* p) { return p != nullptr; }) ||
        (state.hooksReady && state.targets[0] && methods != state.targets))
    { state.latest.valid = false; return; }
    if (tick < state.latest.tick) return;
    state.candidates = methods;
    state.latest = {gi, camera, frame, tick, stable && tick && ValidContext(gi, camera)};
}
void Submission(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists, bool after)
{
    std::lock_guard guard(gate);
    if (!state.enabled || (state.phase != Phase::Closed && state.phase != Phase::Submitting)) return;
    if (!lists || count > 1024) { Fail("submission-list-bound"); return; }
    unsigned hits{};
    for (UINT i = 0; i < count; ++i) if (lists[i] == state.list.Get()) ++hits;
    if (!hits) return;
    if (!queue || hits != 1 || !state.generationKnown || !state.closed ||
        state.generation != state.recordedGeneration) { Fail("submission-list-generation-mismatch"); return; }
    if (!after)
    {
        if (state.phase != Phase::Closed) { Fail("overlapping-submission"); return; }
        ComPtr<ID3D12Device> device; ComPtr<IUnknown> identity;
        if (FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) || FAILED(device.As(&identity)) ||
            identity.Get() != state.deviceIdentity.Get() || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_COMPUTE)
        { Fail("submission-device-or-queue-mismatch"); return; }
        state.queue = queue; state.submissionThread = GetCurrentThreadId(); state.phase = Phase::Submitting;
    }
    else
    {
        if (state.phase != Phase::Submitting || queue != state.queue.Get() ||
            state.submissionThread != GetCurrentThreadId()) { Fail("submission-end-mismatch"); return; }
        state.fenceValue = ++state.nextFence;
        if (FAILED(queue->Signal(state.fence.Get(), state.fenceValue))) { Fail("queue-signal-failed"); return; }
        state.submitTick = GetTickCount64(); state.phase = Phase::WaitingGpu;
    }
}
void Poll()
{
    InstallHooks(); Prepare();
    ComPtr<ID3D12Resource> destination;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    {
        std::lock_guard guard(gate);
        if (!state.enabled || state.phase == Phase::Failed) return;
        const auto now = GetTickCount64();
        if ((state.phase == Phase::Recorded || state.phase == Phase::Closed || state.phase == Phase::Submitting) &&
            now - state.releaseTick > GpuTimeoutMs) { Fail("close-or-submit-timeout"); return; }
        if (state.phase == Phase::WaitingGpu)
        {
            const auto completed = state.fence->GetCompletedValue();
            if (completed == std::numeric_limits<std::uint64_t>::max()) { Fail("device-removed"); return; }
            if (completed >= state.fenceValue)
            {
                state.completedTick = now; state.phase = Phase::AwaitContext; state.mapping = true;
                destination = state.destination; footprint = state.footprint;
            }
            else if (now - state.submitTick > GpuTimeoutMs) { Fail("gpu-fence-timeout"); return; }
        }
    }
    if (destination)
    {
        void* pointer{};
        const D3D12_RANGE range{0, static_cast<SIZE_T>(destination->GetDesc().Width)};
        const auto hr = destination->Map(0, &range, &pointer);
        std::vector<std::uint8_t> packed;
        bool copied = false;
        if (SUCCEEDED(hr))
        {
            try
            {
                packed.resize(VolumeBytes);
                const auto* data = static_cast<const std::uint8_t*>(pointer) + footprint.Offset;
                for (size_t row = 0; row < size_t{Height} * Depth; ++row)
                    std::memcpy(packed.data() + row * RowBytes, data + row * footprint.Footprint.RowPitch, RowBytes);
                copied = true;
            }
            catch (...) { copied = false; }
            const D3D12_RANGE noWrites{}; destination->Unmap(0, &noWrites);
        }
        std::lock_guard guard(gate);
        state.mapping = false;
        if (!state.enabled || state.phase != Phase::AwaitContext) return;
        if (!copied) { Fail("readback-map-failed"); return; }
        state.packed = std::move(packed);
    }
    std::vector<std::uint8_t> volume;
    Context context{};
    std::uint64_t capturedTick{};
    {
        std::lock_guard guard(gate);
        if (!state.enabled || state.phase != Phase::AwaitContext || state.mapping || state.packed.empty()) return;
        const auto& after = state.latest;
        const auto now = GetTickCount64();
        const bool bracketed = state.before.valid && after.valid && state.before.tick <= state.releaseTick &&
            after.tick > state.releaseTick && state.before.frame != after.frame;
        if (bracketed && now >= after.tick && now - after.tick <= ContextAgeMs &&
            now - state.releaseTick <= 2000 && SameMapping(state.before.gi, after.gi))
        {
            volume = std::move(state.packed); context = after; capturedTick = state.releaseTick;
            state.lastCompletedTick = state.completedTick; state.phase = Phase::Ready;
        }
        else if (bracketed || now - state.releaseTick > 2000)
        {
            // A recentered clipmap or absent following observation invalidates
            // this pair, not the next transaction. GPU completion is already proven.
            state.packed.clear(); state.lastCompletedTick = state.completedTick;
            state.phase = Phase::Ready; sdf::Clear();
        }
    }
    if (!volume.empty())
    {
        std::lock_guard guard(gate);
        if (state.enabled)
            // Freshness starts when the copy was recorded, not when a delayed
            // GPU fence or polling thread finally allowed publication.
            sdf::Publish(std::move(volume), context.gi, context.camera, context.frame, capturedTick, true);
    }
}
void Stop()
{
    std::array<void*, 3> targets{};
    {
        std::lock_guard guard(gate);
        state.enabled = false; state.latest.valid = false;
        targets = state.targets; state.hooksReady = false;
    }
    for (void* target : targets) if (target) MH_DisableHook(target);
    sdf::Clear();
}
bool OwnsCodeAddress(std::uint64_t address)
{
    std::lock_guard guard(gate);
    for (void* target : state.targets)
    {
        const auto start = reinterpret_cast<std::uint64_t>(target);
        if (start && address >= start && address - start < 32) return true;
    }
    return false;
}
}
