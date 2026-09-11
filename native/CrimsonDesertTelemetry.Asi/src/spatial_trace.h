#pragma once
#include <windows.h>
#include <d3d12.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace cdt::spatial
{
// Fixed-size passive CPU-call trace, not a GPU state tracker. Never blocks a
// render/submit thread. Any loss is explicit; no absence or ordering inference
// may be made from a trace with drops, overflow or unknown reset generations.
enum class TraceKind { Barrier, ResetBegin, ResetEnd, CloseBegin, CloseEnd,
    ExecuteBegin, ExecuteEnd, ExposureBegin, ExposureEnd };
struct TraceEvent
{
    uint64_t order{}, tick{}, list{}, queue{}, generation{};
    uint32_t thread{};
    TraceKind kind{};
    HRESULT result{};
    bool generationKnown{};
    D3D12_TEXTURE_BARRIER barrier{};
};
struct TraceList
{
    uint64_t address{}, generation{};
    bool known{}, relevant{}, resetPending{};
};
struct SpatialTrace
{
    static constexpr size_t EventLimit=8192, ListLimit=512;
    SRWLOCK mutex=SRWLOCK_INIT;
    std::atomic<uint64_t> target{}, lost{};
    // The event buffer is almost all of this structure: 8192 entries of 120
    // bytes, most of it the embedded D3D12_TEXTURE_BARRIER. Held as a member
    // array it was ~0.95 MiB of zero-filled static data in every build that
    // compiles the probe in -- wasteful in a binary that usually never traces,
    // and a shape generic scanners weight heavily. It is allocated on the first
    // Begin() instead, under the same exclusive lock that every reader and
    // writer already takes, and then kept for the process lifetime so no hook
    // on a render thread can ever observe it disappear.
    std::unique_ptr<TraceEvent[]> events;
    std::array<TraceList,ListLimit> lists{};
    size_t eventCount{},listCount{};
    uint64_t startedTick{},order{},barrierCalls{},textureEntries{},targetBarriers{},resetCalls{},closeCalls{},executeCalls{};
    bool overflow{};

    void Begin(uint64_t resource)
    {
        AcquireSRWLockExclusive(&mutex);
        eventCount=listCount=0;order=barrierCalls=textureEntries=targetBarriers=0;
        resetCalls=closeCalls=executeCalls=0;overflow=false;lost=0;
        startedTick=resource?GetTickCount64():0;
        if(resource&&!events)
        {
            // nothrow: Begin runs on the probe's own path, but this object is
            // reachable from render-thread hooks and must not raise there.
            events.reset(new(std::nothrow) TraceEvent[EventLimit]{});
            if(!events)overflow=true;
        }
        target=resource;
        ReleaseSRWLockExclusive(&mutex);
    }
    bool Enter()
    {
        if(!target.load(std::memory_order_relaxed))return false;
        if(!TryAcquireSRWLockExclusive(&mutex)){++lost;return false;}
        if(!target.load(std::memory_order_relaxed)){ReleaseSRWLockExclusive(&mutex);return false;}
        return true;
    }
    TraceList* List(uint64_t address)
    {
        for(size_t i=0;i<listCount;++i)if(lists[i].address==address)return &lists[i];
        if(listCount==ListLimit){overflow=true;return nullptr;}
        auto& item=lists[listCount++];item={};item.address=address;return &item;
    }
    void Add(TraceList& list,TraceKind kind,HRESULT hr=0,uint64_t queue=0,const D3D12_TEXTURE_BARRIER* barrier=nullptr)
    {
        if(!events||eventCount==EventLimit){overflow=true;return;}
        auto& e=events[eventCount++];e={};e.order=++order;e.tick=GetTickCount64();
        e.thread=GetCurrentThreadId();e.list=list.address;e.queue=queue;
        e.generation=list.generation;e.generationKnown=list.known;e.kind=kind;e.result=hr;
        if(barrier)e.barrier=*barrier;
    }
    void Barrier(ID3D12GraphicsCommandList7* list,UINT groups,const D3D12_BARRIER_GROUP* data)
    {
        if(!Enter())return;
        ++barrierCalls;
        if(groups>64)overflow=true;
        else for(UINT g=0;g<groups;++g)
        {
            if(data[g].Type!=D3D12_BARRIER_TYPE_TEXTURE)continue;
            const auto& group=data[g];
            if(group.NumBarriers>4096){overflow=true;continue;}
            textureEntries+=group.NumBarriers;
            for(UINT i=0;i<group.NumBarriers;++i)
            {
                const auto& b=group.pTextureBarriers[i];
                if(reinterpret_cast<uint64_t>(b.pResource)!=target.load(std::memory_order_relaxed))continue;
                ++targetBarriers;
                if(auto* state=List(reinterpret_cast<uint64_t>(list)))
                {state->relevant=true;Add(*state,TraceKind::Barrier,0,0,&b);}
            }
        }
        ReleaseSRWLockExclusive(&mutex);
    }
    void Lifecycle(uint64_t list,TraceKind kind,HRESULT hr=0)
    {
        if(!Enter())return;
        if(kind==TraceKind::ResetBegin)++resetCalls;
        if(kind==TraceKind::CloseBegin)++closeCalls;
        if(auto* state=List(list))
        {
            if(kind==TraceKind::ResetBegin){state->known=false;state->resetPending=true;}
            if(kind==TraceKind::ResetEnd)
            {++state->generation;state->known=state->resetPending&&SUCCEEDED(hr);state->resetPending=false;}
            if(kind==TraceKind::ExposureBegin)state->relevant=true;
            if(state->relevant)Add(*state,kind,hr);
        }
        ReleaseSRWLockExclusive(&mutex);
    }
    void Submit(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList* const* submitted,bool after)
    {
        if(!Enter())return;
        if(!after)++executeCalls;
        if(count>1024)overflow=true;
        else for(UINT i=0;i<count;++i)
        {
            const auto address=reinterpret_cast<uint64_t>(submitted[i]);
            for(size_t j=0;j<listCount;++j)
                if(lists[j].address==address&&lists[j].relevant)
                    Add(lists[j],after?TraceKind::ExecuteEnd:TraceKind::ExecuteBegin,0,reinterpret_cast<uint64_t>(queue));
        }
        ReleaseSRWLockExclusive(&mutex);
    }
};
}
