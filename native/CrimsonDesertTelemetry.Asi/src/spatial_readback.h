#pragma once
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <atomic>
#include <vector>

namespace cdt::spatial
{
using NativeBarrier = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList7*,UINT,const D3D12_BARRIER_GROUP*);
enum class CopyPhase { Idle, Preparing, Ready, InExposure, AwaitRelease, Recorded, Closed, Submitting, WaitingGpu, Complete, Failed };
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
};

// One in-flight transaction, independent of the lossy passive trace. Callback
// locks cover only bounded metadata/recording; allocation, Map and IO are worker-only.
class SpatialReadback
{
public:
    ~SpatialReadback();
    bool Begin();
    void Discover(ID3D12GraphicsCommandList7* list,ID3D12Resource* source);
    bool Arm(ID3D12GraphicsCommandList7* list,ID3D12Resource* source,uint32_t frame);
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
    SRWLOCK mutex_=SRWLOCK_INIT;
    std::atomic<ID3D12GraphicsCommandList7*> observedList_{};
    CopyResult result_;
    bool requested_{}, resetPending_{}, generationKnown_{}, closed_{}, closePending_{}, mapping_{};
    uint64_t generation_{}, armedTick_{};
    D3D12_COMMAND_LIST_TYPE type_{};
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7> list_;
    Microsoft::WRL::ComPtr<ID3D12Resource> source_,readback_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<IUnknown> deviceIdentity_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
};
}
