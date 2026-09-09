#include "spatial_probe.h"
#include "spatial_trace.h"
#include "spatial_readback.h"
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
RootBufferFn originalCbv{},originalUav{};
RootSignatureFn originalSignature{};
NativeDispatchFn originalNativeDispatch{};
std::array<void*,4> bindingTargets{};
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
    std::array<void*,4> bindingFunctions{};
};
Observation pending;
Observation copyObservation;
thread_local Observation* active{};
bool SelectedNative(ID3D12GraphicsCommandList* list)
{
    return directReadback&&active&&active->selectedForReadback&&active->nativeList7==reinterpret_cast<uint64_t>(list);
}
void STDMETHODCALLTYPE CbvHook(ID3D12GraphicsCommandList* list,UINT index,D3D12_GPU_VIRTUAL_ADDRESS address)
{originalCbv(list,index,address);if(SelectedNative(list))readback->RootBuffer(true,index,address);}
void STDMETHODCALLTYPE UavHook(ID3D12GraphicsCommandList* list,UINT index,D3D12_GPU_VIRTUAL_ADDRESS address)
{originalUav(list,index,address);if(SelectedNative(list))readback->RootBuffer(false,index,address);}
void STDMETHODCALLTYPE SignatureHook(ID3D12GraphicsCommandList* list,ID3D12RootSignature* signature)
{originalSignature(list,signature);if(SelectedNative(list))readback->RootSignature();}
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
void STDMETHODCALLTYPE BarrierHook(ID3D12GraphicsCommandList7* list,UINT groups,const D3D12_BARRIER_GROUP* data)
{
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
    if(directReadback&&o.giGpuResource)
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
    if(directReadback)
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
    o.bindingFunctions={vtable[37],vtable[41],vtable[29],vtable[14]};
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
void Save(const char* reason)
{
    if(directReadback)readback->Cancel("capture-window-ended");
    const auto copy=readback->Snapshot();
    const bool progressing=samples.size()>1 && samples.front()["frame"]!=samples.back()["frame"];
    nlohmann::json report={{"format",directReadback?"private-spatial-readback-v2":"private-spatial-binding-v2"},{"pid",GetCurrentProcessId()},
        {"executableSha256",Hex(native_contract::ExecutableSha256.data(),native_contract::ExecutableSha256.size())},
        {"reason",reason},{"complete",!incomplete&&count==Limit&&(!directReadback||copy.phase==CopyPhase::Complete)},
        {"controlProgressed",progressing},{"gpuCopyIssued",directReadback&&copy.issued},
        {"caveat",directReadback?
            "One instrumented transaction. Buffers are paired only if native bindings/Dispatch, copies and same-list fence succeeded. CPU scene/cache remains unpaired; no local brightness or source visibility API.":
            "Passive instrumented run, not untouched baseline. Per-sample barriers cover only Dispatch; intervalTrace covers discovered implementations across the requested interval, with explicit losses/unknown generations. Neither proves GPU completion/current layout. CPU constants/cache are NOT GPU-frame paired."},
        {"samples",samples},{"intervalTrace",directReadback?nlohmann::json(nullptr):TraceJson()}};
    if(directReadback)
    {
        const auto& f=copy.footprint;
        report["textureReadback"]={{"status",copy.reason},{"hresult",static_cast<uint32_t>(copy.error)},
            {"gpuCopyIssued",copy.issued},{"gpuCompleted",copy.gpuCompleted},{"giGpuFramePaired",copy.buffersGpuPaired},
            {"exposureGpuFramePaired",copy.buffersGpuPaired},{"frame",copy.frame},{"resetGeneration",copy.generation},
            {"queue",copy.queue},{"recordingThread",copy.recordingThread},{"submissionThread",copy.submissionThread},
            {"releaseTick",copy.releaseTick},{"submitTick",copy.submitTick},{"completedTick",copy.completedTick},
            {"fenceValue",copy.fenceValue},{"mapCalls",copy.mapCalls},{"releaseBarrier",BarrierJson(copy.release)},
            {"allocationBytes",copy.allocationBytes},{"footprint",{{"offset",f.Offset},{"format",f.Footprint.Format},
                {"width",f.Footprint.Width},{"height",f.Footprint.Height},{"depth",f.Footprint.Depth},{"rowPitch",f.Footprint.RowPitch}}},
            {"packing","uint8 x-fastest, then y, then z; row padding removed; resource R8_TYPELESS"},
            {"packedTextureHex",Hex(copy.packed.data(),copy.packed.size())},{"context",Json(copyObservation)}};
        report["textureReadback"]["bufferPair"]={{"requested",copy.buffersRequested},{"nativeDispatchSeen",copy.nativeDispatchSeen},
            {"nativeDispatches",copy.nativeDispatches},{"copied",copy.buffersCopied},{"giResource",copy.giResource},
            {"exposureResource",copy.exposureResource},{"giBase",copy.giBase},{"exposureBase",copy.exposureBase},
            {"giOffset",copy.giOffset},{"exposureOffset",copy.exposureOffset},{"giRootIndex",copy.giRootIndex},
            {"exposureRootIndex",copy.exposureRootIndex},{"readbackOffset",copy.pairReadbackOffset},
            {"nativeCbv",copy.nativeCbv},{"nativeUav",copy.nativeUav},
            {"giHex",copy.buffersGpuPaired?Hex(copy.gpuGi.data(),copy.gpuGi.size()):""},
            {"exposureHex",copy.buffersGpuPaired?Hex(copy.gpuExposure.data(),copy.gpuExposure.size()):""}};
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

bool Start(uint64_t moduleBase,const wchar_t* directory,bool enableReadback)
{
    if(enabled) return false;
    std::array<uint8_t,Signature.size()> bytes{};
    if(!Read(moduleBase+DispatchRva,bytes)||bytes!=Signature) return false;
    base=moduleBase;outputDirectory=directory;directReadback=enableReadback;
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
    ch::Log(directReadback?
        "Spatial readback v2 IDLE: explicit event starts20 CPU controls plus ONE texture/GI/exposure transaction; actual native root bindings required, no API change.":
        "Spatial binding probe v2 IDLE: passive interval Barrier/Reset/Close/submission trace; explicit event starts20 samples, no GPU copy.");
    return true;
}
void Poll()
{
    if(!enabled) return;
    if(directReadback)readback->Poll();
    AcquireSRWLockExclusive(&lock);
    if(requestEvent&&WaitForSingleObject(requestEvent,0)==WAIT_OBJECT_0&&phase==Phase::Idle)
    {
        if(!directReadback||readback->Begin())
        {samples=nlohmann::json::array();copyObservation={};count=0;incomplete=false;trace.Begin(0);started=GetTickCount64();lastAttempt=0;phase=Phase::Ready;observing=true;}
        else ch::Log("Spatial direct readback already requested in this process; restart required for another run.");
    }
    void* install{};
    void* installReset{};
    void* installClose{};
    std::array<void*,4> installBindings{};
    if(phase==Phase::Pending)
    {
        samples.push_back(Json(pending));++count;
        if(!barrierInstalled&&!pending.error)
        {install=pending.barrierFunction;installReset=pending.resetFunction;installClose=pending.closeFunction;installBindings=pending.bindingFunctions;}
        phase=Phase::Ready;
        if(count==Limit) Save("sample-limit");
    }
    if(observing&&GetTickCount64()-started>30000){incomplete=true;Save("timeout");}
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
        std::array<bool,4> madeBindings{};
        if(ok&&directReadback)
        {
            const std::array<void*,4> hooks={reinterpret_cast<void*>(CbvHook),reinterpret_cast<void*>(UavHook),
                reinterpret_cast<void*>(SignatureHook),reinterpret_cast<void*>(NativeDispatchHook)};
            const std::array<void**,4> originals={reinterpret_cast<void**>(&originalCbv),reinterpret_cast<void**>(&originalUav),
                reinterpret_cast<void**>(&originalSignature),reinterpret_cast<void**>(&originalNativeDispatch)};
            for(size_t i=0;i<4&&ok;++i)
            {madeBindings[i]=MH_CreateHook(installBindings[i],hooks[i],originals[i])==MH_OK;ok=madeBindings[i]&&MH_EnableHook(installBindings[i])==MH_OK;}
        }
        // Never disable somebody else's detour after MH_ERROR_ALREADY_CREATED.
        if(!ok){if(madeBarrier)MH_DisableHook(install);if(madeReset)MH_DisableHook(reset);if(madeClose)MH_DisableHook(close);
            for(size_t i=0;i<4;++i)if(madeBindings[i])MH_DisableHook(installBindings[i]);}
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
    if(directReadback)
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
    if(o.selectedForReadback){readback->ExposureEnd(o.giStable);copyObservation=o;}
    pending=o;phase=Phase::Pending;
    ReleaseSRWLockExclusive(&lock);
    return result;
}
}
extern "C" uint64_t CdtSpatialDispatch(uint64_t command,uint32_t x,uint32_t y,uint32_t z,uint64_t owner,uint64_t caller)
{
    return cdt::spatial::Dispatch(command,x,y,z,owner,caller);
}
