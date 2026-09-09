// Real WARP shader + native COM detours; synthetic semantics, NOT game evidence.
#include "../src/spatial_probe.cpp"
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <iostream>
namespace cdt::instruments {bool OwnsCodeAddress(uint64_t){return false;}}
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
        std::array<void*,4> targets{vt[37],vt[41],vt[29],vt[14]};
        std::array<void*,4> hooks{reinterpret_cast<void*>(CbvHook),reinterpret_cast<void*>(UavHook),reinterpret_cast<void*>(SignatureHook),reinterpret_cast<void*>(NativeDispatchHook)};
        std::array<void**,4> originals{reinterpret_cast<void**>(&originalCbv),reinterpret_cast<void**>(&originalUav),reinterpret_cast<void**>(&originalSignature),reinterpret_cast<void**>(&originalNativeDispatch)};
        for(size_t i=0;i<4;++i){Check(MH_CreateHook(targets[i],hooks[i],originals[i])==MH_OK,"create hook");Check(MH_EnableHook(targets[i])==MH_OK,"enable hook");}
        originalBarrier=ForwardBarrier;directReadback=true;
        Observation o{};o.selectedForReadback=true;o.nativeList7=reinterpret_cast<uint64_t>(list.Get());active=&o;
        Check(readback->Arm(list.Get(),texture.Get(),123,gi.Get(),output.Get()),"arm pair");
        list->SetComputeRootSignature(signature.Get());
        list->SetComputeRootConstantBufferView(0,gi->GetGPUVirtualAddress()+256);
        list->SetComputeRootUnorderedAccessView(1,output->GetGPUVirtualAddress()+1024);
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
        Check(r.giOffset==256&&r.exposureOffset==1024&&r.giRootIndex==0&&r.exposureRootIndex==1,"actual nonzero offsets");
        Check(std::memcmp(r.gpuGi.data(),giData.data(),768)==0,"all 768 actual GPU GI bytes");
        for(unsigned i=0;i<32;++i){uint32_t value{};std::memcpy(&value,r.gpuExposure.data()+4*i,4);Check(value==(giData[i]^0xabc00000),"GPU output from SAME constants");}
        Check(r.packed.size()==540672&&std::all_of(r.packed.begin(),r.packed.end(),[](uint8_t b){return b==7;}),"texture in same transaction");
        for(auto t:targets){Check(MH_DisableHook(t)==MH_OK,"disable");Check(MH_RemoveHook(t)==MH_OK,"remove");}
        // Missing bindings and root-signature invalidation must not issue copies.
        for(unsigned negative=0;negative<3;++negative)
        {
            SpatialReadback reject;Check(reject.Begin(),"negative begin");reject.Discover(list.Get(),texture.Get(),gi.Get(),output.Get());reject.Poll();
            reject.Reset(list.Get(),false);reject.Reset(list.Get(),true,S_OK);
            Check(reject.Arm(list.Get(),texture.Get(),124,gi.Get(),output.Get()),"negative arm");
            reject.RootBuffer(true,0,gi->GetGPUVirtualAddress()+256);
            if(negative!=0)reject.RootBuffer(false,1,output->GetGPUVirtualAddress()+1024);
            if(negative==1)reject.RootSignature();
            if(negative==2)reject.RootBuffer(true,2,gi->GetGPUVirtualAddress()+512);
            reject.NativeDispatchEnd(list.Get(),2,1,1,ForwardBarrier);
            Check(reject.Snapshot().phase==CopyPhase::Failed&&!reject.Snapshot().issued,"unseen/reset/ambiguous root bindings add no copy");
        }
        delete readback;readback=nullptr;
    }
    for(UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i)
    {
        SIZE_T n{};info->GetMessage(i,nullptr,&n);std::vector<uint8_t> bytes(n);auto* m=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());Hr(info->GetMessage(i,m,&n),"message");
        if(m->Severity<=D3D12_MESSAGE_SEVERITY_WARNING)std::cerr<<m->pDescription<<'\n';
        Check(m->Severity>D3D12_MESSAGE_SEVERITY_WARNING,"zero debug warnings/errors");
    }
    std::cout<<"PASS "<<checks<<" actual WARP shader/native hook/GPU CB+output+texture pairing controls. Synthetic, no game proof.\n";
}
