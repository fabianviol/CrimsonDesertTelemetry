#include "render_capture.h"
#include "render_bridge.h"
#include "native_contract.generated.h"
#include "console/common.h"
#include "console/mem.h"
#include <d3d12.h>
#include <MinHook.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <cstring>

extern "C" void CdtFilterThunk();
extern "C" { void* CdtFilterTrampoline = nullptr; }

namespace cdt::render
{
namespace
{
namespace contract = native_contract;
enum class Phase { Discover, Found, Preparing, Ready, Recorded, Submitting, WaitingGpu, Failed, Stopped };
SRWLOCK lock = SRWLOCK_INIT;
Phase phase = Phase::Stopped;
uint64_t gameBase{}, hookAddress{}, lastAttempt{}, capturedAt{}, issuedAt{}, fenceValue{};
uint32_t intervalMs = 50, capturedFrame{}, lastFrame{}, error{};
uint64_t capturedOutputResource{}, capturedCounterResource{}, capturedOwner{};
uint32_t capturedBufferIndex = UINT32_MAX;
bool hasFrame{}, executeEnabled{};
std::atomic<bool> hookEnabled{};
ID3D12Resource* discoverySource{};
ID3D12Resource* pendingSource{};
ID3D12Resource* pendingCounter{};
ID3D12Resource* readback{};
ID3D12Fence* fence{};
IUnknown* preparedDeviceIdentity{};
ID3D12GraphicsCommandList* pendingList{};
D3D12_COMMAND_LIST_TYPE queueType = D3D12_COMMAND_LIST_TYPE_DIRECT;
std::array<uint8_t, SceneBytes> scene{};
using ExecuteFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
ExecuteFn executeOriginal{};
void* executeTarget{};

template<class T> bool Read(uint64_t address, T& result)
{
    return address && ch::mem::SafeRead(reinterpret_cast<void*>(address), &result, sizeof(result));
}
bool ReadScene(std::array<uint8_t, SceneBytes>& result)
{
    uint64_t root{}, data{};
    if (!Read(gameBase + contract::SceneGlobalRva, root) || !Read(root + contract::SceneRootPointerOffset, data)) return false;
    // +428 is a pointer to the SceneConstantBuffer, not an inline structure.
    return ch::mem::SafeRead(reinterpret_cast<void*>(data), result.data(), result.size()) && ValidateScene(result.data());
}
bool Resolve(uint64_t outer, uint64_t command, ID3D12Resource*& source, ID3D12GraphicsCommandList*& list)
{
    uint64_t inner{}, holder{}, resource{}, nativeList{};
    uint32_t stride{}, count{};
    if (!Read(outer + contract::InnerOffset, inner) || !Read(inner + contract::StrideOffset, stride) ||
        !Read(inner + contract::CountOffset, count) || stride != RecordStride || count != RecordCount ||
        !Read(inner + contract::ResourceOffset, resource) || !Read(command + contract::CommandHolderOffset, holder) ||
        !Read(holder + contract::NativeListOffset, nativeList) || !resource || !nativeList) return false;
    source = reinterpret_cast<ID3D12Resource*>(resource);
    list = reinterpret_cast<ID3D12GraphicsCommandList*>(nativeList);
    const auto desc = source->GetDesc();
    return desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER && desc.Width == LightBytes &&
        (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0 &&
        (list->GetType() == D3D12_COMMAND_LIST_TYPE_DIRECT || list->GetType() == D3D12_COMMAND_LIST_TYPE_COMPUTE);
}
bool ResolveCounter(uint64_t outer, ID3D12Resource*& counter)
{
    uint64_t inner{}, resource{};
    // Do not assume the CPU wrapper's stride/count encoding for this counter.
    // The exact binding identifies it; the native descriptor bounds the copy.
    if (!Read(outer + contract::InnerOffset, inner) || !Read(inner + contract::ResourceOffset, resource) || !resource) return false;
    counter = reinterpret_cast<ID3D12Resource*>(resource);
    const auto desc = counter->GetDesc();
    return desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER && desc.Width >= CounterBytes &&
        (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0;
}
void Fail(uint32_t code)
{
    error = code;
    phase = Phase::Failed;
    // References stay alive after a partial command recording or failed submit;
    // releasing them without a completion fence would itself be unsafe.
}

void Record(uint64_t outer, uint64_t command, uint64_t counterOuter, uint64_t owner)
{
    if (phase != Phase::Discover && phase != Phase::Ready) return;
    const uint64_t now = GetTickCount64();
    if (now - lastAttempt < intervalMs) return;
    lastAttempt = now;
    ID3D12Resource* source{};
    ID3D12Resource* counter{};
    ID3D12GraphicsCommandList* list{};
    if (!Resolve(outer, command, source, list) || !ResolveCounter(counterOuter, counter) || source == counter) return;
    if (phase == Phase::Discover)
    {
        source->AddRef();
        discoverySource = source;
        queueType = list->GetType();
        phase = Phase::Found;
        return;
    }
    IUnknown* sourceDevice{};
    IUnknown* counterDevice{};
    IUnknown* listDevice{};
    const bool sameDevice = SUCCEEDED(source->GetDevice(IID_PPV_ARGS(&sourceDevice))) &&
        SUCCEEDED(counter->GetDevice(IID_PPV_ARGS(&counterDevice))) &&
        SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&listDevice))) &&
        sourceDevice == preparedDeviceIdentity && counterDevice == preparedDeviceIdentity &&
        listDevice == preparedDeviceIdentity && list->GetType() == queueType;
    if (sourceDevice) sourceDevice->Release();
    if (counterDevice) counterDevice->Release();
    if (listDevice) listDevice->Release();
    if (!sameDevice) { Fail(ERROR_INVALID_HANDLE); return; }
    std::array<uint8_t, SceneBytes> confirmation{};
    if (!ReadScene(scene) || !ReadScene(confirmation) || !SameScene(scene.data(), confirmation.data())) return;
    memcpy(&capturedFrame, scene.data() + contract::FrameOffset, sizeof(capturedFrame));
    if (hasFrame && capturedFrame == lastFrame) return;
    source->AddRef();
    counter->AddRef();
    list->AddRef();
    pendingSource = source;
    pendingCounter = counter;
    pendingList = list;
    capturedAt = now;
    // These identities belong to THIS recorded pair. Never resolve them again
    // when the worker publishes: the renderer may already have switched banks.
    capturedOutputResource = reinterpret_cast<uint64_t>(source);
    capturedCounterResource = reinterpret_cast<uint64_t>(counter);
    capturedOwner = owner;
    capturedBufferIndex = UINT32_MAX;
    uint32_t currentIndex{};
    if (owner && Read(owner + contract::OwnerBankIndexOffset, currentIndex)) capturedBufferIndex = currentIndex;
    std::array<D3D12_RESOURCE_BARRIER, 2> barriers{};
    for (auto& barrier : barriers)
    {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    }
    barriers[0].Transition.pResource = source;
    barriers[1].Transition.pResource = counter;
    list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    list->CopyBufferRegion(readback, 0, source, 0, LightBytes);
    // The counter is a GPU-written buffer, not a CPU count. Copy its bounded
    // prefix before subsequent engine passes reuse it, on the SAME list/fence.
    list->CopyBufferRegion(readback, LightBytes, counter, 0, CounterBytes);
    for (auto& barrier : barriers)
    {
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    // A camera update concurrent with recording invalidates this pair. Still
    // submit and fence the copy, but never publish the mismatched sample.
    if (!ReadScene(confirmation) || !SameScene(scene.data(), confirmation.data())) capturedAt = 0;
    issuedAt = now;
    phase = Phase::Recorded;
}

void STDMETHODCALLTYPE ExecuteHook(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
    bool target = false;
    AcquireSRWLockExclusive(&lock);
    if (phase == Phase::Recorded)
    {
        for (UINT i = 0; i < count; ++i)
            if (lists[i] == pendingList) { target = true; phase = Phase::Submitting; break; }
    }
    ReleaseSRWLockExclusive(&lock);
    executeOriginal(queue, count, lists);
    if (!target) return;
    // Queue::Signal is ordered AFTER the exact submission containing our copy.
    // A delay, ID3D12Fence::Signal (CPU-side), or a different queue is not proof.
    IUnknown* queueDevice{};
    const bool compatibleQueue = SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) &&
        queueDevice == preparedDeviceIdentity && queue->GetDesc().Type == queueType;
    if (queueDevice) queueDevice->Release();
    const HRESULT hr = compatibleQueue ? queue->Signal(fence, ++fenceValue) : E_INVALIDARG;
    AcquireSRWLockExclusive(&lock);
    if (FAILED(hr)) Fail(static_cast<uint32_t>(hr));
    else phase = Phase::WaitingGpu;
    ReleaseSRWLockExclusive(&lock);
}

bool Prepare()
{
    ID3D12Device* device{};
    HRESULT hr = discoverySource->GetDevice(IID_PPV_ARGS(&device));
    if (FAILED(hr)) return false;
    hr = device->QueryInterface(IID_PPV_ARGS(&preparedDeviceIdentity));
    if (FAILED(hr)) { device->Release(); return false; }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    auto desc = discoverySource->GetDesc();
    desc.Width = LightBytes + CounterBytes;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback));
    if (SUCCEEDED(hr)) hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    // Obtain the real ExecuteCommandLists implementation from THIS source's
    // device, avoiding a wrong WARP/adapter/system-D3D12 function address.
    ID3D12CommandQueue* probeQueue{};
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = queueType;
    if (SUCCEEDED(hr)) hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&probeQueue));
    device->Release();
    if (FAILED(hr)) { error = static_cast<uint32_t>(hr); return false; }
    executeTarget = (*reinterpret_cast<void***>(probeQueue))[10];
    const auto create = MH_CreateHook(executeTarget, ExecuteHook, reinterpret_cast<void**>(&executeOriginal));
    const auto enable = create == MH_OK ? MH_EnableHook(executeTarget) : create;
    probeQueue->Release();
    if (enable != MH_OK) { error = ERROR_INVALID_FUNCTION; return false; }
    executeEnabled = true;
    ch::Log("ManyLights recurring capture ready: exact filter callsite, %u Hz, paired counter, submission fence; instrumented run.", 1000 / intervalMs);
    return true;
}
}

void CaptureFilter(uint64_t outer, uint64_t command, uint64_t counterOuter, uint64_t owner)
{
    // Never block the renderer behind worker-side Map/publication/initialization.
    if (!TryAcquireSRWLockExclusive(&lock)) return;
    __try { Record(outer, command, counterOuter, owner); }
    __except (EXCEPTION_EXECUTE_HANDLER) { Fail(GetExceptionCode()); }
    ReleaseSRWLockExclusive(&lock);
}

PreflightResult CheckCapturePreflight(uint64_t moduleBase)
{
    if (!moduleBase) return {PreflightFailure::MissingImage};
    // Bound address arithmetic before touching an untrusted/malformed image.
    if (moduleBase > UINT64_MAX - 0x80000000ULL) return {PreflightFailure::MalformedImage};
    IMAGE_DOS_HEADER dos{};
    if (!Read(moduleBase, dos)) return {PreflightFailure::UnreadableImage};
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < static_cast<LONG>(sizeof(dos)) ||
        dos.e_lfanew > 0x100000) return {PreflightFailure::MalformedImage};
    IMAGE_NT_HEADERS64 nt{};
    if (!Read(moduleBase + static_cast<uint32_t>(dos.e_lfanew), nt)) return {PreflightFailure::UnreadableImage};
    const auto imageBytes = nt.OptionalHeader.SizeOfImage;
    const uint64_t sectionsRva = static_cast<uint32_t>(dos.e_lfanew) + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) +
        nt.FileHeader.SizeOfOptionalHeader;
    if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64) ||
        nt.FileHeader.NumberOfSections == 0 || nt.FileHeader.NumberOfSections > 96 || imageBytes > 0x80000000ULL ||
        sectionsRva + nt.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER) > imageBytes)
        return {PreflightFailure::MalformedImage};
    const auto inside = [imageBytes](uint64_t rva, size_t length)
    { return rva < imageBytes && length <= imageBytes - rva; };
    if (!inside(contract::SceneGlobalRva, sizeof(uint64_t))) return {PreflightFailure::SceneGlobalOutsideImage};
    std::array<IMAGE_SECTION_HEADER, 96> sections{};
    for (uint32_t i = 0; i < nt.FileHeader.NumberOfSections; ++i)
        if (!Read(moduleBase + sectionsRva + i * sizeof(IMAGE_SECTION_HEADER), sections[i]))
            return {PreflightFailure::UnreadableImage};
    const auto executable = [&](uint64_t rva, size_t length)
    {
        if (!inside(rva, length)) return false;
        for (uint32_t i = 0; i < nt.FileHeader.NumberOfSections; ++i)
        {
            const auto& section = sections[i];
            const uint64_t start = section.VirtualAddress;
            const uint64_t size = std::max(section.Misc.VirtualSize, section.SizeOfRawData);
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) && start <= rva && rva - start <= size &&
                length <= size - (rva - start) && inside(start, static_cast<size_t>(size))) return true;
        }
        return false;
    };
    const auto matches = [&](uint64_t rva, std::span<const uint8_t> signature)
    {
        std::array<uint8_t, 128> bytes{};
        return !signature.empty() && signature.size() <= bytes.size() &&
            ch::mem::SafeRead(reinterpret_cast<const void*>(moduleBase + rva), bytes.data(), signature.size()) &&
            std::equal(signature.begin(), signature.end(), bytes.begin());
    };
    if (!executable(contract::HookRva, contract::HookSignature.size()))
        return {PreflightFailure::HookOutsideExecutableSection};
    if (!matches(contract::HookRva, contract::HookSignature)) return {PreflightFailure::HookSignatureMismatch};
    for (uint32_t i = 0; i < contract::ContextSignatures.size(); ++i)
    {
        const auto& context = contract::ContextSignatures[i];
        if (!executable(context.rva, context.bytes.size())) return {PreflightFailure::ContextOutsideExecutableSection, i};
        if (!matches(context.rva, context.bytes)) return {PreflightFailure::ContextSignatureMismatch, i};
    }
    return {};
}

const char* PreflightFailureName(PreflightFailure failure)
{
    switch (failure)
    {
        case PreflightFailure::None: return "ready";
        case PreflightFailure::MissingImage: return "missing-module";
        case PreflightFailure::UnreadableImage: return "unreadable-image";
        case PreflightFailure::MalformedImage: return "malformed-pe-image";
        case PreflightFailure::SceneGlobalOutsideImage: return "scene-root-outside-image";
        case PreflightFailure::HookOutsideExecutableSection: return "hook-not-in-executable-section";
        case PreflightFailure::HookSignatureMismatch: return "hook-signature-mismatch";
        case PreflightFailure::ContextOutsideExecutableSection: return "context-not-in-executable-section";
        case PreflightFailure::ContextSignatureMismatch: return "caller-context-signature-mismatch";
    }
    return "unknown-preflight-failure";
}

bool StartCapture(uint64_t moduleBase, unsigned sampleRateHz)
{
    const auto preflight = CheckCapturePreflight(moduleBase);
    if (!preflight)
    {
        PublishStatus(Status::Incompatible, ERROR_INVALID_DATA, ExactBuild);
        ch::Log("ManyLights disabled before any hook: %s (context index %u).", PreflightFailureName(preflight.failure), preflight.contextIndex);
        return false;
    }
    gameBase = moduleBase;
    hookAddress = gameBase + contract::HookRva;
    intervalMs = 1000 / std::clamp(sampleRateHz, 1u, 60u);
    const auto init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
    { PublishStatus(Status::Fault, ERROR_INVALID_FUNCTION, ExactBuild); return false; }
    if (MH_CreateHook(reinterpret_cast<void*>(hookAddress), CdtFilterThunk, &CdtFilterTrampoline) != MH_OK)
    { PublishStatus(Status::Fault, ERROR_INVALID_FUNCTION, ExactBuild); return false; }
    phase = Phase::Discover;
    if (MH_EnableHook(reinterpret_cast<void*>(hookAddress)) != MH_OK)
    { phase = Phase::Stopped; PublishStatus(Status::Fault, ERROR_INVALID_FUNCTION, ExactBuild); return false; }
    hookEnabled = true;
    ch::Log("ManyLights exact-build/context detour installed at RVA 0x%llX; waiting for renderer.", contract::HookRva);
    return true;
}

void PollCapture()
{
    AcquireSRWLockExclusive(&lock);
    if (phase == Phase::Found)
    {
        phase = Phase::Preparing;
        ReleaseSRWLockExclusive(&lock);
        const bool ok = Prepare();
        AcquireSRWLockExclusive(&lock);
        phase = ok ? Phase::Ready : Phase::Failed;
    }
    if (phase == Phase::WaitingGpu)
    {
        const UINT64 completed = fence->GetCompletedValue();
        if (completed == UINT64_MAX) Fail(static_cast<uint32_t>(DXGI_ERROR_DEVICE_REMOVED));
        else if (completed >= fenceValue)
        {
            void* mapped{};
            D3D12_RANGE range{0, LightBytes + CounterBytes};
            const HRESULT hr = readback->Map(0, &range, &mapped);
            if (FAILED(hr) || !mapped) Fail(static_cast<uint32_t>(hr));
            else
            {
                if (capturedAt) PublishSample(scene.data(), mapped, static_cast<const uint8_t*>(mapped) + LightBytes,
                    capturedAt, capturedOutputResource, capturedCounterResource, capturedOwner, capturedBufferIndex);
                const D3D12_RANGE noWrites{0,0};
                readback->Unmap(0, &noWrites);
                pendingSource->Release(); pendingSource = nullptr;
                pendingCounter->Release(); pendingCounter = nullptr;
                pendingList->Release(); pendingList = nullptr;
                lastFrame = capturedFrame; hasFrame = true;
                phase = Phase::Ready;
            }
        }
    }
    if ((phase == Phase::Recorded || phase == Phase::WaitingGpu) && GetTickCount64() - issuedAt > 5000)
        Fail(WAIT_TIMEOUT);
    if (phase == Phase::Failed)
    {
        if (!error) error = ERROR_INVALID_DATA;
        PublishStatus(Status::Fault, error, ExactBuild);
        ch::Log("ManyLights disabled after capture failure 0x%08X; pending resources retained safely until process exit.", error);
        phase = Phase::Stopped;
    }
    ReleaseSRWLockExclusive(&lock);
}

uint32_t CaptureFailureCode()
{
    AcquireSRWLockShared(&lock);
    const auto result = (phase == Phase::Failed || phase == Phase::Stopped) ? error : 0;
    ReleaseSRWLockShared(&lock);
    return result;
}

void StopCapture()
{
    if (hookEnabled) MH_DisableHook(reinterpret_cast<void*>(hookAddress));
    if (executeEnabled) MH_DisableHook(executeTarget);
    AcquireSRWLockExclusive(&lock);
    phase = Phase::Stopped;
    ReleaseSRWLockExclusive(&lock);
    PublishStatus(Status::Stopped);
    // Hooks/trampolines and bounded resources live until process exit. The ASI
    // is pinned while instrumentation is enabled, so in-flight thunks cannot
    // jump into an unloaded DLL. Never MH_Uninitialize: overlay shares MinHook.
}
bool OwnsCodeAddress(uint64_t address)
{
    return hookEnabled && address >= hookAddress && address < hookAddress + contract::HookSignature.size();
}
#ifdef CDT_RENDER_CAPTURE_TEST
// Host-test-only entry. Never compiled into the ASI; production always requires
// the executable hash plus the exact callsite signature before installing hooks.
void InitializeCaptureForTest(uint64_t moduleBase)
{
    gameBase = moduleBase;
    intervalMs = 1;
    const auto result = MH_Initialize();
    if (result != MH_OK && result != MH_ERROR_ALREADY_INITIALIZED) { phase = Phase::Failed; return; }
    phase = Phase::Discover;
}
#endif
}

extern "C" void CdtCaptureFilter(uint64_t outer, uint64_t command, uint64_t counterOuter, uint64_t owner)
{ cdt::render::CaptureFilter(outer, command, counterOuter, owner); }
