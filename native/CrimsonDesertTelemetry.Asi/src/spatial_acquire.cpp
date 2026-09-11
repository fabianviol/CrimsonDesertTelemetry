#include "spatial_probe.h"
#include "spatial_sample.h"
#include "sky_bridge.h"
#include "submission_observer.h"
#include "console/mem.h"
#include <windows.h>
#include <d3d12.h>
#include <MinHook.h>
#include <array>
#include <wrl/client.h>
#include <mutex>

extern "C" void CdtSpatialThunk();
namespace cdt::spatial
{
namespace
{
constexpr uint32_t DispatchRva = 0x37B6520;
constexpr std::array<uint8_t, 14> Signature{0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56};
using DispatchFn = uint64_t(*)(uint64_t, uint32_t, uint32_t, uint32_t);
DispatchFn originalDispatch{};
void* dispatchTarget{};

Microsoft::WRL::ComPtr<ID3D12Resource> readbackBuffer;
Microsoft::WRL::ComPtr<ID3D12Fence> readbackFence;
uint64_t fenceValue = 0;
bool acquisitionInFlight = false;
uint32_t latestFrame = 0;
uint64_t giOffset = 0;
ID3D12GraphicsCommandList* activeList = nullptr;
std::mutex acquireMutex;

template<class T> bool Read(uint64_t address, T& value)
{
    return address >= 0x10000 && ch::mem::SafeRead(reinterpret_cast<void*>(address), &value, sizeof(value));
}

bool Resolve(uint64_t owner, uint64_t command, ID3D12Resource*& resource, ID3D12Resource*& gi, ID3D12GraphicsCommandList*& nativeList, uint32_t& frame)
{
    uint64_t renderer{}, back{}, outer{}, storage{}, holder{}, sceneOwner{}, sceneData{}, cbWrapper{}, cbOuter{}, cbStorage{};
    uint32_t width{}, height{}, depth{};
    uint8_t bank{};
    if (!Read(owner + 0x10, renderer) || !Read(renderer + 0x660, back) || back != owner ||
        !Read(owner + 0x4B8, outer) || !Read(outer + 0x30, storage) ||
        !Read(storage + 0x10, back) || back != outer || !Read(storage + 0x100, reinterpret_cast<uint64_t&>(resource)) || !resource ||
        !Read(storage + 0xD0, width) || !Read(storage + 0xD4, height) || !Read(storage + 0xD8, depth) ||
        width != 64 || height != 32 || depth != 264 || !Read(command + 0x800, holder) ||
        !Read(holder + 8, reinterpret_cast<uint64_t&>(nativeList)) || !nativeList ||
        !Read(owner + 8, sceneOwner) || !Read(sceneOwner + 0x428, sceneData) ||
        !Read(sceneData + 0x20, frame) ||
        !Read(owner + 0x705, bank) ||
        !Read(owner + (bank ? 0x568 : 0x560), cbWrapper) || !Read(cbWrapper + 0x18, cbOuter) ||
        !Read(cbOuter + 0x30, cbStorage) || !Read(cbStorage + 0x168, reinterpret_cast<uint64_t&>(gi)) || !gi) return false;
    
    auto desc = resource->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D || desc.Width != 64 || desc.Format != DXGI_FORMAT_R8_TYPELESS) return false;
    return true;
}

void OnSubmission(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists, bool after)
{
    std::lock_guard<std::mutex> lock(acquireMutex);
    if (!acquisitionInFlight || !after || !readbackFence || !activeList) return;
    
    bool listFound = false;
    for (UINT i = 0; i < count; ++i) {
        if (lists[i] == activeList) { listFound = true; break; }
    }
    
    if (listFound) {
        fenceValue++;
        queue->Signal(readbackFence.Get(), fenceValue);
        activeList = nullptr;
    }
}

} // namespace

uint64_t Dispatch(uint64_t command, uint32_t x, uint32_t y, uint32_t z, uint64_t owner, uint64_t caller)
{
    (void)caller;
    auto result = originalDispatch(command, x, y, z);
    std::lock_guard<std::mutex> lock(acquireMutex);
    
    if (acquisitionInFlight) return result;
    
    ID3D12Resource* resource{};
    ID3D12Resource* gi{};
    ID3D12GraphicsCommandList* list{};
    uint32_t frame{};
    
    if (x == 2 && y == 1 && z == 1 && Resolve(owner, command, resource, gi, list, frame))
    {
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if (SUCCEEDED(resource->GetDevice(IID_PPV_ARGS(&device))))
        {
            if (!readbackBuffer)
            {
                D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
                uint32_t rows{};
                uint64_t rowBytes{}, totalBytes{};
                auto desc = resource->GetDesc();
                device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowBytes, &totalBytes);
                
                giOffset = (totalBytes + 511) & ~UINT64{511};
                uint64_t allocationSize = giOffset + ConstantBytes;
                
                D3D12_RESOURCE_DESC bufferDesc{};
                bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                bufferDesc.Width = allocationSize;
                bufferDesc.Height = 1;
                bufferDesc.DepthOrArraySize = 1;
                bufferDesc.MipLevels = 1;
                bufferDesc.SampleDesc.Count = 1;
                bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                
                D3D12_HEAP_PROPERTIES heap{};
                heap.Type = D3D12_HEAP_TYPE_READBACK;
                heap.CreationNodeMask = 1;
                heap.VisibleNodeMask = 1;
                
                if (SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readbackBuffer))))
                {
                    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&readbackFence));
                }
            }
            
            if (readbackBuffer && readbackFence)
            {
                D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
                uint32_t rows{};
                uint64_t rowBytes{}, totalBytes{};
                auto desc = resource->GetDesc();
                device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowBytes, &totalBytes);
                
                D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
                dst.pResource = readbackBuffer.Get();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                dst.PlacedFootprint = footprint;
                
                src.pResource = resource;
                src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                src.SubresourceIndex = 0;
                
                list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                list->CopyBufferRegion(readbackBuffer.Get(), giOffset, gi, 0, ConstantBytes);
                
                acquisitionInFlight = true;
                latestFrame = frame;
                activeList = list;
            }
        }
    }
    return result;
}

} // namespace cdt::spatial

extern "C" uint64_t CdtSpatialDispatch(uint64_t command, uint32_t x, uint32_t y, uint32_t z, uint64_t owner, uint64_t caller)
{
    return cdt::spatial::Dispatch(command, x, y, z, owner, caller);
}

namespace cdt::spatial
{
bool Start(uint64_t moduleBase, const wchar_t*, bool enableReadback, unsigned, unsigned, unsigned, bool, bool, bool)
{
    if (!enableReadback) return false;
    std::array<uint8_t, Signature.size()> bytes{};
    if (!Read(moduleBase + DispatchRva, bytes) || bytes != Signature) return false;
    
    dispatchTarget = reinterpret_cast<void*>(moduleBase + DispatchRva);
    if (MH_CreateHook(dispatchTarget, reinterpret_cast<void*>(CdtSpatialThunk), reinterpret_cast<void**>(&originalDispatch)) != MH_OK) return false;
    if (MH_EnableHook(dispatchTarget) != MH_OK) return false;
    
    render::submissionObserver = OnSubmission;
    return true;
}

void Poll()
{
    std::lock_guard<std::mutex> lock(acquireMutex);
    if (!acquisitionInFlight || !readbackFence || !readbackBuffer || activeList) return;
    
    if (readbackFence->GetCompletedValue() >= fenceValue)
    {
        void* mappedData = nullptr;
        if (SUCCEEDED(readbackBuffer->Map(0, nullptr, &mappedData)))
        {
            uint8_t* constants = static_cast<uint8_t*>(mappedData) + giOffset;
            uint8_t* volume = static_cast<uint8_t*>(mappedData);
            
            auto reference = SampleAtReference(constants, volume);
            auto state = reference.status == SampleStatus::Ok ? sky::Visibility::Valid 
                       : reference.status == SampleStatus::Fallback ? sky::Visibility::Fallback 
                       : sky::Visibility::Unavailable;
                       
            sky::PublishVisibility(reference.skyVisibility, state, latestFrame, GetTickCount64());
            
            readbackBuffer->Unmap(0, nullptr);
        }
        else
        {
            sky::PublishVisibility(0.0, sky::Visibility::Unavailable, 0, 0);
        }
        
        acquisitionInFlight = false;
    }
}

void Stop()
{
    render::submissionObserver = nullptr;
    if (dispatchTarget) MH_DisableHook(dispatchTarget);
    readbackBuffer.Reset();
    readbackFence.Reset();
    acquisitionInFlight = false;
    sky::PublishVisibility(0.0, sky::Visibility::Unavailable, 0, 0);
}

bool OwnsCodeAddress(uint64_t address)
{
    return dispatchTarget && address >= reinterpret_cast<uint64_t>(dispatchTarget) && address < reinterpret_cast<uint64_t>(dispatchTarget) + 32;
}
}
