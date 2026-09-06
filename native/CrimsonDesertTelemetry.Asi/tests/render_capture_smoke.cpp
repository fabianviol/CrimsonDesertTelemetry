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

namespace cdt::render { void InitializeCaptureForTest(uint64_t base); void CaptureFilter(uint64_t outer, uint64_t command); }
// The smoke executable links only memory/log support from imported research.
namespace cdt::instruments { bool OwnsCodeAddress(uint64_t) { return false; } }

namespace
{
using Microsoft::WRL::ComPtr;
void Check(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; ExitProcess(1); } }
void Hr(HRESULT value, const char* message) { if (FAILED(value)) { std::cerr << std::hex << value << ' '; Check(false,message); } }
template<class T, size_t N> void Put(std::array<uint8_t,N>& data, size_t offset, T value)
{ memcpy(data.data()+offset,&value,sizeof(value)); }
}
int main()
{
    using namespace cdt::render;
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
    ComPtr<ID3D12Resource> source, upload;
    Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&source)),"source");
    heap.Type=D3D12_HEAP_TYPE_UPLOAD; desc.Flags=D3D12_RESOURCE_FLAG_NONE;
    Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)),"upload");
    void* mapped{}; const D3D12_RANGE noReads{0,0}; Hr(upload->Map(0,&noReads,&mapped),"upload map");
    memset(mapped,0,LightBytes);
    constexpr std::array<float,8> light{1,2,3,3.14159265f,0.9f,0.3f,0.1f,0.005f};
    memcpy(mapped,light.data(),sizeof(light)); upload->Unmap(0,nullptr);
    list->CopyBufferRegion(source.Get(),0,upload.Get(),0,LightBytes);
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource=source.Get(); barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST; barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    list->ResourceBarrier(1,&barrier);

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
    std::array<uint8_t,16> holder{}; std::array<uint8_t,0x808> command{};
    Put(holder,8,reinterpret_cast<uint64_t>(list.Get())); Put(command,0x800,reinterpret_cast<uint64_t>(holder.data()));
    Check(OpenBridge(),"bridge");
    const auto mappingName=L"Local\\CrimsonDesertTelemetry.Render."+std::to_wstring(GetCurrentProcessId());
    HANDLE mapHandle=OpenFileMappingW(FILE_MAP_READ,FALSE,mappingName.c_str());
    const auto* bridge=static_cast<const Mapping*>(MapViewOfFile(mapHandle,FILE_MAP_READ,0,0,MappingBytes));
    Check(bridge!=nullptr,"read bridge");
    InitializeCaptureForTest(reinterpret_cast<uint64_t>(fakeBase));
    CaptureFilter(reinterpret_cast<uint64_t>(outer.data()),reinterpret_cast<uint64_t>(command.data()));
    PollCapture(); // prepare readback and install real same-device submission hook
    Sleep(2);
    CaptureFilter(reinterpret_cast<uint64_t>(outer.data()),reinterpret_cast<uint64_t>(command.data()));
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
    for (int i=0;i<400 && bridge->header.sampleSequence==0;++i) { PollCapture(); Sleep(5); }
    Check(bridge->header.state==Status::Active && bridge->header.sampleSequence==1 && bridge->header.frameNumber==100,
        "completed sample not published");
    Check(bridge->header.flags==7 && memcmp(bridge->lights,light.data(),sizeof(light))==0,"wrong light data/flags");
    Check(memcmp(bridge->scene,constants.data(),SceneBytes)==0,"camera not paired");

    Hr(allocator->Reset(),"reuse allocator"); Hr(list->Reset(allocator.Get(),nullptr),"reuse list");
    Put(constants,0x20,uint32_t{101}); Put(constants,0x80,-10527.0f);
    CaptureFilter(reinterpret_cast<uint64_t>(outer.data()),reinterpret_cast<uint64_t>(command.data()));
    Hr(list->Close(),"second close"); queue->ExecuteCommandLists(1,lists);
    for (int i=0;i<400 && bridge->header.sampleSequence<2;++i) { PollCapture(); Sleep(5); }
    Check(bridge->header.sampleSequence==2 && bridge->header.frameNumber==101,"recurring capture failed");
    Check(memcmp(bridge->scene,constants.data(),SceneBytes)==0,"second camera not paired");
    // Resource/list environment changes must fail before recording barriers
    // against resources prepared for another queue type.
    ComPtr<ID3D12CommandAllocator> computeAllocator;
    ComPtr<ID3D12GraphicsCommandList> computeList;
    Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,IID_PPV_ARGS(&computeAllocator)),"compute allocator");
    Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_COMPUTE,computeAllocator.Get(),nullptr,IID_PPV_ARGS(&computeList)),"compute list");
    Put(holder,8,reinterpret_cast<uint64_t>(computeList.Get()));
    Put(constants,0x20,uint32_t{102});
    Sleep(2);
    CaptureFilter(reinterpret_cast<uint64_t>(outer.data()),reinterpret_cast<uint64_t>(command.data()));
    PollCapture();
    Check(bridge->header.state==Status::Fault && bridge->header.error==ERROR_INVALID_HANDLE && bridge->header.sampleSequence==2,
        "changed queue type not refused before copy");
    StopCapture(); Check(bridge->header.state==Status::Stopped,"stop retained active result");
    UnmapViewOfFile(bridge); CloseHandle(mapHandle); VirtualFree(fakeBase,0,MEM_RELEASE);
    std::cout<<"Real D3D12/WARP: unrelated submission ignored; blocked GPU withheld; exact fence published paired data; second frame, queue-type rejection and stop passed.\n";
}
