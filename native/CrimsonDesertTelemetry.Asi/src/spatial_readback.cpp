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
    {list_.Detach();source_.Detach();readback_.Detach();gi_.Detach();exposure_.Detach();device_.Detach();deviceIdentity_.Detach();fence_.Detach();queue_.Detach();}
}
bool SpatialReadback::Begin(unsigned count,uint64_t intervalMilliseconds,unsigned retained)
{
    Guard g(mutex_);
    if(requested_) return false; // One SERIES per plugin process, including failures.
    if(!count||count>MaxSeriesTransactions||!retained||retained>MaxTransactions) return false;
    requested_=true;result_={};result_.reason="awaiting-source";
    budget_=count;completed_=0;nextFence_=0;lastCompletedTick_=0;
    intervalMs_=intervalMilliseconds;retained_=retained;records_.clear();
    return true;
}
bool SpatialReadback::SeriesFinished() const
{
    Guard g(const_cast<SRWLOCK&>(mutex_));
    return requested_&&(budget_==0||result_.phase==CopyPhase::Failed);
}
unsigned SpatialReadback::Completed() const
{
    Guard g(const_cast<SRWLOCK&>(mutex_));
    return completed_;
}
std::vector<CopyResult> SpatialReadback::Records() const
{
    Guard g(const_cast<SRWLOCK&>(mutex_));
    auto copy=records_;
    // A failed or in-flight transaction is reported too; it is never silently dropped.
    if(result_.phase==CopyPhase::Failed||(!records_.empty()&&result_.phase!=CopyPhase::Complete&&
        result_.phase!=CopyPhase::Idle&&result_.phase!=CopyPhase::Ready))
        copy.push_back(result_);
    else if(records_.empty())copy.push_back(result_);
    return copy;
}
CopyResult SpatialReadback::LatestRecord() const
{
    Guard g(const_cast<SRWLOCK&>(mutex_));
    return records_.empty()?CopyResult{}:records_.back();
}
void SpatialReadback::KeepRecordAndRearm()
{
    // Caller holds the lock and has finished mapping this transaction.
    records_.push_back(result_);
    // A long series must not grow without bound: each record carries its whole
    // volume snapshot. The newest are the ones a live consumer and a report need.
    while(records_.size()>retained_) records_.erase(records_.begin());
    ++completed_;
    if(budget_)--budget_;
    lastCompletedTick_=GetTickCount64();
    if(!budget_)return;
    // Preserve everything established at preparation; clear only per-transaction state.
    CopyResult next{};
    next.footprint=result_.footprint;next.allocationBytes=result_.allocationBytes;
    next.buffersRequested=result_.buffersRequested;next.pairReadbackOffset=result_.pairReadbackOffset;
    next.giResource=result_.giResource;next.exposureResource=result_.exposureResource;
    next.giBase=result_.giBase;next.exposureBase=result_.exposureBase;
    next.giBytes=result_.giBytes;next.exposureBytes=result_.exposureBytes;
    next.phase=CopyPhase::Ready;next.reason="awaiting-known-reset-and-exposure";
    result_=next;
    observedList_=list_.Get();
}
void SpatialReadback::Fail(const char* reason,HRESULT hr)
{
    result_.phase=CopyPhase::Failed;result_.reason=reason;result_.error=hr;
    observedList_=nullptr;
    budget_=0; // A series never continues past an unexplained state.
}
void SpatialReadback::Discover(ID3D12GraphicsCommandList7* list,ID3D12Resource* source,ID3D12Resource* gi,ID3D12Resource* exposure)
{
    Guard g(mutex_);
    if(!requested_||result_.phase!=CopyPhase::Idle||!list||!source)return;
    list_=list;source_=source;type_=list->GetType();observedList_=list;
    gi_=gi;exposure_=exposure;result_.buffersRequested=gi||exposure;
    result_.phase=CopyPhase::Preparing;result_.reason="preparing";
}
bool SpatialReadback::Arm(ID3D12GraphicsCommandList7* list,ID3D12Resource* source,uint32_t frame,ID3D12Resource* gi,ID3D12Resource* exposure)
{
    Guard g(mutex_);
    if(result_.phase!=CopyPhase::Ready||list!=list_.Get()||source!=source_.Get()||
        !generationKnown_||resetPending_||closed_||closePending_||gi!=gi_.Get()||exposure!=exposure_.Get())return false;
    if(lastCompletedTick_&&GetTickCount64()-lastCompletedTick_<intervalMs_)return false;
    result_.phase=CopyPhase::InExposure;result_.reason="inside-exposure";
    result_.generation=generation_;result_.frame=frame;result_.recordingThread=GetCurrentThreadId();
    // Bindings issued earlier in THIS recording are still the live root arguments;
    // discarding them here is what made readback.2 reject a valid dispatch.
    armedTick_=GetTickCount64();return true;
}
bool SpatialReadback::Observes(ID3D12GraphicsCommandList* list) const
{
    return list&&list==observedList_.load(std::memory_order_relaxed);
}
bool SpatialReadback::Recording() const
{
    return result_.buffersRequested&&(result_.phase==CopyPhase::Preparing||
        result_.phase==CopyPhase::Ready||result_.phase==CopyPhase::InExposure);
}
void SpatialReadback::NoteRootThread()
{
    const auto thread=GetCurrentThreadId();
    if(!rootThread_)rootThread_=thread;
    else if(rootThread_!=thread)threadConflict_=true;
}
void SpatialReadback::RootSignature()
{
    Guard g(mutex_);
    // A new signature invalidates every root argument, inside or outside exposure.
    if(Recording()){cbv_.fill(0);uav_.fill(0);srv_.fill(0);table_.fill(0);}
}
void SpatialReadback::RootBuffer(RootKind kind,UINT index,D3D12_GPU_VIRTUAL_ADDRESS address)
{
    Guard g(mutex_);
    if(!Recording())return;
    const bool inExposure=result_.phase==CopyPhase::InExposure;
    if(index>=64){if(inExposure)Fail("native-root-context");return;}
    if(inExposure&&GetCurrentThreadId()!=result_.recordingThread){Fail("native-root-context");return;}
    NoteRootThread();
    // Setting another root kind at the same index cannot preserve the old binding.
    cbv_[index]=kind==RootKind::Cbv?address:0;
    uav_[index]=kind==RootKind::Uav?address:0;
    srv_[index]=kind==RootKind::Srv?address:0;
    table_[index]=0;
    if(inExposure)++rootsInside_;else ++rootsBefore_;
}
void SpatialReadback::RootTable(UINT index,uint64_t handle)
{
    Guard g(mutex_);
    if(!Recording()||index>=64)return;
    NoteRootThread();
    // Recorded to distinguish "bound through a table" from "not bound at all".
    // A descriptor handle is never resolved to a resource and never copied from.
    cbv_[index]=uav_[index]=srv_[index]=0;table_[index]=handle;
    ++tables_;
}
void SpatialReadback::DescriptorHeaps(UINT count,const uint64_t* heaps)
{
    Guard g(mutex_);
    if(!Recording()||!heaps)return;
    for(UINT i=0;i<count&&i<heapPtrs_.size();++i)heapPtrs_[i]=heaps[i];
    ++heaps_;
}
void SpatialReadback::NativeDispatchEnd(ID3D12GraphicsCommandList7* list,UINT x,UINT y,UINT z,NativeBarrier original)
{
    Guard g(mutex_);
    if(result_.phase!=CopyPhase::InExposure||!result_.buffersRequested)return;
    if(list!=list_.Get()||x!=2||y!=1||z!=1||!original||GetCurrentThreadId()!=result_.recordingThread||++result_.nativeDispatches!=1)
    {Fail("native-dispatch-context");return;}
    // Measured in PID4340: this dispatch binds GI and exposure through descriptor
    // tables, so no root scan can ever prove the binding. Resource IDENTITY comes
    // from the validated native producer/consumer path instead, and this copy
    // establishes TIMING only: same recording, same submission, one fence,
    // immediately after the original dispatch. Root state is kept as evidence.
    result_.nativeCbv=cbv_;result_.nativeUav=uav_;result_.nativeSrv=srv_;result_.nativeTable=table_;
    result_.descriptorHeaps=heapPtrs_;result_.rootSetsBeforeExposure=rootsBefore_;
    result_.rootSetsInsideExposure=rootsInside_;result_.tableSets=tables_;result_.heapSets=heaps_;
    result_.rootThreadConflict=threadConflict_;
    for(UINT i=0;i<64;++i)
    {
        if(cbv_[i]>=result_.giBase&&cbv_[i]-result_.giBase<giBytes_)
        {++result_.giBindingHits;result_.giOffset=cbv_[i]-result_.giBase;result_.giRootIndex=i;result_.giFromSrv=false;}
        if(srv_[i]>=result_.giBase&&srv_[i]-result_.giBase<giBytes_)
        {++result_.giBindingHits;result_.giOffset=srv_[i]-result_.giBase;result_.giRootIndex=i;result_.giFromSrv=true;}
        if(uav_[i]>=result_.exposureBase&&uav_[i]-result_.exposureBase<exposureBytes_)
        {++result_.exposureBindingHits;result_.exposureOffset=uav_[i]-result_.exposureBase;result_.exposureRootIndex=i;}
    }
    result_.nativeDispatchSeen=true;
    // Buffers have no texture layout. The read path may be a CBV or an SRV, so
    // cover both; the output is an ALLOW_UNORDERED_ACCESS resource by validation.
    D3D12_BUFFER_BARRIER barriers[2]{};
    for(auto& b:barriers)
    {b.SyncBefore=D3D12_BARRIER_SYNC_COMPUTE_SHADING;b.SyncAfter=D3D12_BARRIER_SYNC_COPY;
     b.AccessAfter=D3D12_BARRIER_ACCESS_COPY_SOURCE;b.Size=UINT64_MAX;}
    barriers[0].pResource=gi_.Get();
    barriers[0].AccessBefore=D3D12_BARRIER_ACCESS_CONSTANT_BUFFER|D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
    barriers[1].pResource=exposure_.Get();barriers[1].AccessBefore=D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
    D3D12_BARRIER_GROUP group{};group.Type=D3D12_BARRIER_TYPE_BUFFER;group.NumBarriers=2;group.pBufferBarriers=barriers;
    original(list,1,&group);
    result_.issued=true; // Even a later texture guard failure must retain this destination.
    // Whole buffers: the decoder locates the window, this code never guesses one.
    list->CopyBufferRegion(readback_.Get(),result_.pairReadbackOffset,gi_.Get(),0,giBytes_);
    list->CopyBufferRegion(readback_.Get(),result_.pairReadbackOffset+giBytes_,exposure_.Get(),0,exposureBytes_);
    for(auto& b:barriers)
    {b.SyncBefore=D3D12_BARRIER_SYNC_COPY;b.SyncAfter=D3D12_BARRIER_SYNC_COMPUTE_SHADING;
     b.AccessAfter=b.AccessBefore;b.AccessBefore=D3D12_BARRIER_ACCESS_COPY_SOURCE;}
    original(list,1,&group);result_.buffersCopied=true;
}
void SpatialReadback::ExposureEnd(bool stable)
{
    Guard g(mutex_);
    if(result_.phase!=CopyPhase::InExposure)return;
    if(!stable||result_.recordingThread!=GetCurrentThreadId()) {Fail("exposure-context-changed");return;}
    if(result_.buffersRequested&&!result_.buffersCopied){Fail("native-dispatch-or-bindings-unobserved");return;}
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
    {
        generationKnown_=resetPending_&&SUCCEEDED(hr);resetPending_=false;++generation_;
        cbv_.fill(0);uav_.fill(0);srv_.fill(0);table_.fill(0);rootThread_=0;
        heapPtrs_.fill(0);rootsBefore_=rootsInside_=tables_=heaps_=0;threadConflict_=false;
    }
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
        result_.fenceValue=++nextFence_;
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
        UINT rows{};UINT64 rowBytes{},bytes{},giWidth{},exposureWidth{};auto desc=source->GetDesc();
        const bool skyShape=desc.Width==64&&desc.Height==32&&desc.DepthOrArraySize==264&&desc.Format==DXGI_FORMAT_R8_TYPELESS;
        const bool distanceShape=desc.Width==128&&desc.Height==64&&desc.DepthOrArraySize==1040&&desc.Format==DXGI_FORMAT_R16_TYPELESS;
        const UINT64 texelBytes=distanceShape?2u:1u;
        const UINT64 packedBytes=desc.Width*desc.Height*desc.DepthOrArraySize*texelBytes;
        const bool shape=(skyShape||distanceShape)&&desc.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE3D&&desc.MipLevels==1&&
            (desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)&&
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
            if(rows!=desc.Height||rowBytes!=desc.Width*texelBytes||footprint.Footprint.Width!=desc.Width||
                footprint.Footprint.Height!=desc.Height||footprint.Footprint.Depth!=desc.DepthOrArraySize||
                footprint.Footprint.Format!=desc.Format||footprint.Footprint.RowPitch<rowBytes||
                bytes>(distanceShape?18u:4u)*1024*1024||bytes<packedBytes)hr=E_INVALIDARG;
        }
        if(SUCCEEDED(hr))
        {
            if(result_.buffersRequested)
            {
                if(!gi_||!exposure_||gi_.Get()==exposure_.Get())hr=E_INVALIDARG;
                else for(auto* input:{gi_.Get(),exposure_.Get()})
                {
                    const auto d=input->GetDesc();ComPtr<ID3D12Device> owner;ComPtr<IUnknown> id;
                    D3D12_HEAP_PROPERTIES hp{};D3D12_HEAP_FLAGS flags{};
                    if(d.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||d.Width<768||d.Width>65536||
                        FAILED(input->GetDevice(IID_PPV_ARGS(&owner)))||FAILED(owner.As(&id))||id.Get()!=identity.Get()||
                        FAILED(input->GetHeapProperties(&hp,&flags))||hp.Type!=D3D12_HEAP_TYPE_DEFAULT||!input->GetGPUVirtualAddress())
                    {hr=E_INVALIDARG;break;}
                    if(input==exposure_.Get()&&!(d.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))hr=E_INVALIDARG;
                    (input==gi_.Get()?giWidth:exposureWidth)=d.Width;
                }
            }
        }
        if(SUCCEEDED(hr))
        {
            // Dedicated tails do not overlap the placed texture footprint.
            if(result_.buffersRequested)bytes=((bytes+511)&~UINT64{511})+giWidth+exposureWidth;
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
                if(result_.buffersRequested)
                {
                    giBytes_=giWidth;exposureBytes_=exposureWidth;
                    result_.giBytes=giWidth;result_.exposureBytes=exposureWidth;
                    result_.giResource=reinterpret_cast<uint64_t>(gi_.Get());result_.exposureResource=reinterpret_cast<uint64_t>(exposure_.Get());
                    result_.giBase=gi_->GetGPUVirtualAddress();result_.exposureBase=exposure_->GetGPUVirtualAddress();
                    result_.pairReadbackOffset=bytes-giWidth-exposureWidth;
                }
                result_.phase=CopyPhase::Ready;result_.reason="awaiting-known-reset-and-exposure";
            }
        }
    }
    if(map)
    {
        void* pointer{};D3D12_RANGE range{0,static_cast<SIZE_T>(readback->GetDesc().Width)};
        const auto hr=readback->Map(0,&range,&pointer);
        std::vector<uint8_t> packed,gpuGi,gpuExposure;
        if(SUCCEEDED(hr))
        {
            const size_t rowSize=static_cast<size_t>(footprint.Footprint.Width)*
                (footprint.Footprint.Format==DXGI_FORMAT_R16_TYPELESS?2u:1u);
            const size_t height=footprint.Footprint.Height,depth=footprint.Footprint.Depth;
            packed.resize(rowSize*height*depth);
            const auto* data=static_cast<const uint8_t*>(pointer)+footprint.Offset;
            for(size_t z=0;z<depth;++z)for(size_t y=0;y<height;++y)
                std::memcpy(packed.data()+(z*height+y)*rowSize,data+(z*height+y)*footprint.Footprint.RowPitch,rowSize);
            if(result_.buffersCopied)
            {
                const auto* tail=static_cast<const uint8_t*>(pointer)+result_.pairReadbackOffset;
                gpuGi.assign(tail,tail+result_.giBytes);
                gpuExposure.assign(tail+result_.giBytes,tail+result_.giBytes+result_.exposureBytes);
            }
            D3D12_RANGE empty{};readback->Unmap(0,&empty);
        }
        Guard g(mutex_);mapping_=false;
        if(result_.phase==CopyPhase::WaitingGpu)
        {
            if(FAILED(hr))Fail("readback-map-failed",hr);
            else
            {
                result_.packed=std::move(packed);result_.gpuGi=std::move(gpuGi);result_.gpuExposure=std::move(gpuExposure);
                result_.buffersGpuPaired=result_.buffersCopied;result_.phase=CopyPhase::Complete;
                result_.reason=result_.buffersGpuPaired?"gpu-complete-texture-and-buffers":"gpu-complete-texture-only";
                observedList_=nullptr;
                KeepRecordAndRearm();
            }
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
