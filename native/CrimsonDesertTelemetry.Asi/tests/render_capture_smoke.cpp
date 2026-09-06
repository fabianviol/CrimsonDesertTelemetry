#include "render_capture.h"
#include "render_bridge.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

namespace cdt::render
{
void InitializeCaptureForTest(uint64_t base);
const char* CapturePhaseForTest();
void CaptureFilter(uint64_t outer, uint64_t command, uint64_t counterOuter, uint64_t owner);
}
// The smoke executable links only memory/log support from imported research.
namespace cdt::instruments { bool OwnsCodeAddress(uint64_t) { return false; } }

namespace
{
using Microsoft::WRL::ComPtr;
void Check(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; ExitProcess(1); } }
void Hr(HRESULT value, const char* message) { if (FAILED(value)) { std::cerr << std::hex << value << ' '; Check(false,message); } }
template<class T, size_t N> void Put(std::array<uint8_t,N>& data, size_t offset, T value)
{ memcpy(data.data()+offset,&value,sizeof(value)); }

void CheckCapture(bool value, const cdt::render::Mapping* bridge, ID3D12Device* device, const char* message)
{
    if (value) return;
    std::cerr << message << ": phase=" << cdt::render::CapturePhaseForTest()
        << ", state=" << static_cast<uint32_t>(bridge->header.state)
        << ", sequence=" << bridge->header.sampleSequence << ", frame=" << bridge->header.frameNumber
        << ", capture-error=0x" << std::hex << cdt::render::CaptureFailureCode()
        << ", bridge-error=0x" << bridge->header.error
        << ", device-removed=0x" << device->GetDeviceRemovedReason() << std::dec << '\n';
    ExitProcess(1);
}

void WaitForSample(const cdt::render::Mapping* bridge, ID3D12Device* device,
    uint64_t sequence, uint32_t frame, const char* message)
{
    // Let the production five-second capture timeout report a fault first.
    // A fixed iteration count depended on the machine's Sleep granularity.
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(6);
    while (bridge->header.sampleSequence<sequence && std::chrono::steady_clock::now()<deadline)
    {
        cdt::render::PollCapture();
        if (cdt::render::CaptureFailureCode()!=0) break;
        if (bridge->header.sampleSequence<sequence) Sleep(5);
    }
    CheckCapture(bridge->header.state==cdt::render::Status::Active &&
        bridge->header.sampleSequence==sequence && bridge->header.frameNumber==frame, bridge, device, message);
}
}
int main(int argc, char** argv)
{
    using namespace cdt::render;
    const bool rejectCounterDevice=argc==2 && std::string(argv[1])=="--counter-device";
    ComPtr<IDXGIFactory4> factory; Hr(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"factory");
    ComPtr<IDXGIAdapter> warp; Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)),"WARP");
    ComPtr<ID3D12Device> device; Hr(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    ComPtr<ID3D12CommandQueue> queue; Hr(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)),"queue");
    ComPtr<ID3D12CommandAllocator> allocator, unrelatedAllocator;
    Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
    Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&unrelatedAllocator)),"other allocator");
    ComPtr<ID3D12GraphicsCommandList> list, unrelated;
    Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"list");
    Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,unrelatedAllocator.Get(),nullptr,IID_PPV_ARGS(&unrelated)),"other list");
    Hr(unrelated->Close(),"other close");
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width=LightBytes; desc.Height=1;
    desc.DepthOrArraySize=1; desc.MipLevels=1; desc.SampleDesc.Count=1;
    desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR; desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT; heap.CreationNodeMask=heap.VisibleNodeMask=1;
    ComPtr<ID3D12Resource> source, source2, upload, counter, counter2, counterUpload, shortCounter;
    Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&source)),"source");
    Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&source2)),"second source");
    desc.Width=CounterBytes*2;
    Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&counter)),"counter");
    Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&counter2)),"second counter");
    desc.Width=CounterBytes/2;
    Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&shortCounter)),"undersized counter");
    heap.Type=D3D12_HEAP_TYPE_UPLOAD; desc.Flags=D3D12_RESOURCE_FLAG_NONE;
    desc.Width=LightBytes;
    Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)),"upload");
    desc.Width=CounterBytes*2;
    Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&counterUpload)),"counter upload");
    void* mapped{}; const D3D12_RANGE noReads{0,0}; Hr(upload->Map(0,&noReads,&mapped),"upload map");
    memset(mapped,0,LightBytes);
    constexpr std::array<float,8> light{1,2,3,3.14159265f,0.9f,0.3f,0.1f,0.005f};
    memcpy(mapped,light.data(),sizeof(light)); upload->Unmap(0,nullptr);
    std::array<uint32_t,CounterBytes/sizeof(uint32_t)> counterData{}, counterData2{};
    for (uint32_t i=0;i<counterData.size();++i) { counterData[i]=i*17+3; counterData2[i]=i*23+7; }
    counterData[1]=counterData2[1]=1; // One valid light; capture transports bytes without interpreting the count.
    Hr(counterUpload->Map(0,&noReads,&mapped),"counter upload map");
    memcpy(mapped,counterData.data(),CounterBytes);
    memcpy(static_cast<uint8_t*>(mapped)+CounterBytes,counterData2.data(),CounterBytes);
    counterUpload->Unmap(0,nullptr);
    list->CopyBufferRegion(source.Get(),0,upload.Get(),0,LightBytes);
    list->CopyBufferRegion(source2.Get(),0,upload.Get(),0,LightBytes);
    list->CopyBufferRegion(counter.Get(),0,counterUpload.Get(),0,CounterBytes);
    list->CopyBufferRegion(counter2.Get(),0,counterUpload.Get(),CounterBytes,CounterBytes);
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource=source.Get(); barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST; barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    list->ResourceBarrier(1,&barrier);
    barrier.Transition.pResource=source2.Get(); list->ResourceBarrier(1,&barrier);
    barrier.Transition.pResource=counter.Get(); list->ResourceBarrier(1,&barrier);
    barrier.Transition.pResource=counter2.Get(); list->ResourceBarrier(1,&barrier);

    std::array<uint8_t,SceneBytes> constants{};
    Put(constants,0xAC0,6360000.0f); Put(constants,0x20,uint32_t{100});
    Put(constants,0x30,std::array<float,4>{3840,2160,1.0f/3840,1.0f/2160});
    Put(constants,0x80,std::array<float,4>{-10528,611,-4354,0}); Put(constants,0x90,std::array<float,4>{0,0,1,0});
    std::array<uint8_t,0x500> root{};
    Put(root,0x428,reinterpret_cast<uint64_t>(constants.data()));
    auto* fakeBase=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x6B50000,MEM_RESERVE,PAGE_NOACCESS));
    Check(fakeBase!=nullptr,"reserve fake module");
    Check(VirtualAlloc(fakeBase+0x6B4E000,0x1000,MEM_COMMIT,PAGE_READWRITE)!=nullptr,"commit fake root");
    const auto rootPtr=reinterpret_cast<uint64_t>(root.data()); memcpy(fakeBase+0x6B4EFB8,&rootPtr,8);
    std::array<uint8_t,0x200> inner{}; std::array<uint8_t,0x38> outer{};
    Put(inner,0xC0,RecordStride); Put(inner,0xC4,RecordCount); Put(inner,0x168,reinterpret_cast<uint64_t>(source.Get()));
    Put(outer,0x30,reinterpret_cast<uint64_t>(inner.data()));
    std::array<uint8_t,0x200> counterInner{}; std::array<uint8_t,0x38> counterOuter{};
    Put(counterInner,0xC0,uint32_t{4}); Put(counterInner,0xC4,uint32_t{128});
    Put(counterInner,0x168,reinterpret_cast<uint64_t>(counter.Get()));
    Put(counterOuter,0x30,reinterpret_cast<uint64_t>(counterInner.data()));
    std::array<uint8_t,0x900> owner{};
    std::array<uint8_t,16> holder{}; std::array<uint8_t,0x808> command{};
    Put(holder,8,reinterpret_cast<uint64_t>(list.Get())); Put(command,0x800,reinterpret_cast<uint64_t>(holder.data()));
    Check(OpenBridge(),"bridge");
    const auto mappingName=L"Local\\CrimsonDesertTelemetry.Render."+std::to_wstring(GetCurrentProcessId());
    HANDLE mapHandle=OpenFileMappingW(FILE_MAP_READ,FALSE,mappingName.c_str());
    const auto* bridge=static_cast<const Mapping*>(MapViewOfFile(mapHandle,FILE_MAP_READ,0,0,MappingBytes));
    Check(bridge!=nullptr,"read bridge");
    InitializeCaptureForTest(reinterpret_cast<uint64_t>(fakeBase));
    const auto capture=[&] {
        CaptureFilter(reinterpret_cast<uint64_t>(outer.data()),reinterpret_cast<uint64_t>(command.data()),
            reinterpret_cast<uint64_t>(counterOuter.data()),reinterpret_cast<uint64_t>(owner.data()));
    };
    // A malformed/missing/undersized counter cannot silently downgrade a pair
    // to the former light-only capture. These calls occur before discovery.
    CaptureFilter(reinterpret_cast<uint64_t>(outer.data()),reinterpret_cast<uint64_t>(command.data()),0,0);
    Put(counterInner,0x168,uint64_t{0}); capture(); PollCapture();
    Put(counterInner,0x168,reinterpret_cast<uint64_t>(shortCounter.Get()));
    capture(); PollCapture();
    Put(counterInner,0x168,reinterpret_cast<uint64_t>(counterUpload.Get())); capture(); PollCapture();
    Put(counterInner,0x168,reinterpret_cast<uint64_t>(source.Get())); capture(); PollCapture();
    Check(bridge->header.sampleSequence==0,"invalid counter published a light-only sample");
    Put(counterInner,0x168,reinterpret_cast<uint64_t>(counter.Get())); capture();
    PollCapture(); // prepare readback and install real same-device submission hook
    CheckCapture(std::strcmp(CapturePhaseForTest(),"ready (no copy recorded)")==0,
        bridge,device.Get(),"capture preparation did not become ready");
    capture();
    CheckCapture(std::strcmp(CapturePhaseForTest(),"recorded (not submitted)")==0,
        bridge,device.Get(),"capture step did not record a GPU copy");
    Put(owner,0x8F8,uint32_t{99}); // Publication must keep the index captured BEFORE GPU execution.
    Hr(list->Close(),"target close");
    ID3D12CommandList* unrelatedLists[]{unrelated.Get()}; queue->ExecuteCommandLists(1,unrelatedLists);
    PollCapture(); Check(bridge->header.sampleSequence==0,"unrelated list falsely completed sample");

    // Hold the GPU behind an independent fence. The target submission is real,
    // but its copy cannot complete. A sleep-based reader would publish garbage.
    ComPtr<ID3D12Fence> gate; Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)),"gate");
    Hr(queue->Wait(gate.Get(),1),"gate wait");
    ID3D12CommandList* lists[]{list.Get()}; queue->ExecuteCommandLists(1,lists);
    for (int i=0;i<30;++i) { PollCapture(); Sleep(5); }
    Check(bridge->header.sampleSequence==0,"readback published before GPU fence completion");
    Hr(gate->Signal(1),"release gate");
    WaitForSample(bridge,device.Get(),1,100,"completed sample not published");
    Check(bridge->header.flags==15 && memcmp(bridge->lights,light.data(),sizeof(light))==0,"wrong light data/flags");
    Check(memcmp(bridge->scene,constants.data(),SceneBytes)==0,"camera not paired");
    Check(memcmp(bridge->counters,counterData.data(),CounterBytes)==0,"counter bytes not paired with completed light copy");
    Check(bridge->header.outputResource==reinterpret_cast<uint64_t>(source.Get()) &&
        bridge->header.counterResource==reinterpret_cast<uint64_t>(counter.Get()) &&
        bridge->header.owner==reinterpret_cast<uint64_t>(owner.data()) && bridge->header.bufferIndex==0,
        "first pair identity was resolved again after capture");

    Hr(allocator->Reset(),"reuse allocator"); Hr(list->Reset(allocator.Get(),nullptr),"reuse list");
    Put(constants,0x20,uint32_t{101}); Put(constants,0x80,-10527.0f);
    Put(inner,0x168,reinterpret_cast<uint64_t>(source2.Get()));
    Put(counterInner,0x168,reinterpret_cast<uint64_t>(counter2.Get())); Put(owner,0x8F8,uint32_t{1});
    capture(); Put(owner,0x8F8,uint32_t{98});
    Hr(list->Close(),"second close"); queue->ExecuteCommandLists(1,lists);
    WaitForSample(bridge,device.Get(),2,101,"recurring capture failed");
    Check(memcmp(bridge->scene,constants.data(),SceneBytes)==0,"second camera not paired");
    Check(memcmp(bridge->counters,counterData2.data(),CounterBytes)==0 &&
        bridge->header.outputResource==reinterpret_cast<uint64_t>(source2.Get()) &&
        bridge->header.counterResource==reinterpret_cast<uint64_t>(counter2.Get()) && bridge->header.bufferIndex==1,
        "second resource bank reused the first counter or publication-time index");
    Put(constants,0x20,uint32_t{102}); Put(counterInner,0x168,uint64_t{0});
    capture(); PollCapture();
    Check(bridge->header.sampleSequence==2 && bridge->header.frameNumber==101,
        "failed counter resolution republished the previous pair as a new frame");
    Put(counterInner,0x168,reinterpret_cast<uint64_t>(counter2.Get()));
    // Resource/list environment changes must fail before recording barriers
    // against resources prepared for another queue type.
    ComPtr<ID3D12CommandAllocator> computeAllocator;
    ComPtr<ID3D12GraphicsCommandList> computeList;
    Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,IID_PPV_ARGS(&computeAllocator)),"compute allocator");
    Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_COMPUTE,computeAllocator.Get(),nullptr,IID_PPV_ARGS(&computeList)),"compute list");
    ComPtr<ID3D12Device> otherDevice; ComPtr<ID3D12Resource> foreignCounter;
    if (rejectCounterDevice)
    {
        // D3D12CreateDevice on the same WARP adapter returns the existing
        // device; that is not a negative control. This optional local case
        // requires a separate hardware adapter, unlike the portable WARP test.
        for (UINT adapterIndex=0; !otherDevice; ++adapterIndex)
        {
            ComPtr<IDXGIAdapter1> candidate;
            if (factory->EnumAdapters1(adapterIndex,&candidate)==DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 adapterDesc{};
            if (FAILED(candidate->GetDesc1(&adapterDesc)) || (adapterDesc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)!=0) continue;
            D3D12CreateDevice(candidate.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&otherDevice));
        }
        Check(otherDevice && otherDevice.Get()!=device.Get(),"--counter-device requires a distinct hardware adapter");
        heap.Type=D3D12_HEAP_TYPE_DEFAULT; desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        Hr(otherDevice->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,IID_PPV_ARGS(&foreignCounter)),"foreign counter");
        Put(counterInner,0x168,reinterpret_cast<uint64_t>(foreignCounter.Get()));
    }
    else Put(holder,8,reinterpret_cast<uint64_t>(computeList.Get()));
    capture();
    PollCapture();
    CheckCapture(bridge->header.state==Status::Fault && bridge->header.error==ERROR_INVALID_HANDLE && bridge->header.sampleSequence==2,
        bridge,device.Get(),"changed queue type/counter device not refused before copy");
    StopCapture(); Check(bridge->header.state==Status::Stopped,"stop retained active result");
    UnmapViewOfFile(bridge); CloseHandle(mapHandle); VirtualFree(fakeBase,0,MEM_RELEASE);
    std::cout<<"Real D3D12/WARP: invalid counters refused; unrelated submission ignored; blocked GPU withheld; exact fence published paired scene/lights/256 counter bytes; alternating resource identities frozen at capture; "
        <<(rejectCounterDevice ? "counter-device" : "queue-type")<<" rejection and stop passed.\n";
}
