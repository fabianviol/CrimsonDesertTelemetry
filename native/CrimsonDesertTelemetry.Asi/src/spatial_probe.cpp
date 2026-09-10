#include "spatial_probe.h"
#include "spatial_trace.h"
#include "spatial_readback.h"
#include "spatial_sample.h"
#include "sky_bridge.h"
#include "submission_observer.h"
#include "console/mem.h"
#include "console/common.h"
#include "native_contract.generated.h"
#include <windows.h>
#include <d3d12.h>
#include <MinHook.h>
#include <nlohmann/json.hpp>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <string>

extern "C" void CdtSpatialThunk();
namespace cdt::spatial
{
namespace
{
constexpr uint32_t DispatchRva=0x37B4360, ExposureReturnRva=0x35450A4;
constexpr std::array<uint8_t,14> Signature{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x57,0x41,0x56};
constexpr unsigned Limit=20, BarrierLimit=8;
// Preserve RAX even though the inspected exposure caller ignores it.
using DispatchFn=uint64_t(*)(uint64_t, uint32_t, uint32_t, uint32_t);
using BarrierFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList7*, UINT, const D3D12_BARRIER_GROUP*);
using ResetFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12CommandAllocator*,ID3D12PipelineState*);
using CloseFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*);
DispatchFn originalDispatch{};
BarrierFn originalBarrier{};
ResetFn originalReset{};
CloseFn originalClose{};
using RootBufferFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,D3D12_GPU_VIRTUAL_ADDRESS);
using RootSignatureFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12RootSignature*);
using NativeDispatchFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,UINT,UINT);
using RootTableFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,D3D12_GPU_DESCRIPTOR_HANDLE);
using DescriptorHeapsFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,ID3D12DescriptorHeap* const*);
RootBufferFn originalCbv{},originalUav{},originalSrv{};
RootSignatureFn originalSignature{};
NativeDispatchFn originalNativeDispatch{};
RootTableFn originalTable{};
DescriptorHeapsFn originalHeaps{};
constexpr size_t BindingHooks=7;
std::array<void*,BindingHooks> bindingTargets{};
void* resetTarget{};
void* closeTarget{};
SpatialTrace trace;
ID3D12Resource* traceResource{};
void* dispatchTarget{};
void* barrierTarget{};
uint64_t base{}, started{}, lastAttempt{};
unsigned run{}, count{};
HANDLE requestEvent{};
std::filesystem::path outputDirectory;
std::atomic<bool> enabled{}, observing{};
SRWLOCK lock=SRWLOCK_INIT;
enum class Phase { Idle, Ready, Pending, Failed };
Phase phase=Phase::Idle;
bool barrierInstalled{}, incomplete{};
bool directReadback{};
bool distanceMode{};
unsigned readbackCount{1},requestedTransactions{1},seriesCount{1},visibilitySeconds{};
uint64_t readbackIntervalMs{1000};
// Module is process-pinned; unresolved GPU work retains its bounded allocation.
SpatialReadback* readback=new SpatialReadback;
nlohmann::json samples;
struct Observation
{
    uint64_t tick{}, owner{}, command{}, resource{}, nativeList{}, nativeList7{}, giGpuResource{};
    uint64_t exposureGpuResource{};
    uint32_t frame{}, bank{}, error{}, barrierCount{};
    bool giStable{}, barrierOverflow{}, enhanced{};
    bool selectedForReadback{};
    D3D12_RESOURCE_DESC giDesc{};
    D3D12_HEAP_PROPERTIES giHeap{};
    HRESULT giHeapResult=E_FAIL;
    D3D12_RESOURCE_DESC desc{};
    D3D12_STATIC_SAMPLER_DESC sampler{};
    std::array<uint8_t,768> gi{}, giAfter{};
    std::array<uint8_t,2816> scene{};
    std::array<uint8_t,64> exposure{};
    std::array<uint8_t,80> viewEntry{};
    std::array<uint8_t,64> viewDescriptor{};
    std::array<uint8_t,48> textureDescriptor{};
    std::array<D3D12_TEXTURE_BARRIER,BarrierLimit> barriers{};
    void* barrierFunction{};
    void* resetFunction{};
    void* closeFunction{};
    std::array<void*,BindingHooks> bindingFunctions{};
};
// Passive search for the signed distance volume. AdaptExposureCS -- the dispatch
// this probe hooks -- binds only the sky-visibility volume, confirmed by
// disassembling the pipeline state the captured frame actually ran. So the SDF's
// resource cannot be discovered the way the R8 volume was, and this looks for it
// in texture barriers instead: a barrier names its resource, and the shape is
// unique. It copies nothing, hooks nothing new and issues no GPU work.
constexpr unsigned DistanceWidth=128,DistanceHeight=64,DistanceDepth=1040;
struct DistanceSighting { uint64_t resource{},list{}; unsigned barriers{};
    D3D12_BARRIER_LAYOUT before{},after{};
    // Enhanced barriers separate sync, access and layout. The layout says a copy
    // needs no transition; the ACCESS bits say whether it still needs a barrier.
    D3D12_BARRIER_ACCESS accessBefore{},accessAfter{};
    D3D12_BARRIER_SYNC syncBefore{},syncAfter{}; };
std::array<DistanceSighting,4> distanceSightings{};
std::atomic<unsigned> distanceSeen{},distanceInspections{};
// A first sighting may be the BEGINNING of a write, not the release needed for
// readback. Keep distinct complete tuples for known resources even after the
// descriptor-search budget is exhausted. No resource/list pointer is dereferenced
// later: these are CPU observations, not a resource-state or lifetime guarantee.
struct DistanceTransition
{
    D3D12_TEXTURE_BARRIER barrier{};
    D3D12_COMMAND_LIST_TYPE listType{};
    uint64_t firstList{},lastList{},firstTick{},lastTick{},occurrences{};
};
constexpr unsigned DistanceTransitionLimit=32;
std::array<DistanceTransition,DistanceTransitionLimit> distanceTransitions{};
unsigned distanceTransitionCount{};
SRWLOCK distanceLock=SRWLOCK_INIT;
std::atomic<unsigned> distanceDropped{},distanceOverflow{};
// GetDesc on every barriered resource would cost thousands of calls per frame.
// This bounds the whole run instead of trusting a heuristic to stay cheap.
constexpr unsigned DistanceInspectionLimit=20000;
Observation pending;
Observation copyObservation;
Observation distanceLatest;
uint64_t distanceArmedTick{};
std::vector<Observation> copyObservations;
unsigned publishedTransactions{};
thread_local Observation* active{};
bool SelectedNative(ID3D12GraphicsCommandList* list)
{
    return directReadback&&!distanceMode&&active&&active->selectedForReadback&&active->nativeList7==reinterpret_cast<uint64_t>(list);
}
// Root arguments outlive the exposure wrapper, so these observe the whole
// recording of the pinned list. Only the dispatch itself stays exposure-scoped.
bool ObservedNative(ID3D12GraphicsCommandList* list)
{
    return directReadback&&readback&&readback->Observes(list);
}
void STDMETHODCALLTYPE CbvHook(ID3D12GraphicsCommandList* list,UINT index,D3D12_GPU_VIRTUAL_ADDRESS address)
{originalCbv(list,index,address);if(ObservedNative(list))readback->RootBuffer(RootKind::Cbv,index,address);}
void STDMETHODCALLTYPE UavHook(ID3D12GraphicsCommandList* list,UINT index,D3D12_GPU_VIRTUAL_ADDRESS address)
{originalUav(list,index,address);if(ObservedNative(list))readback->RootBuffer(RootKind::Uav,index,address);}
void STDMETHODCALLTYPE SrvHook(ID3D12GraphicsCommandList* list,UINT index,D3D12_GPU_VIRTUAL_ADDRESS address)
{originalSrv(list,index,address);if(ObservedNative(list))readback->RootBuffer(RootKind::Srv,index,address);}
void STDMETHODCALLTYPE TableHook(ID3D12GraphicsCommandList* list,UINT index,D3D12_GPU_DESCRIPTOR_HANDLE handle)
{originalTable(list,index,handle);if(ObservedNative(list))readback->RootTable(index,handle.ptr);}
void STDMETHODCALLTYPE HeapsHook(ID3D12GraphicsCommandList* list,UINT heapCount,ID3D12DescriptorHeap* const* heaps)
{
    originalHeaps(list,heapCount,heaps);
    if(!ObservedNative(list)||!heaps)return;
    std::array<uint64_t,4> values{};
    const UINT kept=heapCount<values.size()?heapCount:static_cast<UINT>(values.size());
    for(UINT i=0;i<kept;++i)values[i]=reinterpret_cast<uint64_t>(heaps[i]);
    readback->DescriptorHeaps(kept,values.data());
}
void STDMETHODCALLTYPE SignatureHook(ID3D12GraphicsCommandList* list,ID3D12RootSignature* signature)
{originalSignature(list,signature);if(ObservedNative(list))readback->RootSignature();}
void STDMETHODCALLTYPE NativeDispatchHook(ID3D12GraphicsCommandList* list,UINT x,UINT y,UINT z)
{
    originalNativeDispatch(list,x,y,z);
    if(SelectedNative(list))readback->NativeDispatchEnd(reinterpret_cast<ID3D12GraphicsCommandList7*>(list),x,y,z,originalBarrier);
}
template<class T> bool Read(uint64_t address,T& value)
{
    return address>=0x10000 && ch::mem::SafeRead(reinterpret_cast<void*>(address),&value,sizeof(value));
}
std::string Hex(const uint8_t* data,size_t size)
{
    constexpr char digits[]="0123456789ABCDEF";
    std::string s(size*2,'0');
    for(size_t i=0;i<size;++i){s[i*2]=digits[data[i]>>4];s[i*2+1]=digits[data[i]&15];}
    return s;
}
bool SameDistanceBarrier(const D3D12_TEXTURE_BARRIER& a,const D3D12_TEXTURE_BARRIER& b)
{
    const auto& x=a.Subresources;const auto& y=b.Subresources;
    return a.pResource==b.pResource&&a.SyncBefore==b.SyncBefore&&a.SyncAfter==b.SyncAfter&&
        a.AccessBefore==b.AccessBefore&&a.AccessAfter==b.AccessAfter&&
        a.LayoutBefore==b.LayoutBefore&&a.LayoutAfter==b.LayoutAfter&&a.Flags==b.Flags&&
        x.IndexOrFirstMipLevel==y.IndexOrFirstMipLevel&&x.NumMipLevels==y.NumMipLevels&&
        x.FirstArraySlice==y.FirstArraySlice&&x.NumArraySlices==y.NumArraySlices&&
        x.FirstPlane==y.FirstPlane&&x.NumPlanes==y.NumPlanes;
}
// Bounded discovery plus distinct transition tuples. Never wait for a callback
// holding the main probe lock (an exposure dispatch can already hold that lock).
void NoteDistanceVolume(ID3D12GraphicsCommandList7* list,UINT groups,const D3D12_BARRIER_GROUP* data)
{
    if(!observing.load(std::memory_order_relaxed)||!list||!data)return;
    for(UINT g=0;g<groups&&g<64;++g)
    {
        const auto& group=data[g];
        if(group.Type!=D3D12_BARRIER_TYPE_TEXTURE||group.NumBarriers>4096||!group.pTextureBarriers)continue;
        for(UINT i=0;i<group.NumBarriers;++i)
        {
            auto* resource=group.pTextureBarriers[i].pResource;
            if(!resource)continue;
            const auto address=reinterpret_cast<uint64_t>(resource);
            unsigned seen=distanceSeen.load(std::memory_order_acquire);
            bool known=false;
            for(unsigned k=0;k<seen&&k<distanceSightings.size();++k)
                if(distanceSightings[k].resource==address){known=true;break;}
            if(!known)
            {
                if(seen>=distanceSightings.size()||distanceInspections.load(std::memory_order_relaxed)>=DistanceInspectionLimit)continue;
                if(distanceInspections.fetch_add(1,std::memory_order_relaxed)>=DistanceInspectionLimit)continue;
                const auto desc=resource->GetDesc();
                if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE3D||desc.Width!=DistanceWidth||
                    desc.Height!=DistanceHeight||desc.DepthOrArraySize!=DistanceDepth||
                    desc.Format!=DXGI_FORMAT_R16_TYPELESS||desc.MipLevels!=1||
                    desc.SampleDesc.Count!=1||desc.SampleDesc.Quality!=0||
                    !(desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))continue;
            }
            const auto listType=list->GetType();
            if(!TryAcquireSRWLockExclusive(&distanceLock)){++distanceDropped;continue;}
            seen=distanceSeen.load(std::memory_order_relaxed);
            unsigned index=0;
            for(;index<seen;++index)if(distanceSightings[index].resource==address)break;
            const auto& b=group.pTextureBarriers[i];
            if(index==seen&&seen<distanceSightings.size())
            {
                distanceSightings[seen]={address,reinterpret_cast<uint64_t>(list),0,
                    b.LayoutBefore,b.LayoutAfter,b.AccessBefore,b.AccessAfter,
                    b.SyncBefore,b.SyncAfter};
                distanceSeen.store(seen+1,std::memory_order_release);
            }
            if(index>=distanceSightings.size()){ReleaseSRWLockExclusive(&distanceLock);continue;}
            ++distanceSightings[index].barriers;
            unsigned transition=0;
            for(;transition<distanceTransitionCount;++transition)
                if(distanceTransitions[transition].listType==listType&&
                    SameDistanceBarrier(distanceTransitions[transition].barrier,b))break;
            const bool added=transition==distanceTransitionCount&&transition<DistanceTransitionLimit;
            const auto tick=GetTickCount64();
            if(added)
            {
                distanceTransitions[transition]={b,listType,reinterpret_cast<uint64_t>(list),
                    reinterpret_cast<uint64_t>(list),tick,tick,0};
                ++distanceTransitionCount;
            }
            if(transition<DistanceTransitionLimit)
            {
                auto& t=distanceTransitions[transition];++t.occurrences;
                t.lastList=reinterpret_cast<uint64_t>(list);t.lastTick=tick;
            }
            else ++distanceOverflow;
            ReleaseSRWLockExclusive(&distanceLock);
            // Persist each NEW tuple immediately; a missing detach report cannot
            // erase the only release observation. Repeated tuples do not spam IO.
            if(added)
                ch::Log("Spatial probe: signed distance transition %u tick=%llu resource=%llX list=%llX type=%u "
                    "layout %d->%d access %X->%X sync %X->%X flags=%X sub=%u,%u,%u,%u,%u,%u",
                    transition,static_cast<unsigned long long>(tick),
                    static_cast<unsigned long long>(address),
                    static_cast<unsigned long long>(reinterpret_cast<uint64_t>(list)),
                    static_cast<unsigned>(listType),static_cast<int>(b.LayoutBefore),static_cast<int>(b.LayoutAfter),
                    static_cast<unsigned>(b.AccessBefore),static_cast<unsigned>(b.AccessAfter),
                    static_cast<unsigned>(b.SyncBefore),static_cast<unsigned>(b.SyncAfter),static_cast<unsigned>(b.Flags),
                    b.Subresources.IndexOrFirstMipLevel,b.Subresources.NumMipLevels,b.Subresources.FirstArraySlice,
                    b.Subresources.NumArraySlices,b.Subresources.FirstPlane,b.Subresources.NumPlanes);
        }
    }
}
void ArmDistanceRelease(ID3D12GraphicsCommandList7* list,UINT groups,const D3D12_BARRIER_GROUP* data)
{
    if(!distanceMode||!observing||!list||!data||groups>64)return;
    // Copy only the exact COMPUTE SRV release measured in probe.4, on a compute
    // list. The same tuple on a graphics list is not a reason to broaden scope.
    if(list->GetType()!=D3D12_COMMAND_LIST_TYPE_COMPUTE)return;
    for(UINT g=0;g<groups;++g)
    {
        const auto& packet=data[g];
        if(packet.Type!=D3D12_BARRIER_TYPE_TEXTURE||packet.NumBarriers>4096||!packet.pTextureBarriers)continue;
        for(UINT i=0;i<packet.NumBarriers;++i)
        {
            const auto& b=packet.pTextureBarriers[i];
            bool known=false;
            const auto seen=distanceSeen.load(std::memory_order_acquire);
            for(unsigned k=0;k<seen;++k)
                if(distanceSightings[k].resource==reinterpret_cast<uint64_t>(b.pResource)){known=true;break;}
            if(!known||!SpatialReadback::MatchesRelease(b,b.pResource))continue;
            if(!TryAcquireSRWLockExclusive(&lock))return;
            // Context is deliberately CPU-bracketed, not GPU-bound GI. Retain
            // both sides and require offline mapping calibration before any trace.
            const auto now=GetTickCount64();
            if(distanceLatest.giStable&&!distanceLatest.error&&distanceLatest.enhanced&&
                now>=distanceLatest.tick&&now-distanceLatest.tick<=750&&
                publishedTransactions==readback->Completed())
            {
                readback->Discover(list,b.pResource);
                if(readback->Arm(list,b.pResource,distanceLatest.frame))
                {
                    copyObservation=distanceLatest;distanceArmedTick=now;
                    readback->ExposureEnd(true);
                }
            }
            ReleaseSRWLockExclusive(&lock);
        }
    }
}
void STDMETHODCALLTYPE BarrierHook(ID3D12GraphicsCommandList7* list,UINT groups,const D3D12_BARRIER_GROUP* data)
{
    if(distanceMode)
    {
        NoteDistanceVolume(list,groups,data);
        ArmDistanceRelease(list,groups,data);
    }
    if(directReadback)readback->Barrier(list,groups,data,originalBarrier);
    trace.Barrier(list,groups,data); // Entire requested interval, every observed list/thread.
    // Thread-local scope is ONLY the actual AdaptExposure Dispatch invocation.
    // Other engine passes do not acquire a lock or inspect their barrier arrays.
    auto* o=active;
    if(o && observing.load(std::memory_order_relaxed) && reinterpret_cast<uint64_t>(list)==o->nativeList7)
    {
        if(groups>64) o->barrierOverflow=true;
        else for(UINT g=0;g<groups;++g)
        {
            const auto& group=data[g];
            if(group.Type!=D3D12_BARRIER_TYPE_TEXTURE) continue;
            if(group.NumBarriers>4096){o->barrierOverflow=true;continue;}
            for(UINT i=0;i<group.NumBarriers;++i)
            {
                const auto& b=group.pTextureBarriers[i];
                if(reinterpret_cast<uint64_t>(b.pResource)!=o->resource) continue;
                if(o->barrierCount==BarrierLimit){o->barrierOverflow=true;continue;}
                o->barriers[o->barrierCount++]=b;
            }
        }
    }
    if(!distanceMode)NoteDistanceVolume(list,groups,data);
    originalBarrier(list,groups,data); // Never modify or suppress an engine call.
}
HRESULT STDMETHODCALLTYPE ResetHook(ID3D12GraphicsCommandList* list,ID3D12CommandAllocator* allocator,ID3D12PipelineState* state)
{
    const auto address=reinterpret_cast<uint64_t>(list);
    if(directReadback)readback->Reset(list,false);
    trace.Lifecycle(address,TraceKind::ResetBegin);
    const auto hr=originalReset(list,allocator,state);
    if(directReadback)readback->Reset(list,true,hr);
    trace.Lifecycle(address,TraceKind::ResetEnd,hr);
    return hr;
}
HRESULT STDMETHODCALLTYPE CloseHook(ID3D12GraphicsCommandList* list)
{
    const auto address=reinterpret_cast<uint64_t>(list);
    if(directReadback)readback->Close(list,false);
    trace.Lifecycle(address,TraceKind::CloseBegin);
    const auto hr=originalClose(list);
    if(directReadback)readback->Close(list,true,hr);
    trace.Lifecycle(address,TraceKind::CloseEnd,hr);
    return hr;
}
void Submission(ID3D12CommandQueue* queue,UINT listCount,ID3D12CommandList* const* lists,bool after)
{
    if(directReadback)readback->Submit(queue,listCount,lists,after);
    trace.Submit(queue,listCount,lists,after);
}
bool Resolve(Observation& o)
{
    uint64_t renderer{}, back{}, outer{}, storage{}, holder{}, sceneOwner{}, sceneData{}, device{}, samplerArray{}, exposureOwner{};
    uint32_t width{},height{},depth{},samplerCount{};
    uint8_t bank{}, enhanced{};
    if(!Read(o.owner+0x10,renderer)||!Read(renderer+0x660,back)||back!=o.owner||
        !Read(o.owner+0x4B8,outer)||!Read(outer+0x30,storage)||
        !Read(storage+0x10,back)||back!=outer||!Read(storage+0x100,o.resource)||!o.resource||
        !Read(storage+0xD0,width)||!Read(storage+0xD4,height)||!Read(storage+0xD8,depth)||
        width!=64||height!=32||depth!=264||!Read(o.command+0x800,holder)||
        !Read(holder+8,o.nativeList)||!o.nativeList||!Read(o.owner,device)||
        !Read(device+0x43,enhanced)||!Read(device+0x970,samplerArray)||
        !Read(device+0x978,samplerCount)||samplerCount!=13||
        !Read(samplerArray+12*sizeof(D3D12_STATIC_SAMPLER_DESC),o.sampler)||
        !Read(o.owner+8,sceneOwner)||!Read(sceneOwner+0x428,sceneData)||!Read(sceneData,o.scene)||
        !Read(o.owner+0x20,o.gi)||!Read(o.owner+0x705,bank)) return false;
    o.bank=bank; o.enhanced=enhanced!=0;
    Read(storage+0xB0,o.textureDescriptor);
    uint64_t cbWrapper{},cbOuter{},cbStorage{},viewTable{},viewObject{};
    uint32_t viewCount{};
    if(!Read(o.owner+(bank?0x568:0x560),cbWrapper)||!Read(cbWrapper+0x18,cbOuter)||
        !Read(cbOuter+0x30,cbStorage)||!Read(cbStorage+0x168,o.giGpuResource)) return false;
    if(directReadback&&!distanceMode&&o.giGpuResource)
    {
        auto* cb=reinterpret_cast<ID3D12Resource*>(o.giGpuResource);
        o.giDesc=cb->GetDesc();D3D12_HEAP_FLAGS flags{};
        o.giHeapResult=cb->GetHeapProperties(&o.giHeap,&flags);
    }
    // Raw descriptor provenance only; no guessed SRV format interpretation.
    if(Read(storage+0xE8,viewTable)&&Read(storage+0xF0,viewCount)&&viewCount>0&&viewCount<=16)
    {
        Read(viewTable,o.viewEntry);
        if(Read(viewTable+0x20,viewObject)) Read(viewObject,o.viewDescriptor);
    }
    std::memcpy(&o.frame,o.scene.data()+0x20,4);
    if(Read(renderer+0x690,exposureOwner)) Read(exposureOwner+0xD8,o.exposure);
    if(directReadback&&!distanceMode)
    {
        uint64_t exposureOuter{},exposureStorage{},exposureBack{};uint32_t stride{},elements{};
        if(!Read(exposureOwner+0x10,exposureBack)||exposureBack!=renderer||
            !Read(exposureOwner+0xC0,exposureOuter)||!Read(exposureOuter+0x30,exposureStorage)||
            !Read(exposureStorage+0x10,exposureBack)||exposureBack!=exposureOuter||
            !Read(exposureStorage+0xC0,stride)||stride!=4||!Read(exposureStorage+0xC4,elements)||elements!=32||
            !Read(exposureStorage+0x168,o.exposureGpuResource)||!o.exposureGpuResource)return false;
    }
    // Valid engine-owned objects are still alive inside their consuming call.
    auto* resource=reinterpret_cast<ID3D12Resource*>(o.resource);
    auto* list=reinterpret_cast<ID3D12GraphicsCommandList*>(o.nativeList);
    o.desc=resource->GetDesc();
    if(o.desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE3D||o.desc.Width!=64||
        o.desc.Height!=32||o.desc.DepthOrArraySize!=264||o.desc.MipLevels!=1||
        o.desc.Format!=DXGI_FORMAT_R8_TYPELESS||o.sampler.ShaderRegister!=12||o.sampler.RegisterSpace!=4)
        return false;
    ID3D12GraphicsCommandList7* list7{};
    if(FAILED(list->QueryInterface(IID_PPV_ARGS(&list7)))) return false;
    o.nativeList7=reinterpret_cast<uint64_t>(list7);
    // SDK C-interface offsetof is independently checked by the host test.
    o.barrierFunction=(*reinterpret_cast<void***>(list7))[80];
    o.closeFunction=(*reinterpret_cast<void***>(list7))[9];
    o.resetFunction=(*reinterpret_cast<void***>(list7))[10];
    const auto vtable=*reinterpret_cast<void***>(list7);
    o.bindingFunctions={vtable[37],vtable[41],vtable[29],vtable[14],vtable[39],vtable[31],vtable[28]};
    list7->Release();
    return o.barrierFunction!=nullptr;
}
nlohmann::json BarrierJson(const D3D12_TEXTURE_BARRIER& b)
{
    return {{"syncBefore",b.SyncBefore},{"syncAfter",b.SyncAfter},
            {"accessBefore",b.AccessBefore},{"accessAfter",b.AccessAfter},
            {"layoutBefore",b.LayoutBefore},{"layoutAfter",b.LayoutAfter},
            {"flags",b.Flags},{"subresources",{{"indexOrFirstMip",b.Subresources.IndexOrFirstMipLevel},
            {"numMips",b.Subresources.NumMipLevels},{"firstArray",b.Subresources.FirstArraySlice},
            {"numArrays",b.Subresources.NumArraySlices},{"firstPlane",b.Subresources.FirstPlane},
            {"numPlanes",b.Subresources.NumPlanes}}}};
}
nlohmann::json Json(const Observation& o)
{
    nlohmann::json barriers=nlohmann::json::array();
    for(unsigned i=0;i<o.barrierCount;++i)barriers.push_back(BarrierJson(o.barriers[i]));
    return {{"capturedTick",o.tick},{"frame",o.frame},{"error",o.error},{"owner",o.owner},
        {"command",o.command},{"resource",o.resource},{"nativeList",o.nativeList},{"nativeList7",o.nativeList7},
        {"enhancedBarriers",o.enhanced},{"bankFlag",o.bank},{"giGpuResource",o.giGpuResource},{"giCopiesMatch",o.giStable},
        {"exposureGpuResource",o.exposureGpuResource},
        {"gpuCopyIssued",false},{"gpuFramePaired",false},{"barrierHookInstalled",barrierInstalled},
        {"selectedForTextureReadback",o.selectedForReadback},
        {"giResourceMetadata",{{"dimension",o.giDesc.Dimension},{"width",o.giDesc.Width},
            {"heapResult",static_cast<uint32_t>(o.giHeapResult)},{"heapType",o.giHeap.Type},
            {"cpuPageProperty",o.giHeap.CPUPageProperty},{"memoryPool",o.giHeap.MemoryPoolPreference}}},
        {"resourceDescription",{{"dimension",o.desc.Dimension},{"width",o.desc.Width},
            {"height",o.desc.Height},{"depth",o.desc.DepthOrArraySize},{"mips",o.desc.MipLevels},
            {"format",o.desc.Format},{"flags",o.desc.Flags}}},
        {"samplerHex",Hex(reinterpret_cast<const uint8_t*>(&o.sampler),sizeof(o.sampler))},
        {"giBeforeHex",Hex(o.gi.data(),o.gi.size())},{"giAfterHex",Hex(o.giAfter.data(),o.giAfter.size())},
        {"sceneHex",Hex(o.scene.data(),o.scene.size())},{"exposureCacheHex",Hex(o.exposure.data(),o.exposure.size())},
        {"srvViewEntryRawHex",Hex(o.viewEntry.data(),o.viewEntry.size())},
        {"srvViewObjectRawHex",Hex(o.viewDescriptor.data(),o.viewDescriptor.size())},
        {"textureDescriptorRawHex",Hex(o.textureDescriptor.data(),o.textureDescriptor.size())},
        {"observedTextureBarriers",barriers},{"barrierTraceOverflow",o.barrierOverflow}};
}
nlohmann::json TraceJson()
{
    // Stop accepting events before taking a stable worker-side snapshot.
    trace.target=0;
    AcquireSRWLockExclusive(&trace.mutex);
    nlohmann::json events=nlohmann::json::array();
    constexpr const char* names[]={"barrier","reset-begin","reset-end","close-begin","close-end",
        "execute-begin","execute-end","exposure-begin","exposure-end"};
    for(size_t i=0;i<trace.eventCount;++i)
    {
        const auto& e=trace.events[i];
        nlohmann::json row={{"order",e.order},{"tick",e.tick},{"thread",e.thread},
            {"list",e.list},{"queue",e.queue},{"resetGeneration",e.generation},
            {"resetGenerationKnown",e.generationKnown},{"kind",names[static_cast<unsigned>(e.kind)]},
            {"hresult",static_cast<uint32_t>(e.result)}};
        if(e.kind==TraceKind::Barrier)
        {
            row["barrier"]=BarrierJson(e.barrier);
        }
        events.push_back(std::move(row));
    }
    nlohmann::json result={{"targetResource",reinterpret_cast<uint64_t>(traceResource)},
        {"startedTick",trace.startedTick},{"endedTick",GetTickCount64()},
        {"hooks",{{"barrier",reinterpret_cast<uint64_t>(barrierTarget)},
            {"reset",reinterpret_cast<uint64_t>(resetTarget)},{"close",reinterpret_cast<uint64_t>(closeTarget)}}},
        {"barrierCalls",trace.barrierCalls},{"textureEntries",trace.textureEntries},
        {"targetBarriers",trace.targetBarriers},{"resetCalls",trace.resetCalls},
        {"closeCalls",trace.closeCalls},{"executeCalls",trace.executeCalls},
        {"droppedCallbacks",trace.lost.load()},{"overflow",trace.overflow},{"events",events},
        {"gpuCompletionKnown",false},{"currentLayoutKnown",false},
        {"scope","CPU calls through discovered Barrier/Reset/Close implementations and existing queue detour; not complete device coverage or GPU ordering. Reset generation is NOT object-lifetime identity. No fence/copy added."}};
    ReleaseSRWLockExclusive(&trace.mutex);
    return result;
}
// The GPU GI buffer is copied whole because no binding gives us an offset. The
// window is located the same way the offline decoder does it: by matching the CPU
// copy of the same constants, at 256-byte alignment, and only when it is unique.
long long FindGiWindow(const std::vector<uint8_t>& gpu,const std::array<uint8_t,768>& cpu)
{
    if(gpu.size()<cpu.size())return -1;
    long long found=-1;
    for(size_t at=0;at+cpu.size()<=gpu.size();at+=256)
        if(std::memcmp(gpu.data()+at,cpu.data(),cpu.size())==0)
        {
            if(found>=0)return -2; // ambiguous; never pick one
            found=static_cast<long long>(at);
        }
    return found;
}
// Contract pattern: 26 normalized directions at two radii. Every combination of
// -1,0,+1 except the centre, so six axes, twelve edges and eight corners, which
// keeps the widest angular gap near45 degrees. Two radii because the field falls
// off sharply over a few units. Well inside the toroidal wrap bound (|x|,|z|<=16,
// |y|<=8). Entries are NOT necessarily the same age; neighbouring offsets can lie
// in different amortised update blocks of the volume.
constexpr double SampleRadii[]={3.0,6.0};
std::vector<std::array<double,3>> BuildSampleOffsets()
{
    std::vector<std::array<double,3>> offsets;
    for(double radius:SampleRadii)
        for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z)
        {
            if(!x&&!y&&!z)continue;
            const double length=std::sqrt(double(x*x+y*y+z*z));
            offsets.push_back({radius*x/length,radius*y/length,radius*z/length});
        }
    return offsets;
}
const std::vector<std::array<double,3>> SampleOffsets=BuildSampleOffsets();
// How many transactions a run needs. The live channel is a SEPARATE duration
// from the retained snapshot count, because how long a value keeps refreshing and
// how much history is kept are different questions; one key answering both once
// made a request mean its own opposite. Never returns less than the retained
// count, and never more than the series cap.
unsigned SeriesLength(unsigned retained,unsigned publishSeconds,uint64_t intervalMs)
{
    const uint64_t interval=intervalMs?intervalMs:1000;
    const uint64_t wanted=(uint64_t{publishSeconds}*1000+interval-1)/interval;
    const uint64_t series=wanted>retained?wanted:retained;
    return series>cdt::spatial::SpatialReadback::MaxSeriesTransactions
        ?cdt::spatial::SpatialReadback::MaxSeriesTransactions:static_cast<unsigned>(series);
}

// Publishes the reference sky visibility of each newly completed transaction so
// an ambient consumer sees a live value WHILE a run is in progress. It adds no
// copy, no hook and no GPU work: it reads a transaction the probe already made,
// so the probe stays a bounded research instrument and the value stops with it.
void PublishReferenceVisibility()
{
    using namespace cdt::spatial;
    const auto completed=readback?readback->Completed():0u;
    if(completed==publishedTransactions)return;
    publishedTransactions=completed;
    const auto records=readback->Records();
    const auto unavailable=[]{sky::PublishVisibility(0.0,sky::Visibility::Unavailable,0,0);};
    if(records.empty()){unavailable();return;}
    const auto& copy=records.back();
    if(copy.packed.size()!=VolumeBytes||copy.gpuGi.empty()){unavailable();return;}
    // Pair by CONTENT, not by position. FindGiWindow already proves a pairing, and
    // the two rings can legitimately differ in length while a copy is in flight,
    // so an index would be an assumption where a check is available.
    long long window=-1;
    const Observation* observation=nullptr;
    for(auto it=copyObservations.rbegin();it!=copyObservations.rend();++it)
    {
        const auto found=FindGiWindow(copy.gpuGi,it->gi);
        if(found>=0){window=found;observation=&*it;break;}
    }
    if(!observation){unavailable();return;}
    const auto reference=SampleAtReference(copy.gpuGi.data()+window,copy.packed.data());
    // Fallback is one BY DEFINITION, not a measurement. Publishing it as a value
    // would restore full ambient exactly where the local sample is missing.
    const auto state=reference.status==SampleStatus::Ok?sky::Visibility::Valid
        :reference.status==SampleStatus::Fallback?sky::Visibility::Fallback
        :sky::Visibility::Unavailable;
    sky::PublishVisibility(reference.skyVisibility,state,observation->frame,
        copy.completedTick?copy.completedTick:GetTickCount64());
}
nlohmann::json NativeSamplesJson(const cdt::spatial::CopyResult& copy,const Observation& observation)
{
    using namespace cdt::spatial;
    nlohmann::json out={{"available",false}};
    if(copy.packed.size()!=VolumeBytes||copy.gpuGi.empty())return out;
    const auto window=FindGiWindow(copy.gpuGi,observation.gi);
    out["giWindowOffset"]=window;
    if(window<0)
    {
        out["reason"]=window==-2?"gi-window-ambiguous":"gi-window-absent";
        return out;
    }
    const uint8_t* constants=copy.gpuGi.data()+window;
    const auto reference=SampleAtReference(constants,copy.packed.data());
    out["status"]=static_cast<int>(reference.status);
    out["clipmap"]=reference.clipmap;
    out["world"]={reference.world[0],reference.world[1],reference.world[2]};
    if(reference.status!=SampleStatus::Ok)
    {
        out["reason"]=reference.status==SampleStatus::Fallback?"fallback-one-without-coverage":"constants-rejected";
        return out;
    }
    out["available"]=true;
    out["reference"]=reference.skyVisibility;
    nlohmann::json offsets=nlohmann::json::array();
    for(const auto& delta:SampleOffsets)
    {
        const double world[3]={reference.world[0]+delta[0],reference.world[1]+delta[1],
            reference.world[2]+delta[2]};
        const auto at=SampleAtWorld(constants,copy.packed.data(),world);
        offsets.push_back({{"delta",{delta[0],delta[1],delta[2]}},
            {"status",static_cast<int>(at.status)},{"skyVisibility",at.skyVisibility}});
    }
    out["offsets"]=offsets;
    out["caveat"]="Candidate engine sky-visibility factor computed natively from the same fenced "
        "volume, for comparison against the offline decoder. NOT irradiance, room brightness, a "
        "fraction of visible sky or per-source occlusion. Offsets reuse the reference clipmap and "
        "may read voxels of differing age because the volume refreshes in amortised blocks.";
    return out;
}
nlohmann::json TransactionJson(const cdt::spatial::CopyResult& copy,bool includeHex=true)
{
    const auto& f=copy.footprint;
    nlohmann::json row={{"status",copy.reason},{"hresult",static_cast<uint32_t>(copy.error)},
        {"gpuCopyIssued",copy.issued},{"gpuCompleted",copy.gpuCompleted},{"giGpuFramePaired",copy.buffersGpuPaired},
        {"exposureGpuFramePaired",copy.buffersGpuPaired},{"frame",copy.frame},{"resetGeneration",copy.generation},
        {"queue",copy.queue},{"recordingThread",copy.recordingThread},{"submissionThread",copy.submissionThread},
        {"releaseTick",copy.releaseTick},{"submitTick",copy.submitTick},{"completedTick",copy.completedTick},
        {"fenceValue",copy.fenceValue},{"mapCalls",copy.mapCalls},{"releaseBarrier",BarrierJson(copy.release)},
        {"allocationBytes",copy.allocationBytes},{"footprint",{{"offset",f.Offset},{"format",f.Footprint.Format},
            {"width",f.Footprint.Width},{"height",f.Footprint.Height},{"depth",f.Footprint.Depth},{"rowPitch",f.Footprint.RowPitch}}},
        {"packing","uint8 x-fastest, then y, then z; row padding removed; resource R8_TYPELESS"},
        {"packedTextureHex",includeHex?Hex(copy.packed.data(),copy.packed.size()):""}};
    row["bufferPair"]={{"requested",copy.buffersRequested},{"nativeDispatchSeen",copy.nativeDispatchSeen},
        {"nativeDispatches",copy.nativeDispatches},{"copied",copy.buffersCopied},{"giResource",copy.giResource},
        {"exposureResource",copy.exposureResource},{"giBase",copy.giBase},{"exposureBase",copy.exposureBase},
        {"giOffset",copy.giOffset},{"exposureOffset",copy.exposureOffset},{"giRootIndex",copy.giRootIndex},
        {"exposureRootIndex",copy.exposureRootIndex},{"readbackOffset",copy.pairReadbackOffset},
        {"nativeCbv",copy.nativeCbv},{"nativeUav",copy.nativeUav},{"nativeSrv",copy.nativeSrv},
        {"nativeTable",copy.nativeTable},{"descriptorHeaps",copy.descriptorHeaps},{"giFromSrv",copy.giFromSrv},
        {"rootThreadConflict",copy.rootThreadConflict},{"tableSets",copy.tableSets},{"heapSets",copy.heapSets},
        {"rootSetsBeforeExposure",copy.rootSetsBeforeExposure},{"rootSetsInsideExposure",copy.rootSetsInsideExposure},
        {"giBindingHits",copy.giBindingHits},{"exposureBindingHits",copy.exposureBindingHits},
        {"giBytes",copy.giBytes},{"exposureBytes",copy.exposureBytes},
        {"pairing","same-submission-not-binding-proven"},
        {"pairingCaveat","Whole pinned buffers copied on the same list and submission immediately "
            "after the selected native dispatch, under one fence. Resource identity comes from the "
            "validated native producer/consumer path, NOT from an observed root binding: this shader "
            "binds through descriptor tables. Window offsets are resolved offline against the CPU "
            "copies and are not read from any binding. Root fields are corroboration only."},
        {"giHex",copy.buffersGpuPaired?Hex(copy.gpuGi.data(),copy.gpuGi.size()):""},
        {"exposureHex",copy.buffersGpuPaired?Hex(copy.gpuExposure.data(),copy.gpuExposure.size()):""}};
    return row;
}
bool WriteNewEvidence(const std::filesystem::path& path,const void* data,size_t size)
{
    if(size>MAXDWORD)return false;
    const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written{};
    const bool ok=WriteFile(file,data,static_cast<DWORD>(size),&written,nullptr)&&written==size;
    CloseHandle(file);return ok;
}
void SaveDistanceTransaction()
{
    const auto completed=readback->Completed();
    Observation before{},after{};
    AcquireSRWLockExclusive(&lock);
    if(!completed||completed==publishedTransactions||
        (distanceLatest.tick<=distanceArmedTick&&GetTickCount64()-distanceArmedTick<2000))
    {ReleaseSRWLockExclusive(&lock);return;}
    before=copyObservation;after=distanceLatest;
    ReleaseSRWLockExclusive(&lock);
    const auto copy=readback->LatestRecord();
    const auto stem=L"signed-distance-"+std::to_wstring(GetCurrentProcessId())+L"-"+
        std::to_wstring(started)+L"-"+std::to_wstring(completed);
    auto metadata=TransactionJson(copy,false);
    metadata.erase("packedTextureHex");metadata.erase("bufferPair");
    metadata["format"]="private-signed-distance-readback-v1";
    metadata["pid"]=GetCurrentProcessId();
    metadata["executableSha256"]=Hex(native_contract::ExecutableSha256.data(),native_contract::ExecutableSha256.size());
    metadata["packing"]="little-endian binary16, x-fastest then y then z; row padding removed; R16_TYPELESS viewed as R16_FLOAT";
    metadata["packedBytes"]=copy.packed.size();
    metadata["resource"]=reinterpret_cast<uint64_t>(copy.release.pResource);
    metadata["dataFile"]=std::filesystem::path(stem+L".bin").string();
    metadata["contextBefore"]=Json(before);metadata["contextAfter"]=Json(after);
    metadata["cpuContextBracketsCopy"]=before.tick<=copy.releaseTick&&after.tick>copy.releaseTick&&
        before.frame!=after.frame&&before.giStable&&after.giStable;
    metadata["frameMeaning"]="preceding CPU exposure context frame, NOT a GPU-paired distance-volume frame";
    metadata["caveat"]="Fenced live R16 copy at a shape-matched resource's observed SRV release. "
        "CPU GI/scene observations bracket recording when flagged; neither is an observed GPU binding. "
        "Validate mapping stability and known geometry before tracing. Voxel content age is unknown. No visibility verdict.";
    const auto bin=outputDirectory/(stem+L".bin"),json=outputDirectory/(stem+L".json");
    const bool rawOk=copy.gpuCompleted&&copy.packed.size()==17039360&&
        WriteNewEvidence(bin,copy.packed.data(),copy.packed.size());
    metadata["rawSaved"]=rawOk;
    const auto text=metadata.dump(2);
    const bool metaOk=WriteNewEvidence(json,text.data(),text.size());
    AcquireSRWLockExclusive(&lock);
    publishedTransactions=completed;
    if(!rawOk||!metaOk){incomplete=true;readback->Cancel("distance-evidence-write-failed");}
    ReleaseSRWLockExclusive(&lock);
    ch::Log("Signed distance readback %u: %s, bytes=%llu fence=%llu contexts=%u/%u bracket=%d, %s",
        completed,rawOk&&metaOk?"saved":"WRITE FAILED",static_cast<unsigned long long>(copy.packed.size()),
        static_cast<unsigned long long>(copy.fenceValue),before.frame,after.frame,
        metadata["cpuContextBracketsCopy"].get<bool>()?1:0,json.string().c_str());
}
void Save(const char* reason)
{
    if(directReadback)readback->Cancel("capture-window-ended");
    if(distanceMode)
    {
        const auto state=readback->Snapshot();
        ch::Log("Signed distance run ended: %s, copy=%s completed=%u saved=%u; individual evidence files retained.",
            reason,state.reason,readback->Completed(),publishedTransactions);
        if(traceResource){traceResource->Release();traceResource=nullptr;}
        observing=false;phase=Phase::Idle;return;
    }
    const auto copy=readback->Snapshot();
    const bool progressing=samples.size()>1 && samples.front()["frame"]!=samples.back()["frame"];
    nlohmann::json report={{"format",directReadback?"private-spatial-readback-v5":"private-spatial-binding-v2"},{"pid",GetCurrentProcessId()},
        {"executableSha256",Hex(native_contract::ExecutableSha256.data(),native_contract::ExecutableSha256.size())},
        {"reason",reason},{"complete",!incomplete&&count>=Limit&&(!directReadback||
            (copy.phase==CopyPhase::Complete&&readback->Completed()==readbackCount))},
        {"controlProgressed",progressing},{"gpuCopyIssued",directReadback&&copy.issued},
        {"caveat",directReadback?
            "Instrumented transaction series. Each entry is an independent same-submission copy under its own fence value, reusing one readback destination that is never re-armed before the previous map finished. Repeats measure spread at one place; they are NOT a longer exposure or an average. CPU scene/cache remains unpaired; no local brightness or source visibility API.":
            "Passive instrumented run, not untouched baseline. Per-sample barriers cover only Dispatch; intervalTrace covers discovered implementations across the requested interval, with explicit losses/unknown generations. Neither proves GPU completion/current layout. CPU constants/cache are NOT GPU-frame paired."},
        {"samples",samples},{"intervalTrace",directReadback?nlohmann::json(nullptr):TraceJson()}};
    if(directReadback)
    {
        const auto records=readback->Records();
        nlohmann::json transactions=nlohmann::json::array();
        for(size_t i=0;i<records.size();++i)
        {
            auto row=TransactionJson(records[i]);
            // Each transaction carries the CPU observation of ITS OWN exposure.
            if(i<copyObservations.size())
            {
                row["context"]=Json(copyObservations[i]);
                row["nativeSamples"]=NativeSamplesJson(records[i],copyObservations[i]);
            }
            transactions.push_back(std::move(row));
        }
        report["transactions"]=transactions;
        {
            // Where the signed distance volume was seen, if at all. A future fenced
            // copy needs a list and a layout it can legally transition from; this
            // says whether either is reachable from the hook we already own.
            nlohmann::json found=nlohmann::json::array();
            nlohmann::json transitions=nlohmann::json::array();
            AcquireSRWLockExclusive(&distanceLock);
            const auto seen=distanceSeen.load(std::memory_order_acquire);
            for(unsigned i=0;i<seen&&i<distanceSightings.size();++i)
            {
                const auto& d=distanceSightings[i];
                found.push_back({{"resource",d.resource},{"commandList",d.list},
                    {"barriers",d.barriers},{"layoutBefore",static_cast<int>(d.before)},
                    {"layoutAfter",static_cast<int>(d.after)},
                    {"accessBefore",static_cast<unsigned>(d.accessBefore)},
                    {"accessAfter",static_cast<unsigned>(d.accessAfter)},
                    {"syncBefore",static_cast<unsigned>(d.syncBefore)},
                    {"syncAfter",static_cast<unsigned>(d.syncAfter)}});
            }
            for(unsigned i=0;i<distanceTransitionCount;++i)
            {
                const auto& t=distanceTransitions[i];
                auto row=BarrierJson(t.barrier);
                row["resource"]=reinterpret_cast<uint64_t>(t.barrier.pResource);
                row["listType"]=t.listType;row["firstList"]=t.firstList;row["lastList"]=t.lastList;
                row["firstTick"]=t.firstTick;row["lastTick"]=t.lastTick;row["occurrences"]=t.occurrences;
                transitions.push_back(std::move(row));
            }
            ReleaseSRWLockExclusive(&distanceLock);
            report["distanceVolume"]={{"searched",{{"width",DistanceWidth},{"height",DistanceHeight},
                {"depth",DistanceDepth},{"format",static_cast<int>(DXGI_FORMAT_R16_TYPELESS)}}},
                {"inspections",distanceInspections.load(std::memory_order_relaxed)},
                {"inspectionLimit",DistanceInspectionLimit},
                {"sightings",found},
                {"transitions",transitions},{"transitionLimit",DistanceTransitionLimit},
                {"droppedCallbacks",distanceDropped.load()},{"overflowOccurrences",distanceOverflow.load()},
                {"note","passive distinct barrier tuples, not GPU ordering/lifetime proof; sightings keep the first tuple only; AdaptExposureCS does not bind this volume"}};
        }
        report["requestedTransactions"]=requestedTransactions;
        report["retainedTransactions"]=readbackCount;
        report["seriesTransactions"]=seriesCount;
        report["visibilityPublishSeconds"]=visibilitySeconds;
        report["completedTransactions"]=readback->Completed();
        report["transactionIntervalMilliseconds"]=readbackIntervalMs;
        // Latest transaction repeated at the v4 location so existing readers keep working.
        report["textureReadback"]=TransactionJson(copy);
        report["textureReadback"]["context"]=Json(copyObservation);
        report["textureReadback"]["nativeSamples"]=NativeSamplesJson(copy,copyObservation);
    }
    if(traceResource){traceResource->Release();traceResource=nullptr;}
    const auto file=outputDirectory/(L"spatial-binding-"+std::to_wstring(GetCurrentProcessId())+L"-"+
        std::to_wstring(started)+L"-"+std::to_wstring(++run)+L".json");
    HANDLE h=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h!=INVALID_HANDLE_VALUE)
    {
        const auto text=report.dump(2);DWORD written{};
        const bool ok=WriteFile(h,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)&&written==text.size();
        CloseHandle(h);ch::Log("Spatial binding probe: %s, %u/%u observations, %s",ok?"saved":"WRITE FAILED",count,Limit,file.string().c_str());
    }
    else ch::Log("Spatial binding probe could not create output: %lu",GetLastError());
    observing=false;phase=Phase::Idle;
}
}

bool Start(uint64_t moduleBase,const wchar_t* directory,bool enableReadback,
    unsigned transactions,unsigned intervalMilliseconds,unsigned publishSeconds,bool enableDistanceReadback)
{
    if(enabled) return false;
    std::array<uint8_t,Signature.size()> bytes{};
    if(!Read(moduleBase+DispatchRva,bytes)||bytes!=Signature) return false;
    base=moduleBase;outputDirectory=directory;distanceMode=enableDistanceReadback;
    directReadback=enableReadback||distanceMode;
    // Out-of-range configuration falls back to the proven single transaction.
    // SpatialReadbackCount keeps its own meaning: RETAINED diagnostic snapshots.
    // Clamp, never collapse -- asking for more than the maximum used to yield ONE
    // transaction, so an over-large request silently produced the least data
    // instead of the most. That cost a live test.
    requestedTransactions=transactions;
    readbackCount=transactions<1?1u
        :transactions>cdt::spatial::SpatialReadback::MaxTransactions
            ?cdt::spatial::SpatialReadback::MaxTransactions:transactions;
    visibilitySeconds=publishSeconds;
    readbackIntervalMs=(intervalMilliseconds>=250&&intervalMilliseconds<=10000)?intervalMilliseconds:1000;
    // Both inputs are assigned ABOVE. Computing this before them read their
    // defaults and silently produced a series of eight for a request of nine
    // hundred, which is why the arithmetic now lives in a testable function.
    seriesCount=SeriesLength(readbackCount,visibilitySeconds,readbackIntervalMs);
    if(distanceMode)
    {
        seriesCount=transactions<1?1u:transactions>120?120u:transactions;
        readbackCount=1;visibilitySeconds=0;
    }
    const std::string readbackSeriesMessage="Spatial readback v5 IDLE: explicit event starts"+
        std::to_string(Limit)+" CPU controls plus "+std::to_string(seriesCount)+
        " texture/GI/exposure transaction(s), "+std::to_string(readbackCount)+
        " retained, at >="+std::to_string(readbackIntervalMs)+
        "ms; same-submission pairing, whole buffers, offsets resolved offline.";
    dispatchTarget=reinterpret_cast<void*>(base+DispatchRva);
    const auto name=L"Local\\CrimsonDesertTelemetry.SpatialProbe."+std::to_wstring(GetCurrentProcessId());
    requestEvent=CreateEventW(nullptr,FALSE,FALSE,name.c_str());
    if(!requestEvent) return false;
    if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(requestEvent);requestEvent=nullptr;return false;}
    const auto init=MH_Initialize();
    if((init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)||
        MH_CreateHook(dispatchTarget,CdtSpatialThunk,reinterpret_cast<void**>(&originalDispatch))!=MH_OK||
        MH_EnableHook(dispatchTarget)!=MH_OK)
    {CloseHandle(requestEvent);requestEvent=nullptr;return false;}
    enabled=true;
    render::submissionObserver=Submission;
    if(distanceMode)ch::Log("Signed distance readback IDLE: explicit event starts %u R16 copies at >=%llums; "
        "one retained, immediate binary files, CPU-bracketed context only; no sky publication.",seriesCount,
        static_cast<unsigned long long>(readbackIntervalMs));
    else ch::Log(directReadback?
        readbackSeriesMessage.c_str():
        "Spatial binding probe v2 IDLE: passive interval Barrier/Reset/Close/submission trace; explicit event starts20 samples, no GPU copy.");
    return true;
}
void Poll()
{
    if(!enabled) return;
    if(directReadback)readback->Poll();
    // Outside our own lock, and the sky lock is not held here: SRW exclusive is
    // not recursive, so a nested acquisition would deadlock rather than fail.
    if(distanceMode)SaveDistanceTransaction();
    else if(directReadback)PublishReferenceVisibility();
    AcquireSRWLockExclusive(&lock);
    // `observing` is what "a run is in progress" means. The phase is NOT: the
    // dispatch hook sets it to Pending on every observed frame, so after a run ends
    // it only passes through Idle for a fraction of a frame and a request would be
    // consumed and silently discarded almost every time. That, rather than the
    // readback's own guard, is what really made a second run need a game restart.
    if(requestEvent&&WaitForSingleObject(requestEvent,0)==WAIT_OBJECT_0)
    {
        if(observing.load(std::memory_order_relaxed))
            ch::Log("Spatial probe: request ignored, a run is already in progress.");
        else if(!directReadback||readback->Begin(seriesCount,readbackIntervalMs,distanceMode?1u:SpatialReadback::MaxTransactions))
        {samples=nlohmann::json::array();copyObservation={};copyObservations.clear();count=0;incomplete=false;trace.Begin(0);started=GetTickCount64();lastAttempt=0;phase=Phase::Ready;observing=true;
         publishedTransactions=0;ch::Log("Spatial probe: run started.");}
        else ch::Log("Spatial probe: readback refused a new series; the previous one is not settled.");
    }
    void* install{};
    void* installReset{};
    void* installClose{};
    std::array<void*,BindingHooks> installBindings{};
    if(phase==Phase::Pending)
    {
        // Between runs the hook keeps setting Pending, so the bookkeeping and the
        // finish test only apply while a run is actually observing. Installing the
        // hooks stays unconditional: it happens once and is needed either way.
        const bool inRun=observing.load(std::memory_order_relaxed);
        if(inRun)
        {
            if(count<Limit)samples.push_back(Json(pending));
            ++count;
        }
        if(!barrierInstalled&&!pending.error)
        {install=pending.barrierFunction;installReset=pending.resetFunction;installClose=pending.closeFunction;installBindings=pending.bindingFunctions;}
        phase=Phase::Ready;
        if(inRun&&count>=Limit&&(!directReadback||(readback->SeriesFinished()&&
            (!distanceMode||publishedTransactions==readback->Completed())))) Save("sample-limit");
    }
    // The guard exists to end a run that never completes, not to cap a series the
    // caller legitimately asked for. A long series must be able to outlive 30s.
    const uint64_t budget=30000+(directReadback?uint64_t{seriesCount}*readbackIntervalMs:0);
    if(observing&&GetTickCount64()-started>budget){incomplete=true;Save("timeout");}
    ReleaseSRWLockExclusive(&lock);
    // MinHook suspends threads: never hold our capture lock during patching.
    if(install)
    {
        // All three implementations come from the SAME live command list.
        // Queue observations reuse the existing capture hook instead of patching it twice.
        const auto reset=installReset,close=installClose;
        const bool madeBarrier=MH_CreateHook(install,BarrierHook,reinterpret_cast<void**>(&originalBarrier))==MH_OK;
        const bool madeReset=madeBarrier&&MH_CreateHook(reset,ResetHook,reinterpret_cast<void**>(&originalReset))==MH_OK;
        const bool madeClose=madeReset&&MH_CreateHook(close,CloseHook,reinterpret_cast<void**>(&originalClose))==MH_OK;
        bool ok=madeClose&&MH_EnableHook(install)==MH_OK&&MH_EnableHook(reset)==MH_OK&&MH_EnableHook(close)==MH_OK;
        std::array<bool,BindingHooks> madeBindings{};
        if(ok&&directReadback)
        {
            const std::array<void*,BindingHooks> hooks={reinterpret_cast<void*>(CbvHook),reinterpret_cast<void*>(UavHook),
                reinterpret_cast<void*>(SignatureHook),reinterpret_cast<void*>(NativeDispatchHook),
                reinterpret_cast<void*>(SrvHook),reinterpret_cast<void*>(TableHook),reinterpret_cast<void*>(HeapsHook)};
            const std::array<void**,BindingHooks> originals={reinterpret_cast<void**>(&originalCbv),reinterpret_cast<void**>(&originalUav),
                reinterpret_cast<void**>(&originalSignature),reinterpret_cast<void**>(&originalNativeDispatch),
                reinterpret_cast<void**>(&originalSrv),reinterpret_cast<void**>(&originalTable),reinterpret_cast<void**>(&originalHeaps)};
            for(size_t i=0;i<BindingHooks&&ok;++i)
            {madeBindings[i]=MH_CreateHook(installBindings[i],hooks[i],originals[i])==MH_OK;ok=madeBindings[i]&&MH_EnableHook(installBindings[i])==MH_OK;}
        }
        // Never disable somebody else's detour after MH_ERROR_ALREADY_CREATED.
        if(!ok){if(madeBarrier)MH_DisableHook(install);if(madeReset)MH_DisableHook(reset);if(madeClose)MH_DisableHook(close);
            for(size_t i=0;i<BindingHooks;++i)if(madeBindings[i])MH_DisableHook(installBindings[i]);}
        AcquireSRWLockExclusive(&lock);
        if(ok){barrierTarget=install;resetTarget=reset;closeTarget=close;barrierInstalled=true;
            if(directReadback)bindingTargets=installBindings;
            if(!directReadback&&observing&&traceResource)trace.Begin(reinterpret_cast<uint64_t>(traceResource));}
        else {incomplete=true;Save("barrier-hook-failed");}
        ReleaseSRWLockExclusive(&lock);
    }
}
void Stop()
{
    enabled=false;observing=false;
    // The run is the only producer of this value, so it must not outlive it.
    if(directReadback&&!distanceMode)sky::PublishVisibility(0.0,sky::Visibility::Unavailable,0,0);
    if(directReadback)readback->Cancel("stopped");
    trace.target=0;render::submissionObserver=nullptr;
    if(dispatchTarget&&originalDispatch) MH_DisableHook(dispatchTarget);
    if(barrierTarget&&originalBarrier) MH_DisableHook(barrierTarget);
    if(resetTarget&&originalReset) MH_DisableHook(resetTarget);
    if(closeTarget&&originalClose) MH_DisableHook(closeTarget);
    for(auto target:bindingTargets)if(target)MH_DisableHook(target);
    AcquireSRWLockExclusive(&lock);
    if(phase!=Phase::Idle){incomplete=true;Save("stopped");}
    if(requestEvent){CloseHandle(requestEvent);requestEvent=nullptr;}
    ReleaseSRWLockExclusive(&lock);
    // Pinned module/trampolines retained; never free code beneath an in-flight call.
}
bool OwnsCodeAddress(uint64_t address)
{
    return enabled&&address>=base+DispatchRva&&address<base+DispatchRva+32;
}
uint64_t Dispatch(uint64_t command,uint32_t x,uint32_t y,uint32_t z,uint64_t owner,uint64_t caller)
{
    if(!enabled||!observing||caller!=base+ExposureReturnRva||x!=2||y!=1||z!=1||
        !TryAcquireSRWLockExclusive(&lock)) {return originalDispatch(command,x,y,z);}
    if(phase!=Phase::Ready||GetTickCount64()-lastAttempt<500)
    {ReleaseSRWLockExclusive(&lock);return originalDispatch(command,x,y,z);}
    lastAttempt=GetTickCount64();
    Observation o{};o.tick=lastAttempt;o.owner=owner;o.command=command;
    bool valid=Resolve(o);
    if(!valid)o.error=ERROR_INVALID_DATA;
    if(valid&&traceResource&&o.resource!=reinterpret_cast<uint64_t>(traceResource))
    {valid=false;o.error=ERROR_REVISION_MISMATCH;incomplete=true;trace.target=0;}
    if(valid&&barrierInstalled&&(o.barrierFunction!=barrierTarget||o.resetFunction!=resetTarget||o.closeFunction!=closeTarget))
    {valid=false;o.error=ERROR_INVALID_FUNCTION;incomplete=true;trace.target=0;}
    if(valid&&directReadback&&barrierInstalled&&o.bindingFunctions!=bindingTargets)
    {valid=false;o.error=ERROR_INVALID_FUNCTION;incomplete=true;}
    if(valid&&!traceResource)
    {
        traceResource=reinterpret_cast<ID3D12Resource*>(o.resource);traceResource->AddRef();
        if(!directReadback&&barrierInstalled)trace.Begin(o.resource);
    }
    if(directReadback&&!distanceMode)
    {
        if(!valid)readback->Cancel("binding-validation-failed");
        else if(!o.enhanced)readback->Cancel("enhanced-barriers-disabled");
        else
        {
            auto* list=reinterpret_cast<ID3D12GraphicsCommandList7*>(o.nativeList7);
            auto* source=reinterpret_cast<ID3D12Resource*>(o.resource);
            auto* gi=reinterpret_cast<ID3D12Resource*>(o.giGpuResource);
            auto* exposure=reinterpret_cast<ID3D12Resource*>(o.exposureGpuResource);
            readback->Discover(list,source,gi,exposure);
            if(barrierInstalled)o.selectedForReadback=readback->Arm(list,source,o.frame,gi,exposure);
        }
    }
    auto* previous=active;
    if(valid&&barrierInstalled)active=&o;
    if(valid)trace.Lifecycle(o.nativeList7,TraceKind::ExposureBegin);
    const auto result=originalDispatch(command,x,y,z);
    if(valid)trace.Lifecycle(o.nativeList7,TraceKind::ExposureEnd);
    active=previous;
    if(valid){o.giStable=Read(owner+0x20,o.giAfter)&&o.gi==o.giAfter;}
    if(distanceMode&&valid&&o.giStable)distanceLatest=o;
    if(o.selectedForReadback)
    {
        readback->ExposureEnd(o.giStable);copyObservation=o;
        // Same retention as the readback's records, or the two diverge: keeping
        // the FIRST eight here while it keeps the LAST eight silently mispaired
        // every transaction past the eighth.
        copyObservations.push_back(o);
        while(copyObservations.size()>cdt::spatial::SpatialReadback::MaxTransactions)
            copyObservations.erase(copyObservations.begin());
    }
    pending=o;phase=Phase::Pending;
    ReleaseSRWLockExclusive(&lock);
    return result;
}
}
extern "C" uint64_t CdtSpatialDispatch(uint64_t command,uint32_t x,uint32_t y,uint32_t z,uint64_t owner,uint64_t caller)
{
    return cdt::spatial::Dispatch(command,x,y,z,owner,caller);
}
