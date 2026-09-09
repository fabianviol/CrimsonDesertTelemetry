#pragma once
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <atomic>
#include <vector>
#include <array>

namespace cdt::spatial
{
using NativeBarrier = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList7*,UINT,const D3D12_BARRIER_GROUP*);
enum class CopyPhase { Idle, Preparing, Ready, InExposure, AwaitRelease, Recorded, Closed, Submitting, WaitingGpu, Complete, Failed };
// A root argument persists until it is overwritten or the root signature changes,
// so the recording window opens at list Reset, not inside the exposure wrapper.
enum class RootKind { Cbv, Uav, Srv };
struct CopyResult
{
    CopyPhase phase{};
    const char* reason="idle";
    uint64_t generation{}, frame{}, queue{}, releaseTick{}, submitTick{}, completedTick{}, fenceValue{};
    uint32_t recordingThread{}, submissionThread{}, mapCalls{};
    bool issued{}, gpuCompleted{};
    HRESULT error=S_OK;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    uint64_t allocationBytes{};
    D3D12_TEXTURE_BARRIER release{};
    std::vector<uint8_t> packed;
    bool buffersRequested{}, nativeDispatchSeen{}, buffersCopied{}, buffersGpuPaired{};
    uint64_t giResource{}, exposureResource{}, giBase{}, exposureBase{}, giOffset{}, exposureOffset{}, pairReadbackOffset{};
    uint32_t giRootIndex{}, exposureRootIndex{}, nativeDispatches{};
    // Whole pinned buffers: without a resolvable binding there is no trustworthy
    // offset, so the offline decoder locates the window instead of guessing here.
    std::vector<uint8_t> gpuGi, gpuExposure;
    uint64_t giBytes{}, exposureBytes{};
    std::array<uint64_t,64> nativeCbv{},nativeUav{};
    // Diagnostics only: descriptor tables and heaps are recorded, never copied from.
    bool giFromSrv{}, rootThreadConflict{};
    // Corroboration only. The live game binds through descriptor tables, so a
    // root hit is a bonus, never a precondition for the same-submission copy.
    uint32_t giBindingHits{}, exposureBindingHits{};
    uint32_t rootSetsBeforeExposure{}, rootSetsInsideExposure{}, tableSets{}, heapSets{};
    std::array<uint64_t,64> nativeSrv{},nativeTable{};
    std::array<uint64_t,4> descriptorHeaps{};
};

// One in-flight transaction, independent of the lossy passive trace. Callback
// locks cover only bounded metadata/recording; allocation, Map and IO are worker-only.
class SpatialReadback
{
public:
    ~SpatialReadback();
    bool Begin();
    void Discover(ID3D12GraphicsCommandList7* list,ID3D12Resource* source,ID3D12Resource* gi=nullptr,ID3D12Resource* exposure=nullptr);
    bool Arm(ID3D12GraphicsCommandList7* list,ID3D12Resource* source,uint32_t frame,ID3D12Resource* gi=nullptr,ID3D12Resource* exposure=nullptr);
    void RootSignature();
    void RootBuffer(RootKind kind,UINT index,D3D12_GPU_VIRTUAL_ADDRESS address);
    void RootTable(UINT index,uint64_t handle);
    void DescriptorHeaps(UINT count,const uint64_t* heaps);
    bool Observes(ID3D12GraphicsCommandList* list) const;
    void NativeDispatchEnd(ID3D12GraphicsCommandList7* list,UINT x,UINT y,UINT z,NativeBarrier original);
    void ExposureEnd(bool stable);
    void Barrier(ID3D12GraphicsCommandList7* list,UINT count,const D3D12_BARRIER_GROUP* groups,NativeBarrier original);
    void Reset(ID3D12GraphicsCommandList* list,bool after,HRESULT hr=S_OK);
    void Close(ID3D12GraphicsCommandList* list,bool after,HRESULT hr=S_OK);
    void Submit(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList* const* lists,bool after);
    void Poll();
    void Cancel(const char* reason);
    CopyResult Snapshot();
    static bool MatchesRelease(const D3D12_TEXTURE_BARRIER& barrier,ID3D12Resource* source);
private:
    void Fail(const char* reason,HRESULT hr=E_FAIL);
    bool Recording() const;
    void NoteRootThread();
    SRWLOCK mutex_=SRWLOCK_INIT;
    std::atomic<ID3D12GraphicsCommandList7*> observedList_{};
    CopyResult result_;
    bool requested_{}, resetPending_{}, generationKnown_{}, closed_{}, closePending_{}, mapping_{};
    uint64_t generation_{}, armedTick_{};
    D3D12_COMMAND_LIST_TYPE type_{};
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7> list_;
    Microsoft::WRL::ComPtr<ID3D12Resource> source_,readback_;
    Microsoft::WRL::ComPtr<ID3D12Resource> gi_,exposure_;
    uint64_t giBytes_{},exposureBytes_{};
    std::array<uint64_t,64> cbv_{},uav_{},srv_{},table_{};
    uint32_t rootThread_{};
    uint64_t giWidth_{},exposureWidth_{};
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<IUnknown> deviceIdentity_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
};
}
