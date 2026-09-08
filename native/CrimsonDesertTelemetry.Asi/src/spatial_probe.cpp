#include "spatial_probe.h"
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
DispatchFn originalDispatch{};
BarrierFn originalBarrier{};
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
nlohmann::json samples;
struct Observation
{
    uint64_t tick{}, owner{}, command{}, resource{}, nativeList{}, nativeList7{}, giGpuResource{};
    uint32_t frame{}, bank{}, error{}, barrierCount{};
    bool giStable{}, barrierOverflow{}, enhanced{};
    D3D12_RESOURCE_DESC desc{};
    D3D12_STATIC_SAMPLER_DESC sampler{};
    std::array<uint8_t,768> gi{}, giAfter{};
    std::array<uint8_t,2816> scene{};
    std::array<uint8_t,64> exposure{};
    std::array<uint8_t,80> viewEntry{};
    std::array<uint8_t,64> viewDescriptor{};
    std::array<D3D12_TEXTURE_BARRIER,BarrierLimit> barriers{};
    void* barrierFunction{};
};
Observation pending;
thread_local Observation* active{};
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
    uint64_t cbWrapper{},cbOuter{},cbStorage{},viewTable{},viewObject{};
    uint32_t viewCount{};
    if(!Read(o.owner+(bank?0x568:0x560),cbWrapper)||!Read(cbWrapper+0x18,cbOuter)||
        !Read(cbOuter+0x30,cbStorage)||!Read(cbStorage+0x168,o.giGpuResource)) return false;
    // Raw descriptor provenance only; no guessed SRV format interpretation.
    if(Read(storage+0xE8,viewTable)&&Read(storage+0xF0,viewCount)&&viewCount>0&&viewCount<=16)
    {
        Read(viewTable,o.viewEntry);
        if(Read(viewTable+0x20,viewObject)) Read(viewObject,o.viewDescriptor);
    }
    std::memcpy(&o.frame,o.scene.data()+0x20,4);
    if(Read(renderer+0x690,exposureOwner)) Read(exposureOwner+0xD8,o.exposure);
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
    list7->Release();
    return o.barrierFunction!=nullptr;
}
nlohmann::json Json(const Observation& o)
{
    nlohmann::json barriers=nlohmann::json::array();
    for(unsigned i=0;i<o.barrierCount;++i)
    {
        const auto& b=o.barriers[i];
        barriers.push_back({{"syncBefore",b.SyncBefore},{"syncAfter",b.SyncAfter},
            {"accessBefore",b.AccessBefore},{"accessAfter",b.AccessAfter},
            {"layoutBefore",b.LayoutBefore},{"layoutAfter",b.LayoutAfter},
            {"flags",b.Flags},{"subresources",{{"indexOrFirstMip",b.Subresources.IndexOrFirstMipLevel},
            {"numMips",b.Subresources.NumMipLevels},{"firstArray",b.Subresources.FirstArraySlice},
            {"numArrays",b.Subresources.NumArraySlices},{"firstPlane",b.Subresources.FirstPlane},
            {"numPlanes",b.Subresources.NumPlanes}}}});
    }
    return {{"capturedTick",o.tick},{"frame",o.frame},{"error",o.error},{"owner",o.owner},
        {"command",o.command},{"resource",o.resource},{"nativeList",o.nativeList},{"nativeList7",o.nativeList7},
        {"enhancedBarriers",o.enhanced},{"bankFlag",o.bank},{"giGpuResource",o.giGpuResource},{"giCopiesMatch",o.giStable},
        {"gpuCopyIssued",false},{"gpuFramePaired",false},{"barrierHookInstalled",barrierInstalled},
        {"resourceDescription",{{"dimension",o.desc.Dimension},{"width",o.desc.Width},
            {"height",o.desc.Height},{"depth",o.desc.DepthOrArraySize},{"mips",o.desc.MipLevels},
            {"format",o.desc.Format},{"flags",o.desc.Flags}}},
        {"samplerHex",Hex(reinterpret_cast<const uint8_t*>(&o.sampler),sizeof(o.sampler))},
        {"giBeforeHex",Hex(o.gi.data(),o.gi.size())},{"giAfterHex",Hex(o.giAfter.data(),o.giAfter.size())},
        {"sceneHex",Hex(o.scene.data(),o.scene.size())},{"exposureCacheHex",Hex(o.exposure.data(),o.exposure.size())},
        {"srvViewEntryRawHex",Hex(o.viewEntry.data(),o.viewEntry.size())},
        {"srvViewObjectRawHex",Hex(o.viewDescriptor.data(),o.viewDescriptor.size())},
        {"observedTextureBarriers",barriers},{"barrierTraceOverflow",o.barrierOverflow}};
}
void Save(const char* reason)
{
    const bool progressing=samples.size()>1 && samples.front()["frame"]!=samples.back()["frame"];
    nlohmann::json report={{"format","private-spatial-binding-v1"},{"pid",GetCurrentProcessId()},
        {"executableSha256",Hex(native_contract::ExecutableSha256.data(),native_contract::ExecutableSha256.size())},
        {"reason",reason},{"complete",!incomplete&&count==Limit},{"controlProgressed",progressing},
        {"gpuCopyIssued",false},{"caveat","Passive instrumented run, not untouched baseline. Barrier trace is limited to this Dispatch's CPU invocation; absence is NOT a known resource state. CPU constants/cache are NOT GPU-frame paired."},
        {"samples",samples}};
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

bool Start(uint64_t moduleBase,const wchar_t* directory)
{
    if(enabled) return false;
    std::array<uint8_t,Signature.size()> bytes{};
    if(!Read(moduleBase+DispatchRva,bytes)||bytes!=Signature) return false;
    base=moduleBase;outputDirectory=directory;
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
    ch::Log("Spatial binding probe IDLE: passive GetDesc/CB/sampler/enhanced-barrier observation only; explicit event starts20 samples, no GPU copy.");
    return true;
}
void Poll()
{
    if(!enabled) return;
    AcquireSRWLockExclusive(&lock);
    if(requestEvent&&WaitForSingleObject(requestEvent,0)==WAIT_OBJECT_0&&phase==Phase::Idle)
    {samples=nlohmann::json::array();count=0;incomplete=false;started=GetTickCount64();lastAttempt=0;phase=Phase::Ready;observing=true;}
    void* install{};
    if(phase==Phase::Pending)
    {
        samples.push_back(Json(pending));++count;
        if(!barrierInstalled&&!pending.error) install=pending.barrierFunction;
        phase=Phase::Ready;
        if(count==Limit) Save("sample-limit");
    }
    if(observing&&GetTickCount64()-started>30000){incomplete=true;Save("timeout");}
    ReleaseSRWLockExclusive(&lock);
    // MinHook suspends threads: never hold our capture lock during patching.
    if(install)
    {
        const bool ok=MH_CreateHook(install,BarrierHook,reinterpret_cast<void**>(&originalBarrier))==MH_OK&&MH_EnableHook(install)==MH_OK;
        AcquireSRWLockExclusive(&lock);
        if(ok){barrierTarget=install;barrierInstalled=true;}
        else {incomplete=true;Save("barrier-hook-failed");}
        ReleaseSRWLockExclusive(&lock);
    }
}
void Stop()
{
    enabled=false;observing=false;
    if(dispatchTarget&&originalDispatch) MH_DisableHook(dispatchTarget);
    if(barrierTarget&&originalBarrier) MH_DisableHook(barrierTarget);
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
    const bool valid=Resolve(o);
    if(!valid)o.error=ERROR_INVALID_DATA;
    auto* previous=active;
    if(valid&&barrierInstalled)active=&o;
    const auto result=originalDispatch(command,x,y,z);
    active=previous;
    if(valid){o.giStable=Read(owner+0x20,o.giAfter)&&o.gi==o.giAfter;}
    pending=o;phase=Phase::Pending;
    ReleaseSRWLockExclusive(&lock);
    return result;
}
}
extern "C" uint64_t CdtSpatialDispatch(uint64_t command,uint32_t x,uint32_t y,uint32_t z,uint64_t owner,uint64_t caller)
{
    return cdt::spatial::Dispatch(command,x,y,z,owner,caller);
}
