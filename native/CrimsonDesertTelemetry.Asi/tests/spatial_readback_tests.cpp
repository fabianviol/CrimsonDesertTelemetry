#include "spatial_readback.h"
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <iostream>
#include <thread>
#include <cstring>
#include <array>
#include <algorithm>

using namespace cdt::spatial;
using Microsoft::WRL::ComPtr;
unsigned checks{},injectedBarriers{};
void Check(bool value,const char* message)
{++checks;if(!value){std::cerr<<"FAIL "<<checks<<": "<<message<<'\n';ExitProcess(1);}}
void Hr(HRESULT hr,const char* message)
{if(FAILED(hr))std::cerr<<std::hex<<static_cast<unsigned>(hr)<<std::dec<<' ';Check(SUCCEEDED(hr),message);}
void STDMETHODCALLTYPE Forward(ID3D12GraphicsCommandList7* list,UINT count,const D3D12_BARRIER_GROUP* groups)
{++injectedBarriers;list->Barrier(count,groups);}
D3D12_BARRIER_GROUP Group(const D3D12_TEXTURE_BARRIER* b,UINT count=1)
{D3D12_BARRIER_GROUP g{};g.Type=D3D12_BARRIER_TYPE_TEXTURE;g.NumBarriers=count;g.pTextureBarriers=b;return g;}
D3D12_TEXTURE_BARRIER Release(ID3D12Resource* source)
{
    D3D12_TEXTURE_BARRIER b{};b.pResource=source;b.SyncBefore=D3D12_BARRIER_SYNC_COMPUTE_SHADING;
    b.AccessBefore=D3D12_BARRIER_ACCESS_SHADER_RESOURCE;b.AccessAfter=D3D12_BARRIER_ACCESS_NO_ACCESS;
    b.LayoutBefore=D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;b.LayoutAfter=D3D12_BARRIER_LAYOUT_GENERIC_READ;
    b.Subresources.IndexOrFirstMipLevel=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;return b;
}
void Reset(SpatialReadback& copy,ID3D12GraphicsCommandList7* list,ID3D12CommandAllocator* alloc)
{copy.Reset(list,false);const auto hr=list->Reset(alloc,nullptr);copy.Reset(list,true,hr);Hr(hr,"Reset");}
void Close(SpatialReadback& copy,ID3D12GraphicsCommandList7* list)
{copy.Close(list,false);const auto hr=list->Close();copy.Close(list,true,hr);Hr(hr,"Close");}
void Prepare(SpatialReadback& copy,ID3D12GraphicsCommandList7* list,ID3D12Resource* texture)
{
    Check(copy.Begin(),"first request");copy.Discover(list,texture);copy.Poll();
    const auto r=copy.Snapshot();if(r.phase!=CopyPhase::Ready)std::cerr<<r.reason<<' '<<std::hex<<r.error<<std::dec<<'\n';
    Check(r.phase==CopyPhase::Ready,"prepared");
    Check(!copy.Arm(list,texture,77),"unknown Reset never arms");
}
int main()
{
    ComPtr<ID3D12Debug> debug;Hr(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),"debug layer required for barrier proof");debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;Hr(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"factory");
    ComPtr<IDXGIAdapter> warp;Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)),"WARP");
    ComPtr<ID3D12Device> device;Hr(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");
    ComPtr<ID3D12InfoQueue> info;Hr(device.As(&info),"info queue");
    // The same real queue/fence and negative controls cover both byte layouts.
    // R8 is the existing byte-exact invariant; R16 exercises both bytes of every
    // half value through the final depth slice, without assuming their semantics.
    for(bool distanceVolume:{false,true})
    {
    D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE3D;td.Width=64;td.Height=32;
    td.DepthOrArraySize=264;td.MipLevels=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8_TYPELESS;
    td.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if(distanceVolume){td.Width=128;td.Height=64;td.DepthOrArraySize=1040;td.Format=DXGI_FORMAT_R16_TYPELESS;}
    const size_t rowSize=static_cast<size_t>(td.Width)*(distanceVolume?2u:1u);
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    ComPtr<ID3D12Resource> texture;Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&td,
        D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&texture)),"texture");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT rows{};UINT64 rowBytes{},bytes{};
    device->GetCopyableFootprints(&td,0,1,0,&fp,&rows,&rowBytes,&bytes);
    D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bytes;bd.Height=1;
    bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type=D3D12_HEAP_TYPE_UPLOAD;ComPtr<ID3D12Resource> upload;
    Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)),"upload");
    std::vector<uint8_t> expected(rowSize*td.Height*td.DepthOrArraySize);void* ptr{};D3D12_RANGE empty{};
    Hr(upload->Map(0,&empty,&ptr),"upload map");std::memset(ptr,0xA5,static_cast<size_t>(bytes));
    for(size_t z=0;z<td.DepthOrArraySize;++z)for(size_t y=0;y<td.Height;++y)for(size_t x=0;x<rowSize;++x)
    {
        const auto value=static_cast<uint8_t>((x*7+y*13+z*17)%256);
        expected[(z*td.Height+y)*rowSize+x]=value;static_cast<uint8_t*>(ptr)[fp.Offset+(z*td.Height+y)*fp.Footprint.RowPitch+x]=value;
    }
    upload->Unmap(0,nullptr);
    auto release=Release(texture.Get());auto releaseGroup=Group(&release);
    Check(SpatialReadback::MatchesRelease(release,texture.Get()),"observed release tuple");
    auto bad=release;bad.AccessAfter=D3D12_BARRIER_ACCESS_COMMON;
    Check(!SpatialReadback::MatchesRelease(bad,texture.Get()),"COMMON != NO_ACCESS");
    bad=release;bad.Subresources.NumMipLevels=1;
    Check(!SpatialReadback::MatchesRelease(bad,texture.Get()),"exact whole-subresource encoding");
    bad=release;bad.SyncBefore=D3D12_BARRIER_SYNC_ALL;
    Check(!SpatialReadback::MatchesRelease(bad,texture.Get()),"no broad sync inference");

    for(auto type:{D3D12_COMMAND_LIST_TYPE_DIRECT,D3D12_COMMAND_LIST_TYPE_COMPUTE})
    {
        D3D12_COMMAND_QUEUE_DESC qd{};qd.Type=type;
        ComPtr<ID3D12CommandQueue> queue;Hr(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)),"queue");
        ComPtr<ID3D12CommandAllocator> allocator;Hr(device->CreateCommandAllocator(type,IID_PPV_ARGS(&allocator)),"allocator");
        ComPtr<ID3D12GraphicsCommandList7> list;Hr(device->CreateCommandList(0,type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"list7");
        Hr(list->Close(),"initial close");
        SpatialReadback copy;Prepare(copy,list.Get(),texture.Get());Reset(copy,list.Get(),allocator.Get());
        Check(!copy.Arm(list.Get(),upload.Get(),77),"wrong source never arms");
        Check(!copy.Arm(nullptr,texture.Get(),77),"wrong list never arms");
        // Upload a known full volume; all padding is deliberately nonzero.
        D3D12_TEXTURE_BARRIER init{};init.pResource=texture.Get();init.Subresources=release.Subresources;
        init.SyncBefore=D3D12_BARRIER_SYNC_NONE;init.SyncAfter=D3D12_BARRIER_SYNC_COPY;
        init.AccessBefore=D3D12_BARRIER_ACCESS_NO_ACCESS;init.AccessAfter=D3D12_BARRIER_ACCESS_COPY_DEST;
        init.LayoutBefore=type==D3D12_COMMAND_LIST_TYPE_DIRECT?D3D12_BARRIER_LAYOUT_COMMON:D3D12_BARRIER_LAYOUT_GENERIC_READ;
        init.LayoutAfter=D3D12_BARRIER_LAYOUT_COPY_DEST;
        auto group=Group(&init);list->Barrier(1,&group);
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=texture.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        auto sampled=release;sampled.SyncBefore=D3D12_BARRIER_SYNC_COPY;sampled.SyncAfter=D3D12_BARRIER_SYNC_COMPUTE_SHADING;
        sampled.AccessBefore=D3D12_BARRIER_ACCESS_COPY_DEST;sampled.AccessAfter=D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
        sampled.LayoutBefore=D3D12_BARRIER_LAYOUT_COPY_DEST;sampled.LayoutAfter=D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
        group=Group(&sampled);list->Barrier(1,&group);
        Check(copy.Arm(list.Get(),texture.Get(),77),"known generation arms");copy.ExposureEnd(true);
        const auto before=injectedBarriers;
        // Another group's barrier must survive; production forwards ORIGINAL once.
        D3D12_GLOBAL_BARRIER global{};global.SyncBefore=global.SyncAfter=D3D12_BARRIER_SYNC_COMPUTE_SHADING;
        global.AccessBefore=global.AccessAfter=D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
        D3D12_BARRIER_GROUP gg{};gg.Type=D3D12_BARRIER_TYPE_GLOBAL;gg.NumBarriers=1;gg.pGlobalBarriers=&global;
        std::array<D3D12_BARRIER_GROUP,2> packet{gg,releaseGroup};const auto unchanged=packet;
        copy.Barrier(list.Get(),2,packet.data(),Forward);
        Check(injectedBarriers-before==2,"two injected transitions exactly");
        Check(std::memcmp(packet.data(),unchanged.data(),sizeof(packet))==0,"engine packet unchanged");
        list->Barrier(2,packet.data()); // exact production forwarding
        Check(copy.Snapshot().phase==CopyPhase::Recorded,"copy recorded");
        copy.Barrier(list.Get(),1,&releaseGroup,Forward);Check(injectedBarriers-before==2,"one shot");
        copy.Poll();Check(copy.Snapshot().mapCalls==0,"no Map before Close/Execute");
        Close(copy,list.Get());
        ComPtr<ID3D12Fence> gate;Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)),"GPU gate");
        Hr(queue->Wait(gate.Get(),1),"hold GPU deliberately");
        ID3D12CommandList* submitted[]={list.Get()};
        copy.Submit(queue.Get(),1,submitted,false);queue->ExecuteCommandLists(1,submitted);copy.Submit(queue.Get(),1,submitted,true);
        copy.Poll();Check(copy.Snapshot().phase==CopyPhase::WaitingGpu&&copy.Snapshot().mapCalls==0,"Execute return is not GPU completion");
        // CPU command-list Reset after submit is legal; it must not invalidate
        // the already queued copy, nor allow another copy in the new generation.
        copy.Reset(list.Get(),false);copy.Reset(list.Get(),true,S_OK);
        Check(!copy.Arm(list.Get(),texture.Get(),78),"no second in-flight sample");
        Hr(gate->Signal(1),"release GPU gate");
        const auto deadline=GetTickCount64()+8000;
        while(copy.Snapshot().phase==CopyPhase::WaitingGpu&&GetTickCount64()<deadline){copy.Poll();Sleep(1);}
        const auto result=copy.Snapshot();
        if(result.phase!=CopyPhase::Complete)std::cerr<<result.reason<<'\n';
        Check(result.phase==CopyPhase::Complete&&result.gpuCompleted,"own queue fence complete");
        Check(result.packed==expected,"every R8/R16 byte exact including final slice; padding removed");
        Check(copy.LatestRecord().packed==expected,"latest completed evidence retained");
        Check(result.mapCalls==1&&result.frame==77&&result.generation==1&&result.queue==reinterpret_cast<uint64_t>(queue.Get()),"paired transaction provenance");
        // A finished series may be started again: repeating a measurement must not
        // cost a game restart. What must still be refused is replacing one that is
        // in flight, which the phase and fence state below stand in for.
        Check(copy.Restartable(),"a settled series is restartable");
        Check(copy.Begin(),"a finished series can start again in the same process");
        Check(!copy.Restartable(),"a series that still owes transactions is not restartable");
        Check(!copy.Begin(),"a running series is never overwritten");
        Hr(allocator->Reset(),"allocator GPU done");

        // Negative controls do not add ANY command to a list.
        {
            SpatialReadback reject;Prepare(reject,list.Get(),texture.Get());Reset(reject,list.Get(),allocator.Get());
            Check(reject.Arm(list.Get(),texture.Get(),1),"negative arm");reject.ExposureEnd(true);
            std::array<D3D12_TEXTURE_BARRIER,2> duplicates{release,release};auto dup=Group(duplicates.data(),2);
            const auto n=injectedBarriers;reject.Barrier(list.Get(),1,&dup,Forward);
            Check(reject.Snapshot().phase==CopyPhase::Failed&&!reject.Snapshot().issued&&n==injectedBarriers,"duplicate release rejected without GPU command");Close(reject,list.Get());
        }
        {
            SpatialReadback reject;Prepare(reject,list.Get(),texture.Get());Reset(reject,list.Get(),allocator.Get());
            Check(reject.Arm(list.Get(),texture.Get(),1),"unstable arm");reject.ExposureEnd(false);
            reject.Barrier(list.Get(),1,&releaseGroup,Forward);Check(!reject.Snapshot().issued&&reject.Snapshot().phase==CopyPhase::Failed,"unstable GI context rejected");Close(reject,list.Get());
        }
        {
            SpatialReadback reject;Prepare(reject,list.Get(),texture.Get());Reset(reject,list.Get(),allocator.Get());
            Check(reject.Arm(list.Get(),texture.Get(),1),"thread arm");reject.ExposureEnd(true);
            std::thread foreign([&]{reject.Barrier(list.Get(),1,&releaseGroup,Forward);});foreign.join();
            Check(!reject.Snapshot().issued&&reject.Snapshot().phase==CopyPhase::Failed,"foreign recording thread rejected");Close(reject,list.Get());
        }
        {
            SpatialReadback reject;Prepare(reject,list.Get(),texture.Get());Reset(reject,list.Get(),allocator.Get());
            Check(reject.Arm(list.Get(),texture.Get(),1),"reset arm");reject.ExposureEnd(true);
            reject.Reset(list.Get(),false);reject.Reset(list.Get(),true,E_FAIL);
            reject.Barrier(list.Get(),1,&releaseGroup,Forward);Check(!reject.Snapshot().issued&&reject.Snapshot().phase==CopyPhase::Failed,"new/failed generation invalidates arm");Close(reject,list.Get());
        }
        {
            SpatialReadback reject;Prepare(reject,list.Get(),texture.Get());
            reject.Reset(list.Get(),true,S_OK);
            Check(!reject.Arm(list.Get(),texture.Get(),1),"missing Reset begin is not a known generation");
            Reset(reject,list.Get(),allocator.Get());Check(reject.Arm(list.Get(),texture.Get(),1),"close-negative arm");
            reject.ExposureEnd(true);Close(reject,list.Get());
            Check(!reject.Snapshot().issued&&reject.Snapshot().phase==CopyPhase::Failed,"missing release before Close fails");
        }
        {
            SpatialReadback reject;Prepare(reject,list.Get(),texture.Get());Reset(reject,list.Get(),allocator.Get());
            Check(reject.Arm(list.Get(),texture.Get(),1),"tuple-negative arm");reject.ExposureEnd(true);
            auto changed=release;changed.LayoutAfter=D3D12_BARRIER_LAYOUT_COMMON;auto changedGroup=Group(&changed);
            const auto n=injectedBarriers;reject.Barrier(list.Get(),1,&changedGroup,Forward);
            Check(!reject.Snapshot().issued&&n==injectedBarriers&&reject.Snapshot().phase==CopyPhase::Failed,"changed actual release adds no command");Close(reject,list.Get());
        }
        for(bool wrongQueue:{false,true})
        {
            SpatialReadback reject;Prepare(reject,list.Get(),texture.Get());Reset(reject,list.Get(),allocator.Get());
            // This recording is deliberately never executed. Failed transactions
            // retain resources because the instrument cannot prove non-submission.
            Check(reject.Arm(list.Get(),texture.Get(),1),"submit-negative arm");reject.ExposureEnd(true);
            reject.Barrier(list.Get(),1,&releaseGroup,Forward);list->Barrier(1,&releaseGroup);
            if(wrongQueue)
            {
                Close(reject,list.Get());
                D3D12_COMMAND_QUEUE_DESC wrongDesc{};wrongDesc.Type=type==D3D12_COMMAND_LIST_TYPE_DIRECT?
                    D3D12_COMMAND_LIST_TYPE_COMPUTE:D3D12_COMMAND_LIST_TYPE_DIRECT;
                ComPtr<ID3D12CommandQueue> wrong;Hr(device->CreateCommandQueue(&wrongDesc,IID_PPV_ARGS(&wrong)),"wrong-type queue");
                ID3D12CommandList* target[]={list.Get()};reject.Submit(wrong.Get(),1,target,false);
                Check(reject.Snapshot().phase==CopyPhase::Failed,"wrong queue rejected");
            }
            else
            {
                reject.Close(list.Get(),false);reject.Close(list.Get(),true,E_FAIL);
                Hr(list->Close(),"actual close after simulated failure");
                Check(reject.Snapshot().phase==CopyPhase::Failed,"failed Close rejects recorded copy");
            }
            reject.Poll();Check(reject.Snapshot().mapCalls==0&&!reject.Snapshot().gpuCompleted,"failed submission never maps");
        }
    }
    }
    const auto messages=info->GetNumStoredMessagesAllowedByRetrievalFilter();
    for(UINT64 i=0;i<messages;++i)
    {
        SIZE_T size{};info->GetMessage(i,nullptr,&size);std::vector<uint8_t> data(size);
        auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());Hr(info->GetMessage(i,message,&size),"debug message");
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_WARNING)std::cerr<<message->pDescription<<'\n';
        Check(message->Severity>D3D12_MESSAGE_SEVERITY_WARNING,"zero debug-layer warnings/errors");
    }
    Hr(device->GetDeviceRemovedReason(),"device healthy");
    std::cout<<"PASS "<<checks<<" WARP/debug-layer checks; DIRECT + COMPUTE exact volume copies, release preserved, fence gate and rejection controls. Synthetic, not game evidence.\n";
}
