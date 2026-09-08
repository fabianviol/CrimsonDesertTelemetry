#include "spatial_readback.h"
#include <cstring>
#include <limits>

namespace cdt::spatial
{
namespace
{
using Microsoft::WRL::ComPtr;
struct Guard
{
    SRWLOCK& lock;
    explicit Guard(SRWLOCK& value):lock(value){AcquireSRWLockExclusive(&lock);}
    ~Guard(){ReleaseSRWLockExclusive(&lock);}
};
bool PendingCopy(CopyPhase p)
{
    return p==CopyPhase::InExposure||p==CopyPhase::AwaitRelease||p==CopyPhase::Recorded||p==CopyPhase::Closed;
}
}
SpatialReadback::~SpatialReadback()
{
    // A timed-out/failed submission can still reference these GPU objects. Never
    // reclaim them merely because the CPU stopped waiting. Bounded, process-lived.
    if(result_.issued&&!result_.gpuCompleted)
    {list_.Detach();source_.Detach();readback_.Detach();device_.Detach();deviceIdentity_.Detach();fence_.Detach();queue_.Detach();}
}
bool SpatialReadback::Begin()
{
    Guard g(mutex_);
    if(requested_) return false; // One direct readback per plugin process, including failures.
    requested_=true;result_={};result_.reason="awaiting-source";
    return true;
}
void SpatialReadback::Fail(const char* reason,HRESULT hr)
{
    result_.phase=CopyPhase::Failed;result_.reason=reason;result_.error=hr;
    observedList_=nullptr;
}
void SpatialReadback::Discover(ID3D12GraphicsCommandList7* list,ID3D12Resource* source)
{
    Guard g(mutex_);
    if(!requested_||result_.phase!=CopyPhase::Idle||!list||!source)return;
    list_=list;source_=source;type_=list->GetType();observedList_=list;
    result_.phase=CopyPhase::Preparing;result_.reason="preparing";
}
bool SpatialReadback::Arm(ID3D12GraphicsCommandList7* list,ID3D12Resource* source,uint32_t frame)
{
    Guard g(mutex_);
    if(result_.phase!=CopyPhase::Ready||list!=list_.Get()||source!=source_.Get()||
        !generationKnown_||resetPending_||closed_||closePending_)return false;
    result_.phase=CopyPhase::InExposure;result_.reason="inside-exposure";
    result_.generation=generation_;result_.frame=frame;result_.recordingThread=GetCurrentThreadId();
    armedTick_=GetTickCount64();return true;
}
void SpatialReadback::ExposureEnd(bool stable)
{
    Guard g(mutex_);
    if(result_.phase!=CopyPhase::InExposure)return;
    if(!stable||result_.recordingThread!=GetCurrentThreadId()) {Fail("exposure-context-changed");return;}
    result_.phase=CopyPhase::AwaitRelease;result_.reason="awaiting-exact-release";
}
bool SpatialReadback::MatchesRelease(const D3D12_TEXTURE_BARRIER& b,ID3D12Resource* source)
{
    const auto& s=b.Subresources;
    return b.pResource==source&&b.SyncBefore==D3D12_BARRIER_SYNC_COMPUTE_SHADING&&
        b.SyncAfter==D3D12_BARRIER_SYNC_NONE&&b.AccessBefore==D3D12_BARRIER_ACCESS_SHADER_RESOURCE&&
        b.AccessAfter==D3D12_BARRIER_ACCESS_NO_ACCESS&&b.LayoutBefore==D3D12_BARRIER_LAYOUT_SHADER_RESOURCE&&
        b.LayoutAfter==D3D12_BARRIER_LAYOUT_GENERIC_READ&&b.Flags==D3D12_TEXTURE_BARRIER_FLAG_NONE&&
        s.IndexOrFirstMipLevel==D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES&&s.NumMipLevels==0&&
        s.FirstArraySlice==0&&s.NumArraySlices==0&&s.FirstPlane==0&&s.NumPlanes==0;
}
void SpatialReadback::Barrier(ID3D12GraphicsCommandList7* list,UINT count,const D3D12_BARRIER_GROUP* groups,NativeBarrier original)
{
    if(list!=observedList_.load(std::memory_order_relaxed))return;
    Guard g(mutex_);
    if(list!=list_.Get()||(result_.phase!=CopyPhase::InExposure&&result_.phase!=CopyPhase::AwaitRelease))return;
    if(!groups||count>64){Fail("barrier-packet-bound");return;}
    const D3D12_TEXTURE_BARRIER* match{};unsigned hits{};
    for(UINT k=0;k<count;++k)
    {
        const auto& group=groups[k];
        if(group.Type!=D3D12_BARRIER_TYPE_TEXTURE)continue;
        if(group.NumBarriers>4096||(!group.pTextureBarriers&&group.NumBarriers)){Fail("barrier-entry-bound");return;}
        for(UINT i=0;i<group.NumBarriers;++i)if(group.pTextureBarriers[i].pResource==source_.Get())
        {match=&group.pTextureBarriers[i];++hits;}
    }
    if(!hits)return;
    if(result_.phase!=CopyPhase::AwaitRelease||hits!=1||!MatchesRelease(*match,source_.Get())||!original||
        !generationKnown_||generation_!=result_.generation||GetCurrentThreadId()!=result_.recordingThread||
        GetTickCount64()-armedTick_>250)
    {Fail("release-context-or-tuple-mismatch");return;}
    // The CURRENT engine packet establishes SRV layout/access, not a historical
    // trace. Round-trip through COPY_SOURCE, then forward the unmodified release.
    // This preserves every engine group, resource, flag and final layout.
    auto before=*match;
    before.SyncAfter=D3D12_BARRIER_SYNC_COPY;before.AccessAfter=D3D12_BARRIER_ACCESS_COPY_SOURCE;
    before.LayoutAfter=D3D12_BARRIER_LAYOUT_COPY_SOURCE;
    D3D12_BARRIER_GROUP batch{};batch.Type=D3D12_BARRIER_TYPE_TEXTURE;batch.NumBarriers=1;batch.pTextureBarriers=&before;
    original(list,1,&batch);
    D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readback_.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint=result_.footprint;
    D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=source_.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    auto restore=before;
    restore.SyncBefore=D3D12_BARRIER_SYNC_COPY;restore.SyncAfter=D3D12_BARRIER_SYNC_COMPUTE_SHADING;
    restore.AccessBefore=D3D12_BARRIER_ACCESS_COPY_SOURCE;restore.AccessAfter=D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
    restore.LayoutBefore=D3D12_BARRIER_LAYOUT_COPY_SOURCE;restore.LayoutAfter=D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
    batch.pTextureBarriers=&restore;original(list,1,&batch);
    result_.issued=true;result_.release=*match;result_.releaseTick=GetTickCount64();
    result_.phase=CopyPhase::Recorded;result_.reason="recorded-awaiting-close";
}
void SpatialReadback::Reset(ID3D12GraphicsCommandList* list,bool after,HRESULT hr)
{
    if(list!=observedList_.load(std::memory_order_relaxed))return;
    Guard g(mutex_);
    if(list!=list_.Get())return;
    if(!after)
    {
        if(PendingCopy(result_.phase)||result_.phase==CopyPhase::Submitting)Fail("list-reset-before-confirmed-submit");
        generationKnown_=false;resetPending_=true;closed_=false;closePending_=false;
    }
    else
    {generationKnown_=resetPending_&&SUCCEEDED(hr);resetPending_=false;++generation_;}
}
void SpatialReadback::Close(ID3D12GraphicsCommandList* list,bool after,HRESULT hr)
{
    if(list!=observedList_.load(std::memory_order_relaxed))return;
    Guard g(mutex_);
    if(list!=list_.Get())return;
    if(!after)
    {
        closePending_=true;
        if(result_.phase==CopyPhase::InExposure||result_.phase==CopyPhase::AwaitRelease)Fail("close-without-release");
    }
    else
    {
        closed_=closePending_&&SUCCEEDED(hr);closePending_=false;
        if(result_.phase==CopyPhase::Recorded)
        {if(!closed_)Fail("close-failed",hr);else {result_.phase=CopyPhase::Closed;result_.reason="awaiting-submit";}}
    }
}
void SpatialReadback::Submit(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList* const* lists,bool after)
{
    if(!observedList_.load(std::memory_order_relaxed))return;
    Guard g(mutex_);
    if(result_.phase!=CopyPhase::Closed&&result_.phase!=CopyPhase::Submitting)return;
    if(!lists||count>1024){Fail("submission-bound");return;}
    unsigned hits{};for(UINT i=0;i<count;++i)if(lists[i]==list_.Get())++hits;
    if(!hits)return;
    if(!queue||hits!=1||!generationKnown_||generation_!=result_.generation||!closed_)
    {Fail("submission-list-generation-mismatch");return;}
    if(!after)
    {
        if(result_.phase!=CopyPhase::Closed){Fail("overlapping-submission");return;}
        ComPtr<ID3D12Device> device;ComPtr<IUnknown> identity;
        if(FAILED(queue->GetDevice(IID_PPV_ARGS(&device)))||FAILED(device.As(&identity))||
            identity.Get()!=deviceIdentity_.Get()||queue->GetDesc().Type!=type_)
        {Fail("submission-device-or-queue-type");return;}
        queue_=queue;result_.queue=reinterpret_cast<uint64_t>(queue);result_.submissionThread=GetCurrentThreadId();
        result_.phase=CopyPhase::Submitting;
    }
    else
    {
        if(result_.phase!=CopyPhase::Submitting||queue!=queue_.Get()||result_.submissionThread!=GetCurrentThreadId())
        {Fail("submission-end-mismatch");return;}
        result_.fenceValue=1;
        const auto hr=queue->Signal(fence_.Get(),result_.fenceValue);
        if(FAILED(hr)){Fail("queue-signal-failed",hr);return;}
        result_.submitTick=GetTickCount64();result_.phase=CopyPhase::WaitingGpu;result_.reason="awaiting-gpu-fence";
    }
}
void SpatialReadback::Poll()
{
    // Preparation and mapping never hold the hook's mutex across driver work.
    ComPtr<ID3D12Resource> source,readback;ComPtr<ID3D12GraphicsCommandList7> list;
    bool prepare{},map{};D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    {
        Guard g(mutex_);
        if(result_.phase==CopyPhase::Preparing){prepare=true;source=source_;list=list_;}
        if(result_.phase==CopyPhase::WaitingGpu)
        {
            const auto completed=fence_->GetCompletedValue();
            if(completed==std::numeric_limits<uint64_t>::max())Fail("device-removed",DXGI_ERROR_DEVICE_REMOVED);
            else if(completed>=result_.fenceValue&&!mapping_)
            {
                result_.gpuCompleted=true;result_.completedTick=GetTickCount64();mapping_=true;
                map=true;readback=readback_;footprint=result_.footprint;++result_.mapCalls;
            }
            else if(GetTickCount64()-result_.submitTick>5000)Fail("gpu-fence-timeout",HRESULT_FROM_WIN32(WAIT_TIMEOUT));
        }
        if(PendingCopy(result_.phase)&&GetTickCount64()-armedTick_>5000)Fail("release-or-submit-timeout",HRESULT_FROM_WIN32(WAIT_TIMEOUT));
    }
    if(prepare)
    {
        ComPtr<ID3D12Device> device,listDevice;ComPtr<IUnknown> identity,listIdentity;
        ComPtr<ID3D12Resource> buffer;ComPtr<ID3D12Fence> fence;
        D3D12_FEATURE_DATA_D3D12_OPTIONS12 options{};HRESULT hr=S_OK;
        UINT rows{};UINT64 rowBytes{},bytes{};auto desc=source->GetDesc();
        const bool shape=desc.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE3D&&desc.Width==64&&desc.Height==32&&
            desc.DepthOrArraySize==264&&desc.MipLevels==1&&desc.Format==DXGI_FORMAT_R8_TYPELESS&&
            desc.SampleDesc.Count==1&&desc.SampleDesc.Quality==0;
        if(!shape||(type_!=D3D12_COMMAND_LIST_TYPE_DIRECT&&type_!=D3D12_COMMAND_LIST_TYPE_COMPUTE))hr=E_INVALIDARG;
        if(SUCCEEDED(hr))hr=source->GetDevice(IID_PPV_ARGS(&device));
        if(SUCCEEDED(hr))hr=list->GetDevice(IID_PPV_ARGS(&listDevice));
        if(SUCCEEDED(hr))hr=device.As(&identity);
        if(SUCCEEDED(hr))hr=listDevice.As(&listIdentity);
        if(SUCCEEDED(hr)&&identity.Get()!=listIdentity.Get())hr=E_INVALIDARG;
        if(SUCCEEDED(hr))hr=device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12,&options,sizeof(options));
        if(SUCCEEDED(hr)&&(!options.EnhancedBarriersSupported||device->GetNodeCount()!=1))hr=E_NOTIMPL;
        if(SUCCEEDED(hr))
        {
            device->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowBytes,&bytes);
            if(rows!=32||rowBytes!=64||footprint.Footprint.Width!=64||footprint.Footprint.Height!=32||
                footprint.Footprint.Depth!=264||footprint.Footprint.Format!=DXGI_FORMAT_R8_TYPELESS||
                footprint.Footprint.RowPitch<64||bytes>4*1024*1024||bytes<540672)hr=E_INVALIDARG;
        }
        if(SUCCEEDED(hr))
        {
            D3D12_RESOURCE_DESC bufferDesc{};bufferDesc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
            bufferDesc.Width=bytes;bufferDesc.Height=1;bufferDesc.DepthOrArraySize=1;bufferDesc.MipLevels=1;
            bufferDesc.SampleDesc.Count=1;bufferDesc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;heap.CreationNodeMask=heap.VisibleNodeMask=1;
            hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bufferDesc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&buffer));
            if(SUCCEEDED(hr))hr=device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence));
        }
        Guard g(mutex_);
        if(result_.phase==CopyPhase::Preparing)
        {
            if(FAILED(hr))Fail("readback-preparation-failed",hr);
            else
            {
                device_=device;deviceIdentity_=identity;readback_=buffer;fence_=fence;
                result_.footprint=footprint;result_.allocationBytes=bytes;
                result_.phase=CopyPhase::Ready;result_.reason="awaiting-known-reset-and-exposure";
            }
        }
    }
    if(map)
    {
        void* pointer{};D3D12_RANGE range{0,static_cast<SIZE_T>(readback->GetDesc().Width)};
        const auto hr=readback->Map(0,&range,&pointer);
        std::vector<uint8_t> packed;
        if(SUCCEEDED(hr))
        {
            packed.resize(64*32*264);
            const auto* data=static_cast<const uint8_t*>(pointer)+footprint.Offset;
            for(size_t z=0;z<264;++z)for(size_t y=0;y<32;++y)
                std::memcpy(packed.data()+(z*32+y)*64,data+(z*32+y)*footprint.Footprint.RowPitch,64);
            D3D12_RANGE empty{};readback->Unmap(0,&empty);
        }
        Guard g(mutex_);mapping_=false;
        if(result_.phase==CopyPhase::WaitingGpu)
        {
            if(FAILED(hr))Fail("readback-map-failed",hr);
            else{result_.packed=std::move(packed);result_.phase=CopyPhase::Complete;result_.reason="gpu-complete-texture-only";observedList_=nullptr;}
        }
    }
}
void SpatialReadback::Cancel(const char* reason)
{
    Guard g(mutex_);
    if(requested_&&result_.phase!=CopyPhase::Complete&&result_.phase!=CopyPhase::Failed)Fail(reason);
}
CopyResult SpatialReadback::Snapshot(){Guard g(mutex_);return result_;}
}
