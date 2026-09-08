// White-box controls for the PRIVATE observer. Synthetic wrappers and WARP
// resource descriptors, NOT evidence about an actual game frame or GPU copy.
#include "../src/spatial_probe.cpp"
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <iostream>
namespace cdt::instruments { bool OwnsCodeAddress(uint64_t){return false;} }
using namespace cdt::spatial;
using Microsoft::WRL::ComPtr;
unsigned checks{},dispatchCalls{},barrierCalls{};
void Check(bool value){++checks;if(!value){std::cerr<<"failed check "<<checks<<'\n';ExitProcess(1);}}
void Hr(HRESULT hr){Check(SUCCEEDED(hr));}
template<size_t N,class T> void Put(std::array<uint8_t,N>& a,size_t p,T v){memcpy(a.data()+p,&v,sizeof(v));}
uint64_t StubDispatch(uint64_t,uint32_t,uint32_t,uint32_t){++dispatchCalls;return 0xABCDEF;}
void STDMETHODCALLTYPE StubBarrier(ID3D12GraphicsCommandList7*,UINT,const D3D12_BARRIER_GROUP*){++barrierCalls;}
int main()
{
    ComPtr<IDXGIFactory4> factory;Hr(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> warp;Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    ComPtr<ID3D12Device> device;Hr(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandAllocator> allocator;Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    desc.Width=64;desc.Height=32;desc.DepthOrArraySize=264;desc.MipLevels=1;desc.Format=DXGI_FORMAT_R8_TYPELESS;
    desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> resource;Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
        D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&resource)));
    std::array<uint8_t,0x800> owner{},renderer{},outer{},storage{},sceneOwner{};
    std::array<uint8_t,0x1000> engineDevice{},command{};
    std::array<uint8_t,2816> scene{};std::array<uint8_t,16> holder{};
    std::array<D3D12_STATIC_SAMPLER_DESC,13> samplers{};
    samplers[12].ShaderRegister=12;samplers[12].RegisterSpace=4;
    Put(owner,0,reinterpret_cast<uint64_t>(engineDevice.data()));
    Put(owner,8,reinterpret_cast<uint64_t>(sceneOwner.data()));
    Put(owner,0x10,reinterpret_cast<uint64_t>(renderer.data()));
    Put(renderer,0x660,reinterpret_cast<uint64_t>(owner.data()));
    Put(owner,0x4B8,reinterpret_cast<uint64_t>(outer.data()));
    Put(outer,0x30,reinterpret_cast<uint64_t>(storage.data()));
    Put(storage,0x10,reinterpret_cast<uint64_t>(outer.data()));
    Put(storage,0x100,reinterpret_cast<uint64_t>(resource.Get()));
    Put(storage,0xD0,uint32_t{64});Put(storage,0xD4,uint32_t{32});Put(storage,0xD8,uint32_t{264});
    Put(command,0x800,reinterpret_cast<uint64_t>(holder.data()));Put(holder,8,reinterpret_cast<uint64_t>(list.Get()));
    Put(engineDevice,0x970,reinterpret_cast<uint64_t>(samplers.data()));Put(engineDevice,0x978,uint32_t{13});
    Put(sceneOwner,0x428,reinterpret_cast<uint64_t>(scene.data()));Put(scene,0x20,uint32_t{42});
    std::array<uint8_t,0x180> cbWrapper{},cbOuter{},cbStorage{};
    Put(owner,0x560,reinterpret_cast<uint64_t>(cbWrapper.data()));
    Put(cbWrapper,0x18,reinterpret_cast<uint64_t>(cbOuter.data()));
    Put(cbOuter,0x30,reinterpret_cast<uint64_t>(cbStorage.data()));
    Put(cbStorage,0x168,reinterpret_cast<uint64_t>(resource.Get()));
    Observation o{};o.owner=reinterpret_cast<uint64_t>(owner.data());o.command=reinterpret_cast<uint64_t>(command.data());
    Check(Resolve(o));Check(o.frame==42&&o.resource==reinterpret_cast<uint64_t>(resource.Get()));
    Check(o.desc.Format==DXGI_FORMAT_R8_TYPELESS&&o.barrierFunction!=nullptr);
    Put(renderer,0x660,uint64_t{0});Check(!Resolve(o));Put(renderer,0x660,o.owner);
    Put(storage,0xD8,uint32_t{263});Check(!Resolve(o));Put(storage,0xD8,uint32_t{264});
    samplers[12].RegisterSpace=3;Check(!Resolve(o));samplers[12].RegisterSpace=4;
    Check(Resolve(o));
    originalDispatch=StubDispatch;originalBarrier=StubBarrier;enabled=true;observing=true;base=0x140000000;
    phase=Phase::Ready;
    Dispatch(o.command,2,1,1,o.owner,0);Check(dispatchCalls==1&&phase==Phase::Ready);
    Dispatch(o.command,1,1,1,o.owner,base+ExposureReturnRva);Check(dispatchCalls==2&&phase==Phase::Ready);
    Check(Dispatch(o.command,2,1,1,o.owner,base+ExposureReturnRva)==0xABCDEF);
    Check(dispatchCalls==3&&phase==Phase::Pending&&pending.giStable&&pending.frame==42);
    observing=true;active=&o;o.barrierCount=0;
    D3D12_TEXTURE_BARRIER barrier{};barrier.pResource=resource.Get();
    barrier.LayoutBefore=D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;barrier.LayoutAfter=D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
    D3D12_BARRIER_GROUP group{};group.Type=D3D12_BARRIER_TYPE_TEXTURE;group.NumBarriers=1;group.pTextureBarriers=&barrier;
    BarrierHook(nullptr,1,&group);Check(barrierCalls==1&&o.barrierCount==0);
    auto* list7=reinterpret_cast<ID3D12GraphicsCommandList7*>(o.nativeList7);
    BarrierHook(list7,1,&group);Check(barrierCalls==2&&o.barrierCount==1);
    Check(o.barriers[0].LayoutAfter==D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
    barrier.pResource=nullptr;BarrierHook(list7,1,&group);Check(o.barrierCount==1);
    barrier.pResource=resource.Get();for(unsigned i=0;i<10;++i)BarrierHook(list7,1,&group);
    Check(o.barrierCount==8&&o.barrierOverflow);
    group.NumBarriers=4097;BarrierHook(list7,1,&group);Check(o.barrierOverflow);
    active=nullptr;const auto previous=o.barrierCount;BarrierHook(list7,1,&group);Check(o.barrierCount==previous);
    auto result=Json(o);Check(result["gpuCopyIssued"]==false&&result["gpuFramePaired"]==false);
    // Real MinHook + real WARP list7::Barrier, not only the synthetic callback.
    Check(MH_Initialize()==MH_OK);
    Check(MH_CreateHook(o.barrierFunction,BarrierHook,reinterpret_cast<void**>(&originalBarrier))==MH_OK);
    Check(MH_EnableHook(o.barrierFunction)==MH_OK);
    o.barrierCount=0;o.barrierOverflow=false;active=&o;
    barrier.SyncBefore=D3D12_BARRIER_SYNC_NONE;barrier.SyncAfter=D3D12_BARRIER_SYNC_COMPUTE_SHADING;
    barrier.AccessBefore=D3D12_BARRIER_ACCESS_NO_ACCESS;barrier.AccessAfter=D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
    barrier.LayoutBefore=D3D12_BARRIER_LAYOUT_COMMON;barrier.LayoutAfter=D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
    barrier.Subresources={0,1,0,1,0,1};group.NumBarriers=1;
    list7->Barrier(1,&group);active=nullptr;
    Check(o.barrierCount==1&&o.barriers[0].LayoutAfter==D3D12_BARRIER_LAYOUT_SHADER_RESOURCE);
    Check(MH_DisableHook(o.barrierFunction)==MH_OK);
    Check(MH_RemoveHook(o.barrierFunction)==MH_OK);
    enabled=false;observing=false;
    Hr(list->Close());
    std::cout<<"PASS "<<checks<<" passive spatial observer controls; no GPU copies or game evidence.\n";
}
