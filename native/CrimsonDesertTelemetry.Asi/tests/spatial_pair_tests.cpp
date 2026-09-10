// Real WARP shader + native COM detours; synthetic semantics, NOT game evidence.
#include "../src/spatial_probe.cpp"
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <iostream>
namespace cdt::instruments {bool OwnsCodeAddress(uint64_t){return false;}}
namespace cdt::sky {
// Link seam, matching the OwnsCodeAddress one above: these targets compile the
// probe without the bridge. Recording the call lets the fallback rule be asserted.
double publishedValue{}; Visibility publishedState{Visibility::Unavailable};
unsigned publishedCalls{};
void PublishVisibility(double value, Visibility state, uint32_t, uint64_t)
{ publishedValue = value; publishedState = state; ++publishedCalls; }
}
using namespace cdt::spatial;
using Microsoft::WRL::ComPtr;
unsigned checks{};
void Check(bool v,const char* m){++checks;if(!v){std::cerr<<"FAIL "<<checks<<' '<<m<<'\n';ExitProcess(1);}}
void Hr(HRESULT h,const char* m){if(FAILED(h))std::cerr<<std::hex<<h<<std::dec<<' ';Check(SUCCEEDED(h),m);}
void STDMETHODCALLTYPE ForwardBarrier(ID3D12GraphicsCommandList7* l,UINT n,const D3D12_BARRIER_GROUP* g){l->Barrier(n,g);}
int main()
{
    ComPtr<ID3D12Debug> debug;Hr(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),"debug");debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;Hr(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"factory");
    ComPtr<IDXGIAdapter> warp;Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)),"warp");
    ComPtr<ID3D12Device> device;Hr(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");
    ComPtr<ID3D12InfoQueue> info;Hr(device.As(&info),"info");
    D3D12_ROOT_PARAMETER roots[2]{};roots[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;roots[0].Descriptor.ShaderRegister=1;
    roots[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_ROOT_SIGNATURE_DESC rs{};rs.NumParameters=2;rs.pParameters=roots;
    ComPtr<ID3DBlob> blob,error;Hr(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"root serialize");
    ComPtr<ID3D12RootSignature> signature;Hr(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&signature)),"root");
    const char* code="cbuffer GI : register(b1) { uint4 values[48]; }; RWByteAddressBuffer output : register(u0); [numthreads(16,1,1)] void main(uint3 id : SV_DispatchThreadID) { uint i=id.x; if(i<32) output.Store(i*4,values[i/4][i%4]^0xabc00000); }";
    Hr(D3DCompile(code,std::strlen(code),nullptr,nullptr,nullptr,"main","cs_5_1",0,0,&blob,&error),"shader");
    D3D12_COMPUTE_PIPELINE_STATE_DESC ps{};ps.pRootSignature=signature.Get();ps.CS={blob->GetBufferPointer(),blob->GetBufferSize()};
    ComPtr<ID3D12PipelineState> pipeline;Hr(device->CreateComputePipelineState(&ps,IID_PPV_ARGS(&pipeline)),"pso");
    auto buffer=[&](D3D12_HEAP_TYPE heapType,UINT64 size,D3D12_RESOURCE_STATES state,D3D12_RESOURCE_FLAGS flags)
    {
        D3D12_HEAP_PROPERTIES hp{};hp.Type=heapType;hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;d.Height=1;
        d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.Flags=flags;
        ComPtr<ID3D12Resource> r;Hr(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r)),"buffer");return r;
    };
    auto gi=buffer(D3D12_HEAP_TYPE_DEFAULT,65536,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_FLAG_NONE);
    auto output=buffer(D3D12_HEAP_TYPE_DEFAULT,65536,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto upload=buffer(D3D12_HEAP_TYPE_UPLOAD,3*1024*1024,D3D12_RESOURCE_STATE_GENERIC_READ,D3D12_RESOURCE_FLAG_NONE);
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE3D;td.Width=64;td.Height=32;
    td.DepthOrArraySize=264;td.MipLevels=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8_TYPELESS;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> texture;Hr(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&texture)),"texture");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};device->GetCopyableFootprints(&td,0,1,0,&fp,nullptr,nullptr,nullptr);
    void* p{};D3D12_RANGE empty{};Hr(upload->Map(0,&empty,&p),"upload map");std::memset(p,7,3*1024*1024);
    std::array<uint32_t,192> giData{};for(unsigned i=0;i<192;++i)giData[i]=i+17;
    std::memcpy(static_cast<uint8_t*>(p)+2500000,giData.data(),768);upload->Unmap(0,nullptr);
    Hr(MH_Initialize(),"MinHook");
    for(auto type:{D3D12_COMMAND_LIST_TYPE_DIRECT,D3D12_COMMAND_LIST_TYPE_COMPUTE})
    {
        D3D12_COMMAND_QUEUE_DESC qd{};qd.Type=type;ComPtr<ID3D12CommandQueue> queue;
        Hr(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)),"queue");
        ComPtr<ID3D12CommandAllocator> alloc;Hr(device->CreateCommandAllocator(type,IID_PPV_ARGS(&alloc)),"allocator");
        ComPtr<ID3D12GraphicsCommandList7> list;Hr(device->CreateCommandList(0,type,alloc.Get(),nullptr,IID_PPV_ARGS(&list)),"list");
        Hr(list->Close(),"initial close");
        readback=new SpatialReadback;Check(readback->Begin(),"begin");readback->Discover(list.Get(),texture.Get(),gi.Get(),output.Get());readback->Poll();
        Check(readback->Snapshot().phase==CopyPhase::Ready,"prepared pair");
        readback->Reset(list.Get(),false);Hr(list->Reset(alloc.Get(),pipeline.Get()),"reset");readback->Reset(list.Get(),true,S_OK);
        if(type==D3D12_COMMAND_LIST_TYPE_DIRECT)
        {
            D3D12_BUFFER_BARRIER initial[2]{};
            initial[0].pResource=gi.Get();initial[0].SyncAfter=D3D12_BARRIER_SYNC_COPY;initial[0].AccessAfter=D3D12_BARRIER_ACCESS_COPY_DEST;
            initial[1].pResource=output.Get();initial[1].SyncAfter=D3D12_BARRIER_SYNC_COMPUTE_SHADING;initial[1].AccessAfter=D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            for(auto& b:initial){b.AccessBefore=D3D12_BARRIER_ACCESS_NO_ACCESS;b.Size=UINT64_MAX;}
            D3D12_BARRIER_GROUP g{};g.Type=D3D12_BARRIER_TYPE_BUFFER;g.NumBarriers=2;g.pBufferBarriers=initial;list->Barrier(1,&g);
            D3D12_TEXTURE_BARRIER t{};t.pResource=texture.Get();t.SyncAfter=D3D12_BARRIER_SYNC_COPY;
            t.AccessBefore=D3D12_BARRIER_ACCESS_NO_ACCESS;t.AccessAfter=D3D12_BARRIER_ACCESS_COPY_DEST;
            t.LayoutBefore=D3D12_BARRIER_LAYOUT_COMMON;t.LayoutAfter=D3D12_BARRIER_LAYOUT_COPY_DEST;t.Subresources.IndexOrFirstMipLevel=UINT_MAX;
            g.Type=D3D12_BARRIER_TYPE_TEXTURE;g.NumBarriers=1;g.pTextureBarriers=&t;list->Barrier(1,&g);
        }
        // Restore prior iteration's final texture/CB states before re-uploading.
        if(type==D3D12_COMMAND_LIST_TYPE_COMPUTE)
        {
            D3D12_TEXTURE_BARRIER tb{};tb.pResource=texture.Get();tb.SyncAfter=D3D12_BARRIER_SYNC_COPY;
            tb.AccessBefore=D3D12_BARRIER_ACCESS_NO_ACCESS;tb.AccessAfter=D3D12_BARRIER_ACCESS_COPY_DEST;
            tb.LayoutBefore=D3D12_BARRIER_LAYOUT_GENERIC_READ;tb.LayoutAfter=D3D12_BARRIER_LAYOUT_COPY_DEST;tb.Subresources.IndexOrFirstMipLevel=UINT_MAX;
            D3D12_BARRIER_GROUP g{};g.Type=D3D12_BARRIER_TYPE_TEXTURE;g.NumBarriers=1;g.pTextureBarriers=&tb;list->Barrier(1,&g);
            D3D12_BUFFER_BARRIER bb{};bb.pResource=gi.Get();bb.SyncBefore=D3D12_BARRIER_SYNC_COMPUTE_SHADING;bb.SyncAfter=D3D12_BARRIER_SYNC_COPY;
            bb.AccessBefore=D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;bb.AccessAfter=D3D12_BARRIER_ACCESS_COPY_DEST;bb.Size=UINT64_MAX;
            g.Type=D3D12_BARRIER_TYPE_BUFFER;g.pBufferBarriers=&bb;list->Barrier(1,&g);
        }
        list->CopyBufferRegion(gi.Get(),256,upload.Get(),2500000,768);
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=texture.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        D3D12_BUFFER_BARRIER bb{};bb.pResource=gi.Get();bb.SyncBefore=D3D12_BARRIER_SYNC_COPY;bb.SyncAfter=D3D12_BARRIER_SYNC_COMPUTE_SHADING;
        bb.AccessBefore=D3D12_BARRIER_ACCESS_COPY_DEST;bb.AccessAfter=D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;bb.Size=UINT64_MAX;
        D3D12_BARRIER_GROUP bg{};bg.Type=D3D12_BARRIER_TYPE_BUFFER;bg.NumBarriers=1;bg.pBufferBarriers=&bb;list->Barrier(1,&bg);
        D3D12_TEXTURE_BARRIER tb{};tb.pResource=texture.Get();tb.SyncBefore=D3D12_BARRIER_SYNC_COPY;tb.SyncAfter=D3D12_BARRIER_SYNC_COMPUTE_SHADING;
        tb.AccessBefore=D3D12_BARRIER_ACCESS_COPY_DEST;tb.AccessAfter=D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
        tb.LayoutBefore=D3D12_BARRIER_LAYOUT_COPY_DEST;tb.LayoutAfter=D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;tb.Subresources.IndexOrFirstMipLevel=UINT_MAX;
        bg.Type=D3D12_BARRIER_TYPE_TEXTURE;bg.pTextureBarriers=&tb;list->Barrier(1,&bg);
        // Install the same four native methods as production and exercise their ABI.
        auto vt=*reinterpret_cast<void***>(list.Get());
        std::array<void*,BindingHooks> targets{vt[37],vt[41],vt[29],vt[14],vt[39],vt[31],vt[28]};
        std::array<void*,BindingHooks> hooks{reinterpret_cast<void*>(CbvHook),reinterpret_cast<void*>(UavHook),
            reinterpret_cast<void*>(SignatureHook),reinterpret_cast<void*>(NativeDispatchHook),
            reinterpret_cast<void*>(SrvHook),reinterpret_cast<void*>(TableHook),reinterpret_cast<void*>(HeapsHook)};
        std::array<void**,BindingHooks> originals{reinterpret_cast<void**>(&originalCbv),reinterpret_cast<void**>(&originalUav),
            reinterpret_cast<void**>(&originalSignature),reinterpret_cast<void**>(&originalNativeDispatch),
            reinterpret_cast<void**>(&originalSrv),reinterpret_cast<void**>(&originalTable),reinterpret_cast<void**>(&originalHeaps)};
        for(size_t i=0;i<BindingHooks;++i){Check(MH_CreateHook(targets[i],hooks[i],originals[i])==MH_OK,"create hook");Check(MH_EnableHook(targets[i])==MH_OK,"enable hook");}
        originalBarrier=ForwardBarrier;directReadback=true;
        // The game binds its roots BEFORE the exposure wrapper runs. readback.2
        // cleared them at Arm and rejected live; these sets must survive it.
        list->SetComputeRootSignature(signature.Get());
        list->SetComputeRootConstantBufferView(0,gi->GetGPUVirtualAddress()+256);
        list->SetComputeRootUnorderedAccessView(1,output->GetGPUVirtualAddress()+1024);
        Observation o{};o.selectedForReadback=true;o.nativeList7=reinterpret_cast<uint64_t>(list.Get());active=&o;
        Check(readback->Arm(list.Get(),texture.Get(),123,gi.Get(),output.Get()),"arm pair");
        list->Dispatch(2,1,1);readback->ExposureEnd(true);active=nullptr;
        Check(readback->Snapshot().buffersCopied,"immediate CB/output copies");
        tb.SyncBefore=D3D12_BARRIER_SYNC_COMPUTE_SHADING;tb.SyncAfter=D3D12_BARRIER_SYNC_NONE;
        tb.AccessBefore=D3D12_BARRIER_ACCESS_SHADER_RESOURCE;tb.AccessAfter=D3D12_BARRIER_ACCESS_NO_ACCESS;
        tb.LayoutBefore=D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;tb.LayoutAfter=D3D12_BARRIER_LAYOUT_GENERIC_READ;
        readback->Barrier(list.Get(),1,&bg,ForwardBarrier);list->Barrier(1,&bg);
        readback->Close(list.Get(),false);Hr(list->Close(),"close");readback->Close(list.Get(),true,S_OK);
        ComPtr<ID3D12Fence> gate;Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)),"gate");Hr(queue->Wait(gate.Get(),1),"queue gate");
        ID3D12CommandList* submitted[]{list.Get()};readback->Submit(queue.Get(),1,submitted,false);
        queue->ExecuteCommandLists(1,submitted);readback->Submit(queue.Get(),1,submitted,true);
        readback->Poll();Check(!readback->Snapshot().buffersGpuPaired&&readback->Snapshot().mapCalls==0,"no pair before fence");
        Hr(gate->Signal(1),"open gate");const auto deadline=GetTickCount64()+5000;
        while(readback->Snapshot().phase==CopyPhase::WaitingGpu&&GetTickCount64()<deadline){readback->Poll();Sleep(1);}
        const auto r=readback->Snapshot();Check(r.phase==CopyPhase::Complete&&r.buffersGpuPaired,"all copies completed");
        Check(r.giOffset==256&&r.exposureOffset==1024&&r.giRootIndex==0&&r.exposureRootIndex==1,"root corroboration recorded");
        Check(r.giBindingHits==1&&r.exposureBindingHits==1&&!r.giFromSrv&&!r.rootThreadConflict,"root hits counted, not required");
        // Counters are snapshotted with the arrays; a later Reset must not rewrite them.
        Check(r.rootSetsBeforeExposure==2&&r.rootSetsInsideExposure==0,"roots counted at the dispatch, before the exposure");
        // Whole buffers are copied; the window is found offline, never assumed here.
        Check(r.gpuGi.size()==65536&&r.giBytes==65536&&r.gpuExposure.size()==65536&&r.exposureBytes==65536,"whole pinned buffers copied");
        Check(std::memcmp(r.gpuGi.data()+256,giData.data(),768)==0,"all 768 actual GPU GI bytes at their real place");
        for(unsigned i=0;i<32;++i){uint32_t value{};std::memcpy(&value,r.gpuExposure.data()+1024+4*i,4);Check(value==(giData[i]^0xabc00000),"GPU output from SAME constants");}
        Check(r.packed.size()==540672&&std::all_of(r.packed.begin(),r.packed.end(),[](uint8_t b){return b==7;}),"texture in same transaction");
        for(auto t:targets){Check(MH_DisableHook(t)==MH_OK,"disable");Check(MH_RemoveHook(t)==MH_OK,"remove");}
        // The dispatch context itself must still fail closed. Absent root bindings
        // must NOT: the live shader binds through descriptor tables, and identity
        // comes from the validated native path rather than from a root argument.
        for(unsigned negative=0;negative<4;++negative)
        {
            SpatialReadback reject;Check(reject.Begin(),"negative begin");reject.Discover(list.Get(),texture.Get(),gi.Get(),output.Get());reject.Poll();
            reject.Reset(list.Get(),false);reject.Reset(list.Get(),true,S_OK);
            Check(reject.Arm(list.Get(),texture.Get(),124,gi.Get(),output.Get()),"negative arm");
            if(negative==0)reject.NativeDispatchEnd(list.Get(),1,1,1,ForwardBarrier);        // wrong thread group
            if(negative==1)reject.NativeDispatchEnd(list.Get(),2,2,1,ForwardBarrier);        // wrong dimensions
            if(negative==2)reject.NativeDispatchEnd(nullptr,2,1,1,ForwardBarrier);           // another list
            if(negative==3){reject.NativeDispatchEnd(list.Get(),2,1,1,nullptr);}             // no forwardable barrier
            Check(reject.Snapshot().phase==CopyPhase::Failed&&!reject.Snapshot().issued,"wrong dispatch context adds no copy");
        }
        // Two dispatches inside one exposure are ambiguous and must fail closed.
        {
            ComPtr<ID3D12CommandAllocator> twiceAlloc;Hr(device->CreateCommandAllocator(type,IID_PPV_ARGS(&twiceAlloc)),"twice allocator");
            ComPtr<ID3D12GraphicsCommandList7> twiceList;Hr(device->CreateCommandList(0,type,twiceAlloc.Get(),nullptr,IID_PPV_ARGS(&twiceList)),"twice list");
            SpatialReadback twice;Check(twice.Begin(),"twice begin");twice.Discover(twiceList.Get(),texture.Get(),gi.Get(),output.Get());twice.Poll();
            twice.Reset(twiceList.Get(),false);twice.Reset(twiceList.Get(),true,S_OK);
            Check(twice.Arm(twiceList.Get(),texture.Get(),127,gi.Get(),output.Get()),"twice arm");
            twice.NativeDispatchEnd(twiceList.Get(),2,1,1,ForwardBarrier);
            Check(twice.Snapshot().buffersCopied,"first dispatch copies");
            twice.NativeDispatchEnd(twiceList.Get(),2,1,1,ForwardBarrier);
            Check(twice.Snapshot().phase==CopyPhase::Failed,"a second dispatch invalidates the transaction");
            twice.Cancel("test-only");Hr(twiceList->Close(),"twice close");
        }
        // The live case: descriptor tables only, no usable root argument at all.
        // This MUST still copy, with the binding fields reporting zero hits.
        {
            ComPtr<ID3D12CommandAllocator> tableAlloc;Hr(device->CreateCommandAllocator(type,IID_PPV_ARGS(&tableAlloc)),"table allocator");
            ComPtr<ID3D12GraphicsCommandList7> tableList;Hr(device->CreateCommandList(0,type,tableAlloc.Get(),nullptr,IID_PPV_ARGS(&tableList)),"table list");
            SpatialReadback tables;Check(tables.Begin(),"table begin");tables.Discover(tableList.Get(),texture.Get(),gi.Get(),output.Get());tables.Poll();
            tables.Reset(tableList.Get(),false);tables.Reset(tableList.Get(),true,S_OK);
            for(UINT i=5;i<10;++i)tables.RootTable(i,0xb5678a00e55ac0ull+i*0x600);
            tables.RootBuffer(RootKind::Cbv,1,0x1036631400ull);   // unrelated upload-ring constant
            Check(tables.Arm(tableList.Get(),texture.Get(),128,gi.Get(),output.Get()),"table arm");
            tables.NativeDispatchEnd(tableList.Get(),2,1,1,ForwardBarrier);
            const auto tr=tables.Snapshot();
            Check(tr.buffersCopied&&tr.issued,"descriptor-table dispatch still pairs by submission");
            Check(tr.giBindingHits==0&&tr.exposureBindingHits==0,"no root corroboration is reported honestly");
            Check(tr.tableSets==5&&tr.nativeTable[5]!=0&&tr.nativeCbv[1]==0x1036631400ull,"table and unrelated CBV recorded");
            tables.Cancel("test-only");Hr(tableList->Close(),"table close");
        }
        // A list reset clears the recorded root evidence, and a reset AFTER the
        // dispatch must never rewrite what the dispatch already snapshotted.
        {
            ComPtr<ID3D12CommandAllocator> staleAlloc;Hr(device->CreateCommandAllocator(type,IID_PPV_ARGS(&staleAlloc)),"stale allocator");
            ComPtr<ID3D12GraphicsCommandList7> staleList;Hr(device->CreateCommandList(0,type,staleAlloc.Get(),nullptr,IID_PPV_ARGS(&staleList)),"stale list");
            SpatialReadback stale;Check(stale.Begin(),"stale begin");stale.Discover(staleList.Get(),texture.Get(),gi.Get(),output.Get());stale.Poll();
            stale.Reset(staleList.Get(),false);stale.Reset(staleList.Get(),true,S_OK);
            stale.RootBuffer(RootKind::Cbv,0,gi->GetGPUVirtualAddress()+256);
            stale.RootBuffer(RootKind::Uav,1,output->GetGPUVirtualAddress()+1024);
            stale.RootTable(7,0x95678a00e6d420ull);
            stale.Reset(staleList.Get(),false);stale.Reset(staleList.Get(),true,S_OK);
            stale.RootBuffer(RootKind::Cbv,3,gi->GetGPUVirtualAddress()+512);
            Check(stale.Arm(staleList.Get(),texture.Get(),129,gi.Get(),output.Get()),"stale arm");
            stale.NativeDispatchEnd(staleList.Get(),2,1,1,ForwardBarrier);
            auto sn=stale.Snapshot();
            Check(sn.rootSetsBeforeExposure==1&&sn.tableSets==0&&sn.nativeTable[7]==0,"reset cleared the previous recording");
            Check(sn.giRootIndex==3&&sn.giOffset==512,"only the surviving root is reported");
            // The next frame's reset must leave the snapshot untouched.
            stale.Reset(staleList.Get(),false);
            sn=stale.Snapshot();
            Check(sn.rootSetsBeforeExposure==1&&sn.giOffset==512,"a later reset does not rewrite the snapshot");
            stale.Cancel("test-only");Hr(staleList->Close(),"stale close");
        }
        delete readback;readback=nullptr;
    }
    // A repeated series must refresh the reused destination every time and must
    // never re-arm before the previous map finished. Own resources, own queue.
    {
        constexpr unsigned kRuns=3;
        D3D12_COMMAND_QUEUE_DESC qd{};qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        ComPtr<ID3D12CommandQueue> queue;Hr(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)),"series queue");
        ComPtr<ID3D12CommandAllocator> alloc;Hr(device->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&alloc)),"series allocator");
        ComPtr<ID3D12GraphicsCommandList7> list;Hr(device->CreateCommandList(0,qd.Type,alloc.Get(),nullptr,IID_PPV_ARGS(&list)),"series list");
        Hr(list->Close(),"series initial close");
        auto seriesGi=buffer(D3D12_HEAP_TYPE_DEFAULT,65536,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_FLAG_NONE);
        auto seriesOut=buffer(D3D12_HEAP_TYPE_DEFAULT,65536,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        ComPtr<ID3D12Resource> seriesTexture;
        Hr(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&seriesTexture)),"series texture");
        SpatialReadback series;
        Check(!series.Begin(0),"zero transactions refused");
        Check(!series.Begin(SpatialReadback::MaxTransactions+1),"too many transactions refused");
        Check(series.Begin(kRuns,0),"series begin");
        series.Discover(list.Get(),seriesTexture.Get(),seriesGi.Get(),seriesOut.Get());series.Poll();
        Check(series.Snapshot().phase==CopyPhase::Ready,"series prepared once");
        for(unsigned pass_=0;pass_<kRuns;++pass_)
        {
            const uint8_t fill=static_cast<uint8_t>(11+pass_*29);
            std::array<uint32_t,192> data{};for(unsigned i=0;i<192;++i)data[i]=i+1000u*pass_;
            void* mapped{};D3D12_RANGE none{};Hr(upload->Map(0,&none,&mapped),"series upload map");
            std::memset(mapped,fill,3*1024*1024);
            std::memcpy(static_cast<uint8_t*>(mapped)+2500000,data.data(),768);
            upload->Unmap(0,nullptr);
            series.Reset(list.Get(),false);Hr(list->Reset(alloc.Get(),pipeline.Get()),"series reset");series.Reset(list.Get(),true,S_OK);
            const bool first=pass_==0;
            // Only the GI buffer is re-uploaded. The output is brought into UAV
            // access once and then left there; NativeDispatchEnd restores it.
            D3D12_BUFFER_BARRIER pre{};
            pre.pResource=seriesGi.Get();
            pre.AccessBefore=first?D3D12_BARRIER_ACCESS_NO_ACCESS:D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
            pre.SyncBefore=first?D3D12_BARRIER_SYNC_NONE:D3D12_BARRIER_SYNC_COMPUTE_SHADING;
            pre.SyncAfter=D3D12_BARRIER_SYNC_COPY;pre.AccessAfter=D3D12_BARRIER_ACCESS_COPY_DEST;pre.Size=UINT64_MAX;
            D3D12_BARRIER_GROUP g{};g.Type=D3D12_BARRIER_TYPE_BUFFER;g.NumBarriers=1;g.pBufferBarriers=&pre;list->Barrier(1,&g);
            if(first)
            {
                D3D12_BUFFER_BARRIER out{};out.pResource=seriesOut.Get();
                out.SyncBefore=D3D12_BARRIER_SYNC_NONE;out.AccessBefore=D3D12_BARRIER_ACCESS_NO_ACCESS;
                out.SyncAfter=D3D12_BARRIER_SYNC_COMPUTE_SHADING;out.AccessAfter=D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
                out.Size=UINT64_MAX;g.pBufferBarriers=&out;list->Barrier(1,&g);
            }
            D3D12_TEXTURE_BARRIER t{};t.pResource=seriesTexture.Get();t.SyncBefore=first?D3D12_BARRIER_SYNC_NONE:D3D12_BARRIER_SYNC_COMPUTE_SHADING;
            t.SyncAfter=D3D12_BARRIER_SYNC_COPY;t.AccessBefore=first?D3D12_BARRIER_ACCESS_NO_ACCESS:D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
            t.AccessAfter=D3D12_BARRIER_ACCESS_COPY_DEST;t.LayoutBefore=first?D3D12_BARRIER_LAYOUT_COMMON:D3D12_BARRIER_LAYOUT_GENERIC_READ;
            t.LayoutAfter=D3D12_BARRIER_LAYOUT_COPY_DEST;t.Subresources.IndexOrFirstMipLevel=UINT_MAX;
            g.Type=D3D12_BARRIER_TYPE_TEXTURE;g.NumBarriers=1;g.pTextureBarriers=&t;list->Barrier(1,&g);
            list->CopyBufferRegion(seriesGi.Get(),256,upload.Get(),2500000,768);
            D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=seriesTexture.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;
            list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
            D3D12_BUFFER_BARRIER post{};post.pResource=seriesGi.Get();
            post.SyncBefore=D3D12_BARRIER_SYNC_COPY;post.SyncAfter=D3D12_BARRIER_SYNC_COMPUTE_SHADING;
            post.AccessBefore=D3D12_BARRIER_ACCESS_COPY_DEST;post.AccessAfter=D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
            post.Size=UINT64_MAX;
            g.Type=D3D12_BARRIER_TYPE_BUFFER;g.NumBarriers=1;g.pBufferBarriers=&post;list->Barrier(1,&g);
            t.SyncBefore=D3D12_BARRIER_SYNC_COPY;t.SyncAfter=D3D12_BARRIER_SYNC_COMPUTE_SHADING;
            t.AccessBefore=D3D12_BARRIER_ACCESS_COPY_DEST;t.AccessAfter=D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
            t.LayoutBefore=D3D12_BARRIER_LAYOUT_COPY_DEST;t.LayoutAfter=D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
            g.Type=D3D12_BARRIER_TYPE_TEXTURE;g.NumBarriers=1;g.pTextureBarriers=&t;list->Barrier(1,&g);
            list->SetComputeRootSignature(signature.Get());
            list->SetComputeRootConstantBufferView(0,seriesGi->GetGPUVirtualAddress()+256);
            list->SetComputeRootUnorderedAccessView(1,seriesOut->GetGPUVirtualAddress()+1024);
            series.RootBuffer(RootKind::Cbv,0,seriesGi->GetGPUVirtualAddress()+256);
            series.RootBuffer(RootKind::Uav,1,seriesOut->GetGPUVirtualAddress()+1024);
            Check(series.Arm(list.Get(),seriesTexture.Get(),200+pass_,seriesGi.Get(),seriesOut.Get()),"series arm");
            list->Dispatch(2,1,1);
            series.NativeDispatchEnd(list.Get(),2,1,1,ForwardBarrier);
            series.ExposureEnd(true);
            t.SyncBefore=D3D12_BARRIER_SYNC_COMPUTE_SHADING;t.SyncAfter=D3D12_BARRIER_SYNC_NONE;
            t.AccessBefore=D3D12_BARRIER_ACCESS_SHADER_RESOURCE;t.AccessAfter=D3D12_BARRIER_ACCESS_NO_ACCESS;
            t.LayoutBefore=D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;t.LayoutAfter=D3D12_BARRIER_LAYOUT_GENERIC_READ;
            g.Type=D3D12_BARRIER_TYPE_TEXTURE;g.NumBarriers=1;g.pTextureBarriers=&t;
            series.Barrier(list.Get(),1,&g,ForwardBarrier);list->Barrier(1,&g);
            series.Close(list.Get(),false);Hr(list->Close(),"series close");series.Close(list.Get(),true,S_OK);
            ID3D12CommandList* submitted[]{list.Get()};
            series.Submit(queue.Get(),1,submitted,false);
            queue->ExecuteCommandLists(1,submitted);
            series.Submit(queue.Get(),1,submitted,true);
            Check(series.Snapshot().fenceValue==pass_+1,"fence value advances per transaction");
            const auto deadline=GetTickCount64()+5000;
            while(series.Snapshot().phase==CopyPhase::WaitingGpu&&GetTickCount64()<deadline){series.Poll();Sleep(1);}
            Check(series.Completed()==pass_+1,"transaction recorded");
            Check(series.SeriesFinished()==(pass_+1==kRuns),"series ends exactly at the budget");
        }
        const auto records=series.Records();
        Check(records.size()==kRuns,"one record per transaction");
        for(unsigned pass_=0;pass_<kRuns;++pass_)
        {
            const auto& r=records[pass_];
            const uint8_t fill=static_cast<uint8_t>(11+pass_*29);
            Check(r.phase==CopyPhase::Complete&&r.buffersGpuPaired&&r.fenceValue==pass_+1,"each transaction completed under its own fence");
            Check(r.packed.size()==540672&&std::all_of(r.packed.begin(),r.packed.end(),[fill](uint8_t b){return b==fill;}),
                  "reused destination holds THIS transaction's texture, not a stale copy");
            uint32_t first{};std::memcpy(&first,r.gpuGi.data()+256,4);
            Check(first==1000u*pass_,"reused destination holds THIS transaction's GI bytes");
        }
        Check(!series.Begin(2),"a consumed series cannot restart in the same process");
    }
    for(UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i)
    {
        SIZE_T n{};info->GetMessage(i,nullptr,&n);std::vector<uint8_t> bytes(n);auto* m=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());Hr(info->GetMessage(i,m,&n),"message");
        if(m->Severity<=D3D12_MESSAGE_SEVERITY_WARNING)std::cerr<<m->pDescription<<'\n';
        Check(m->Severity>D3D12_MESSAGE_SEVERITY_WARNING,"zero debug warnings/errors");
    }
    std::cout<<"PASS "<<checks<<" actual WARP shader/native hook/GPU CB+output+texture pairing and repeated-series controls. Synthetic, no game proof.\n";
}
