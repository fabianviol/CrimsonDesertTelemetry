// One-shot GPU readback for the fire-light investigation.
//
// This is intentionally build-pinned and command-driven. `prepare` consumes a
// bpcapture at the exact g_manyLightsDataBufferUAV binding, validates the live
// BufferD3D12 layout, and creates a READBACK buffer. `arm` then places the
// existing one-shot software breakpoint immediately after the producer's
// Dispatch(256,1,1). Its hit callback records a transition/copy/transition on
// that render thread. `read` maps the result only after a conservative delay.
#include "manylights.h"
#include "breakpoint.h"
#include "common.h"
#include "mem.h"
#include "../instruments.h"

#include <d3d12.h>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <intrin.h>

namespace ch { namespace manylights {

namespace {

constexpr u64 kBindingReturnRva = 0x2F007AB;
constexpr u64 kAfterDispatchRva = 0x2F007F5;
constexpr u64 kByteSize         = 1572864;
constexpr u32 kStride           = 48;
constexpr u32 kCount            = 32768;
constexpr u64 kCounterByteSize  = 512;
constexpr u32 kCounterStride    = 4;
constexpr u32 kCounterCount     = 128;
constexpr u64 kFilterBindingReadyRva = 0x3CB6584;
constexpr u64 kFilterAfterDispatchRva = 0x3CB65CA;
constexpr u64 kEmitterBindingReadyRva    = 0x2EFF301;
constexpr u32 kEmitterStride             = 224;
constexpr u32 kEmitterCount              = 2048;
constexpr u64 kSimulationBindingReadyRva = 0x2EFF487;
constexpr u32 kSimulationStride          = 336;
constexpr u32 kSimulationViewCount       = 1280;
constexpr ULONGLONG kReadDelayMs = 1000;

enum CopyState : LONG { Idle = 0, Armed = 1, Copying = 2, Issued = 3, Read = 4, Failed = 5 };

ID3D12Resource* g_source = nullptr;
ID3D12Resource* g_readback = nullptr;
ID3D12Resource* g_counterSource = nullptr;
ID3D12Resource* g_counterReadback = nullptr;
u64             g_outer = 0;
u64             g_inner = 0;
u64             g_counterOuter = 0;
u64             g_counterInner = 0;
u64             g_counterOuterUsed = 0;
u64             g_counterInnerUsed = 0;
u64             g_counterSourceUsed = 0;
volatile LONG   g_state = Idle;
volatile LONG   g_error = 0;
u64             g_commandList = 0;
ULONGLONG       g_issuedAt = 0;

ID3D12Resource* g_filterSource = nullptr;
ID3D12Resource* g_filterReadback = nullptr;
u64             g_filterOuter = 0;
u64             g_filterInner = 0;
u64             g_filterOuterUsed = 0;
u64             g_filterInnerUsed = 0;
u64             g_filterSourceUsed = 0;
u64             g_filterCommandList = 0;
volatile LONG   g_filterState = Idle;
volatile LONG   g_filterError = 0;
ULONGLONG       g_filterIssuedAt = 0;

ID3D12Resource* g_emitterSource = nullptr;
ID3D12Resource* g_emitterReadback = nullptr;
u64             g_emitterOuter = 0;
u64             g_emitterInner = 0;
u64             g_emitterCommandList = 0;
u64             g_emitterByteSize = 0;
u32             g_emitterStride = 0;
u32             g_emitterCount = 0;
bool            g_emitterSimulation = false;
D3D12_HEAP_TYPE g_emitterHeapType = D3D12_HEAP_TYPE_DEFAULT;
volatile LONG   g_emitterState = Idle;
volatile LONG   g_emitterError = 0;
ULONGLONG       g_emitterIssuedAt = 0;

// Temporary provenance probe for the three BufferD3D12 CopyBufferRegion
// wrappers. The command object's vtable is cloned so unrelated command-list
// objects and native D3D12 calls remain untouched.
constexpr u64 kCopyOuterUploadRva = 0x37B6CE0;  // vtable +0x648
constexpr u64 kCopyOuterCopyRva   = 0x37B6A40;  // vtable +0x650
constexpr u64 kCopyInnerUploadRva = 0x37B6F60;  // vtable +0x658
constexpr size_t kCommandVtableEntries = 512;
constexpr size_t kCopyOuterUploadIndex = 0x648 / sizeof(void*);
constexpr size_t kCopyOuterCopyIndex   = 0x650 / sizeof(void*);
constexpr size_t kCopyInnerUploadIndex = 0x658 / sizeof(void*);

using EngineCopyFn = void (*)(void*, void*, u32, void*, u32, u32);
u64          g_writerCommandObject = 0;
void**       g_writerOriginalVtable = nullptr;
void**       g_writerCloneVtable = nullptr;
EngineCopyFn g_writerOriginalOuterUpload = nullptr;
EngineCopyFn g_writerOriginalOuterCopy = nullptr;
EngineCopyFn g_writerOriginalInnerUpload = nullptr;
u64          g_writerTargetOuter = 0;
u64          g_writerTargetInner = 0;
u64          g_writerTargetResource = 0;
volatile LONG g_writerArmed = 0;
volatile LONG g_writerHits = 0;
volatile LONG g_writerMethod = 0;
u64           g_writerDestination = 0;
u64           g_writerSource = 0;
u64           g_writerSourceInner = 0;
u64           g_writerSourceResource = 0;
u64           g_writerCaller = 0;
u32           g_writerDestinationOffset = 0;
u32           g_writerSourceOffset = 0;
u32           g_writerByteCount = 0;
u32           g_writerThread = 0;
ULONGLONG     g_writerSeenAt = 0;

// One-shot write capture for the engine-side 2,048 x 224 CPU pool. Unlike the
// execute breakpoints, this makes one selected data page read-only until the
// first writer reaches it. Reads and the later GPU upload do not trigger it.
enum PoolWatchState : LONG {
    PoolWatchIdle = 0, PoolWatchArmed = 1, PoolWatchClaimed = 2,
    PoolWatchCompleted = 3
};
volatile LONG g_poolWatchState = PoolWatchIdle;
PVOID         g_poolWatchHandler = nullptr;
u64           g_poolWatchRequested = 0;
u64           g_poolWatchPage = 0;
SIZE_T        g_poolWatchPageSize = 0;
DWORD         g_poolWatchOriginalProtect = 0;
u64           g_poolWatchFaultAddress = 0;
bp::Capture   g_poolWatchCapture;

// Conditional breakpoint at the 0xE0 record append call. It stays armed across
// non-matching emitters and completes only when R9 points at the known fire
// position. At that call site RDI is the work item, R13/R9 the built record,
// and the caller's [RSP+0x58] is the upstream group pointer used for +0xD8.
constexpr u64 kFireAppendCallRva = 0x2EFC670;
constexpr u64 kFireOwnerCallRva = 0x3010C77;
constexpr u64 kFireSourceSetterRva = 0x2ECF350;
constexpr float kFireX = -10529.755f;
constexpr float kFireY = 611.292f;
constexpr float kFireZ = -4420.300f;
enum FireWatchKind : LONG {
    FireWatchBuilder = 0, FireWatchOwner = 1, FireWatchSource = 2
};
enum FireWatchState : LONG {
    FireWatchIdle = 0, FireWatchArmed = 1, FireWatchClaimed = 2,
    FireWatchCompleted = 3
};
volatile LONG g_fireWatchState = FireWatchIdle;
FireWatchKind g_fireWatchKind = FireWatchBuilder;
PVOID         g_fireWatchHandler = nullptr;
u64           g_fireWatchTarget = 0;
u64           g_fireWatchCallTarget = 0;
u8            g_fireWatchOriginalByte = 0;
bp::Capture   g_fireWatchCapture;
u64           g_fireWatchWorkItem = 0;
u64           g_fireWatchSourceRecord = 0;
u64           g_fireWatchUpstream = 0;
u64           g_fireWatchOwner = 0;
u64           g_fireWatchParent = 0;
u64           g_fireWatchAssociationSource = 0;
u64           g_fireWatchPaired150 = 0;
u64           g_fireWatchPaired20 = 0;
u32           g_fireWatchRecordIndex = 0;
u32           g_fireWatchGroupId = 0;
u32           g_fireWatchSourceGroupId = 0;
volatile LONG g_fireWatchRejected = 0;
constexpr size_t kFireSourceSnapshotSize = 0xE0;
constexpr size_t kFireWorkItemSnapshotSize = 0x2C0;
constexpr size_t kFireUpstreamSnapshotSize = 0x220;
constexpr size_t kFireOwnerSnapshotSize = 0x2D8;
constexpr size_t kFireParentSnapshotSize = 0x300;
constexpr size_t kFireAssociationSourceSnapshotSize = 0x100;
constexpr size_t kFirePaired150SnapshotSize = 0x150;
constexpr size_t kFirePaired20SnapshotSize = 0x20;
u8            g_fireSourceSnapshot[kFireSourceSnapshotSize] = {};
u8            g_fireWorkItemSnapshot[kFireWorkItemSnapshotSize] = {};
u8            g_fireUpstreamSnapshot[kFireUpstreamSnapshotSize] = {};
u8            g_fireOwnerSnapshot[kFireOwnerSnapshotSize] = {};
u8            g_fireParentSnapshot[kFireParentSnapshotSize] = {};
u8            g_fireAssociationSourceSnapshot[kFireAssociationSourceSnapshotSize] = {};
u8            g_firePaired150Snapshot[kFirePaired150SnapshotSize] = {};
u8            g_firePaired20Snapshot[kFirePaired20SnapshotSize] = {};
bool          g_fireSourceSnapshotValid = false;
bool          g_fireWorkItemSnapshotValid = false;
bool          g_fireUpstreamSnapshotValid = false;
bool          g_fireOwnerSnapshotValid = false;
bool          g_fireParentSnapshotValid = false;
bool          g_fireAssociationSourceSnapshotValid = false;
bool          g_firePaired150SnapshotValid = false;
bool          g_firePaired20SnapshotValid = false;

struct ManyLight {
    float position[4];
    float color[4];
    u32 up[2];
    u32 look[2];
};
static_assert(sizeof(ManyLight) == kStride, "ManyLightsData layout changed");

const char* StateName(LONG state) {
    switch (state) {
        case Idle:    return g_readback ? "prepared" : "idle";
        case Armed:   return "armed";
        case Copying: return "copying";
        case Issued:  return "copy-issued";
        case Read:    return "read";
        case Failed:  return "failed";
        default:      return "?";
    }
}

const char* FilterStateName(LONG state) {
    if (state == Idle && g_filterReadback) return "prepared";
    switch (state) {
        case Idle:    return "idle";
        case Armed:   return "armed";
        case Copying: return "copying";
        case Issued:  return "copy-issued";
        case Read:    return "read";
        case Failed:  return "failed";
        default:      return "?";
    }
}

const char* EmitterStateName(LONG state) {
    if (state == Idle && g_emitterReadback) return "prepared";
    switch (state) {
        case Idle:    return "idle";
        case Armed:   return "armed";
        case Copying: return "copying";
        case Issued:  return "copy-issued";
        case Read:    return "read";
        case Failed:  return "failed";
        default:      return "?";
    }
}

template <typename T>
bool ReadAt(u64 addr, T* value) {
    return value && mem::SafeRead(reinterpret_cast<const void*>(addr), value, sizeof(T));
}

const char* PoolWatchStateName(LONG state) {
    switch (state) {
        case PoolWatchIdle:      return "idle";
        case PoolWatchArmed:     return "armed";
        case PoolWatchClaimed:   return "claimed";
        case PoolWatchCompleted: return "completed";
        default:                 return "?";
    }
}

void FillWatchCapture(bp::Capture& c, EXCEPTION_POINTERS* info) {
    const CONTEXT* x = info->ContextRecord;
    c.rip = x->Rip; c.rsp = x->Rsp; c.rbp = x->Rbp; c.eflags = x->EFlags;
    c.rax = x->Rax; c.rbx = x->Rbx; c.rcx = x->Rcx; c.rdx = x->Rdx;
    c.rsi = x->Rsi; c.rdi = x->Rdi;
    c.r8 = x->R8; c.r9 = x->R9; c.r10 = x->R10; c.r11 = x->R11;
    c.r12 = x->R12; c.r13 = x->R13; c.r14 = x->R14; c.r15 = x->R15;
    c.threadId = GetCurrentThreadId();
    c.stackReadable = mem::SafeRead(reinterpret_cast<const void*>(x->Rsp),
                                    c.stack, sizeof(c.stack));
    c.hit = true;
}

LONG CALLBACK PoolWriteVeh(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* exception = info ? info->ExceptionRecord : nullptr;
    if (!exception || exception->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
        exception->NumberParameters < 2 || exception->ExceptionInformation[0] != 1)
        return EXCEPTION_CONTINUE_SEARCH;

    const u64 fault = static_cast<u64>(exception->ExceptionInformation[1]);
    if (!g_poolWatchPage || fault < g_poolWatchPage ||
        fault >= g_poolWatchPage + g_poolWatchPageSize)
        return EXCEPTION_CONTINUE_SEARCH;

    const LONG previous = InterlockedCompareExchange(
        &g_poolWatchState, PoolWatchClaimed, PoolWatchArmed);
    if (previous == PoolWatchArmed) {
        g_poolWatchFaultAddress = fault;
        FillWatchCapture(g_poolWatchCapture, info);
        DWORD ignored = 0;
        const bool restored = VirtualProtect(reinterpret_cast<void*>(g_poolWatchPage),
                                             g_poolWatchPageSize,
                                             g_poolWatchOriginalProtect, &ignored) != FALSE;
        if (!restored) g_poolWatchCapture.restoreFailed = true;
        MemoryBarrier();
        InterlockedExchange(&g_poolWatchState, PoolWatchCompleted);
        return restored ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
    }

    // A write can already be in flight on another thread when the winner
    // restores the page. Wait only for that bounded transition, then retry the
    // original instruction against the restored protection.
    if (previous == PoolWatchClaimed) {
        InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(
            &g_poolWatchCapture.lateTraps));
        while (InterlockedCompareExchange(&g_poolWatchState, 0, 0) == PoolWatchClaimed)
            YieldProcessor();
    }
    if (InterlockedCompareExchange(&g_poolWatchState, 0, 0) == PoolWatchCompleted &&
        g_poolWatchCapture.hit && !g_poolWatchCapture.restoreFailed)
        return EXCEPTION_CONTINUE_EXECUTION;
    return EXCEPTION_CONTINUE_SEARCH;
}

bool StopPoolWatch(bool verbose) {
    const LONG state = InterlockedCompareExchange(&g_poolWatchState, 0, 0);
    if (state == PoolWatchClaimed) {
        if (verbose) Out("Pool write capture is completing; try again");
        return false;
    }
    if (state == PoolWatchArmed &&
        InterlockedCompareExchange(&g_poolWatchState, PoolWatchCompleted,
                                   PoolWatchArmed) == PoolWatchArmed) {
        DWORD ignored = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(g_poolWatchPage),
                            g_poolWatchPageSize, g_poolWatchOriginalProtect, &ignored)) {
            InterlockedExchange(&g_poolWatchState, PoolWatchArmed);
            if (verbose) Out("ERROR: could not restore pool page protection (error %lu)",
                             GetLastError());
            return false;
        }
        if (verbose) Out("Pool write capture cancelled; page protection restored");
        return true;
    }
    if (verbose) Out("Pool write capture is not armed");
    return true;
}

void ArmPoolWatch(const char* addressText) {
    if (!g_cfg.allowManyLights) { Out("Refused: [Explorer] AllowManyLights=0"); return; }
    if (!addressText || !*addressText) {
        Out("usage: manylights poolwatch <absolute-address>");
        return;
    }
    const LONG state = InterlockedCompareExchange(&g_poolWatchState, 0, 0);
    if (state == PoolWatchArmed || state == PoolWatchClaimed) {
        Out("Pool write capture is already %s", PoolWatchStateName(state));
        return;
    }

    char* end = nullptr;
    const u64 requested = _strtoui64(addressText, &end, 0);
    if (!requested || !end || *end) {
        Out("Invalid absolute address: %s", addressText);
        return;
    }

    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const SIZE_T pageSize = systemInfo.dwPageSize;
    const u64 page = requested & ~(static_cast<u64>(pageSize) - 1);
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<void*>(page), &mbi, sizeof(mbi)) ||
        mbi.State != MEM_COMMIT) {
        Out("Refused: 0x%llX is not in committed memory", requested);
        return;
    }
    const DWORD baseProtect = mbi.Protect & 0xFF;
    DWORD readProtect = 0;
    if (baseProtect == PAGE_READWRITE) readProtect = PAGE_READONLY;
    else if (baseProtect == PAGE_EXECUTE_READWRITE) readProtect = PAGE_EXECUTE_READ;
    else {
        Out("Refused: page protection 0x%lX is not ordinary writable memory", mbi.Protect);
        return;
    }
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE)) {
        Out("Refused: page has unsupported protection flags 0x%lX", mbi.Protect);
        return;
    }
    if (!g_poolWatchHandler) {
        g_poolWatchHandler = AddVectoredExceptionHandler(1, PoolWriteVeh);
        if (!g_poolWatchHandler) {
            Out("ERROR: AddVectoredExceptionHandler failed (error %lu)", GetLastError());
            return;
        }
    }

    g_poolWatchRequested = requested;
    g_poolWatchPage = page;
    g_poolWatchPageSize = pageSize;
    g_poolWatchOriginalProtect = mbi.Protect;
    g_poolWatchFaultAddress = 0;
    g_poolWatchCapture = bp::Capture();
    InterlockedExchange(&g_poolWatchState, PoolWatchArmed);
    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(page), pageSize, readProtect, &oldProtect)) {
        InterlockedExchange(&g_poolWatchState, PoolWatchIdle);
        Out("ERROR: could not arm pool page 0x%llX (error %lu)", page, GetLastError());
        return;
    }
    Out("Pool write capture armed: requested=0x%llX page=0x%llX size=0x%llX",
        requested, page, static_cast<u64>(pageSize));
    Out("  protection 0x%lX -> 0x%lX; first write restores it immediately", oldProtect,
        readProtect);
}

void PoolWatchStatus() {
    const bp::Capture& c = g_poolWatchCapture;
    Out("=== CPU pool write capture ===");
    Out("  state=%s requested=0x%llX page=0x%llX size=0x%llX protect=0x%lX",
        PoolWatchStateName(g_poolWatchState), g_poolWatchRequested, g_poolWatchPage,
        static_cast<u64>(g_poolWatchPageSize), g_poolWatchOriginalProtect);
    if (!c.hit) { Out("  no write captured"); return; }
    Out("  fault=0x%llX pageOffset=0x%llX thread=%llu%s", g_poolWatchFaultAddress,
        g_poolWatchFaultAddress - g_poolWatchPage, c.threadId,
        c.restoreFailed ? "  ERROR: protection NOT restored" : "");
    Out("  rip 0x%016llX  rsp 0x%016llX  rbp 0x%016llX  fl 0x%llX",
        c.rip, c.rsp, c.rbp, c.eflags);
    Out("  rcx 0x%016llX  rdx 0x%016llX  r8  0x%016llX  r9  0x%016llX",
        c.rcx, c.rdx, c.r8, c.r9);
    Out("  rax 0x%016llX  rbx 0x%016llX  rsi 0x%016llX  rdi 0x%016llX",
        c.rax, c.rbx, c.rsi, c.rdi);
    Out("  r10 0x%016llX  r11 0x%016llX  r12 0x%016llX  r13 0x%016llX",
        c.r10, c.r11, c.r12, c.r13);
    Out("  r14 0x%016llX  r15 0x%016llX", c.r14, c.r15);
    if (c.lateTraps) Out("  %llu other write(s) were already in flight", c.lateTraps);
    if (!c.stackReadable) { Out("  stack was not readable"); return; }
    Out("  stack:");
    for (int i = 0; i < bp::Capture::kStackWords; ++i)
        Out("    [rsp+0x%02X] 0x%016llX", i * 8, c.stack[i]);
}

const char* FireWatchStateName(LONG state) {
    switch (state) {
        case FireWatchIdle:      return "idle";
        case FireWatchArmed:     return "armed";
        case FireWatchClaimed:   return "claimed";
        case FireWatchCompleted: return "completed";
        default:                 return "?";
    }
}

bool PokeFireByte(u64 address, u8 value) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(address), 1,
                        PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    *reinterpret_cast<volatile u8*>(address) = value;
    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void*>(address), 1, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), 1);
    return true;
}

bool IsFireSource(u64 source) {
    float position[3] = {};
    if (!source || !mem::SafeRead(reinterpret_cast<const void*>(source + 0x30),
                                 position, sizeof(position)))
        return false;
    return std::fabs(position[0] - kFireX) <= 0.03f &&
           std::fabs(position[1] - kFireY) <= 0.03f &&
           std::fabs(position[2] - kFireZ) <= 0.03f;
}

bool IsFirePositionAt(u64 address) {
    float position[3] = {};
    if (!address || !mem::SafeRead(reinterpret_cast<const void*>(address),
                                   position, sizeof(position)))
        return false;
    return std::fabs(position[0] - kFireX) <= 0.03f &&
           std::fabs(position[1] - kFireY) <= 0.03f &&
           std::fabs(position[2] - kFireZ) <= 0.03f;
}

bool IsFireWorkItem(u64 workItem, u64* upstreamOut) {
    u64 upstream = 0;
    if (!workItem || !ReadAt(workItem, &upstream) || !upstream) return false;
    if (upstreamOut) *upstreamOut = upstream;
    return IsFirePositionAt(upstream + 0xBC) ||
           IsFirePositionAt(upstream + 0xFC);
}

bool IsFireParent(u64 parent, u64* childOut) {
    u64 child = 0;
    if (!parent || !ReadAt(parent + 0x250, &child) || !child) return false;
    if (childOut) *childOut = child;
    return IsFirePositionAt(child + 0xBC) ||
           IsFirePositionAt(child + 0xFC);
}

bool EmulateFireTransfer(CONTEXT* context) {
    if (!context || !g_fireWatchCallTarget) return false;
    if (g_fireWatchOriginalByte == 0xE9) {
        context->Rip = g_fireWatchCallTarget;
        return true;
    }
    const u64 returnAddress = g_fireWatchTarget + 5;
    const u64 newRsp = context->Rsp - sizeof(returnAddress);
    __try {
        *reinterpret_cast<volatile u64*>(newRsp) = returnAddress;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    context->Rsp = newRsp;
    context->Rip = g_fireWatchCallTarget;
    return true;
}

LONG CALLBACK FireWatchVeh(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* exception = info ? info->ExceptionRecord : nullptr;
    if (!exception) return EXCEPTION_CONTINUE_SEARCH;
    if (exception->ExceptionCode != EXCEPTION_BREAKPOINT ||
        reinterpret_cast<u64>(exception->ExceptionAddress) != g_fireWatchTarget)
        return EXCEPTION_CONTINUE_SEARCH;

    const LONG state = InterlockedCompareExchange(&g_fireWatchState, 0, 0);
    u64 matchedUpstream = 0;
    u64 matchedChild = 0;
    bool matches = false;
    if (state == FireWatchArmed) {
        if (g_fireWatchKind == FireWatchOwner)
            matches = IsFireWorkItem(info->ContextRecord->Rdx, &matchedUpstream);
        else if (g_fireWatchKind == FireWatchSource)
            matches = IsFireParent(info->ContextRecord->Rcx, &matchedChild);
        else
            matches = IsFireSource(info->ContextRecord->R9);
    }
    if (state == FireWatchArmed && matches) {
        if (InterlockedCompareExchange(&g_fireWatchState, FireWatchClaimed,
                                       FireWatchArmed) == FireWatchArmed) {
            const bool restored = PokeFireByte(g_fireWatchTarget,
                                               g_fireWatchOriginalByte);
            FillWatchCapture(g_fireWatchCapture, info);
            g_fireWatchCapture.rip = g_fireWatchTarget;
            if (g_fireWatchKind == FireWatchOwner) {
                g_fireWatchOwner = info->ContextRecord->Rdi;
                g_fireWatchWorkItem = info->ContextRecord->Rdx;
                g_fireWatchUpstream = matchedUpstream;
            } else if (g_fireWatchKind == FireWatchSource) {
                g_fireWatchParent = info->ContextRecord->Rcx;
                g_fireWatchOwner = matchedChild;
                g_fireWatchAssociationSource = info->ContextRecord->Rdx;
            } else {
                g_fireWatchWorkItem = info->ContextRecord->Rdi;
                g_fireWatchSourceRecord = info->ContextRecord->R9;
                g_fireWatchRecordIndex = static_cast<u32>(info->ContextRecord->R8) /
                                         kEmitterStride;
                ReadAt(info->ContextRecord->Rsp + 0x58, &g_fireWatchUpstream);
                // The builder has already completed the two sister records at
                // this callsite. Their pointers remain in its verified frame.
                ReadAt(info->ContextRecord->Rsp + 0x1C8, &g_fireWatchPaired150);
                ReadAt(info->ContextRecord->Rsp + 0x1B0, &g_fireWatchPaired20);
            }
            g_fireSourceSnapshotValid = mem::SafeRead(
                reinterpret_cast<const void*>(g_fireWatchSourceRecord),
                g_fireSourceSnapshot, sizeof(g_fireSourceSnapshot));
            g_fireWorkItemSnapshotValid = mem::SafeRead(
                reinterpret_cast<const void*>(g_fireWatchWorkItem),
                g_fireWorkItemSnapshot, sizeof(g_fireWorkItemSnapshot));
            g_fireUpstreamSnapshotValid = g_fireWatchUpstream && mem::SafeRead(
                reinterpret_cast<const void*>(g_fireWatchUpstream),
                g_fireUpstreamSnapshot, sizeof(g_fireUpstreamSnapshot));
            g_fireOwnerSnapshotValid = g_fireWatchOwner && mem::SafeRead(
                reinterpret_cast<const void*>(g_fireWatchOwner),
                g_fireOwnerSnapshot, sizeof(g_fireOwnerSnapshot));
            g_fireParentSnapshotValid = g_fireWatchParent && mem::SafeRead(
                reinterpret_cast<const void*>(g_fireWatchParent),
                g_fireParentSnapshot, sizeof(g_fireParentSnapshot));
            g_fireAssociationSourceSnapshotValid = g_fireWatchAssociationSource &&
                mem::SafeRead(reinterpret_cast<const void*>(g_fireWatchAssociationSource),
                              g_fireAssociationSourceSnapshot,
                              sizeof(g_fireAssociationSourceSnapshot));
            g_firePaired150SnapshotValid = g_fireWatchPaired150 && mem::SafeRead(
                reinterpret_cast<const void*>(g_fireWatchPaired150),
                g_firePaired150Snapshot, sizeof(g_firePaired150Snapshot));
            g_firePaired20SnapshotValid = g_fireWatchPaired20 && mem::SafeRead(
                reinterpret_cast<const void*>(g_fireWatchPaired20),
                g_firePaired20Snapshot, sizeof(g_firePaired20Snapshot));
            if (g_fireSourceSnapshotValid)
                memcpy(&g_fireWatchSourceGroupId, g_fireSourceSnapshot + 0xD8,
                       sizeof(g_fireWatchSourceGroupId));
            if (g_fireUpstreamSnapshotValid)
                memcpy(&g_fireWatchGroupId, g_fireUpstreamSnapshot + 0x1E8,
                       sizeof(g_fireWatchGroupId));
            if (!restored) g_fireWatchCapture.restoreFailed = true;
            MemoryBarrier();
            InterlockedExchange(&g_fireWatchState, FireWatchCompleted);
            info->ContextRecord->Rip = g_fireWatchTarget;
            return restored ? EXCEPTION_CONTINUE_EXECUTION
                            : EXCEPTION_CONTINUE_SEARCH;
        }
    }

    // Non-matching emitter: emulate the displaced rel32 CALL while leaving the
    // INT3 installed. The previous restore/single-step/reinsert sequence opened
    // a real multithreaded window in which the six wanted calls could pass
    // unobserved among hundreds of thousands of other emitter calls.
    if (state == FireWatchArmed) {
        InterlockedIncrement(&g_fireWatchRejected);
        if (!EmulateFireTransfer(info->ContextRecord)) {
            PokeFireByte(g_fireWatchTarget, g_fireWatchOriginalByte);
            g_fireWatchCapture.restoreFailed = true;
            InterlockedExchange(&g_fireWatchState, FireWatchCompleted);
            return EXCEPTION_CONTINUE_SEARCH;
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    // A second thread may already have fetched the INT3 while the winner is
    // restoring it. Rewind; it then sees either the restored call byte or a
    // bounded re-trap until the state transition completes.
    const LONG current = InterlockedCompareExchange(&g_fireWatchState, 0, 0);
    if (current == FireWatchClaimed ||
        (current == FireWatchCompleted && !g_fireWatchCapture.restoreFailed)) {
        info->ContextRecord->Rip = g_fireWatchTarget;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

bool StopFireWatch(bool verbose) {
    const LONG state = InterlockedCompareExchange(&g_fireWatchState, 0, 0);
    if (state == FireWatchClaimed) {
        if (verbose) Out("Fire writer capture is completing; try again");
        return false;
    }
    if (state == FireWatchArmed &&
        InterlockedCompareExchange(&g_fireWatchState, FireWatchCompleted,
                                   FireWatchArmed) == FireWatchArmed) {
        if (!PokeFireByte(g_fireWatchTarget, g_fireWatchOriginalByte)) {
            InterlockedExchange(&g_fireWatchState, FireWatchArmed);
            if (verbose) Out("ERROR: could not restore fire-watch instruction");
            return false;
        }
        if (verbose) Out("Fire writer capture cancelled; instruction restored");
        return true;
    }
    if (verbose) Out("Fire writer capture is not armed");
    return true;
}

void ArmFireWatch(const char* targetText, FireWatchKind kind) {
    if (!g_cfg.allowManyLights || !g_cfg.allowBreakpoints) {
        Out("Refused: AllowManyLights and AllowBreakpoints must both be 1");
        return;
    }
    const LONG state = InterlockedCompareExchange(&g_fireWatchState, 0, 0);
    if (state == FireWatchArmed || state == FireWatchClaimed) {
        Out("Fire writer capture is already %s", FireWatchStateName(state));
        return;
    }
    if (!g_game.moduleBase || !g_game.moduleSize) {
        Out("Refused: game module range is unavailable");
        return;
    }

    const bool customTarget = targetText && *targetText;
    const u64 defaultRva = kind == FireWatchOwner ? kFireOwnerCallRva :
                           kind == FireWatchSource ? kFireSourceSetterRva :
                           kFireAppendCallRva;
    u64 target = g_game.moduleBase + defaultRva;
    if (customTarget) {
        char* end = nullptr;
        const u64 value = _strtoui64(targetText, &end, 0);
        if (!value || !end || *end) { Out("Invalid firewatch target: %s", targetText); return; }
        if (value < g_game.moduleSize) target = g_game.moduleBase + value;
        else if (value >= g_game.moduleBase &&
                 value < g_game.moduleBase + g_game.moduleSize) target = value;
        else { Out("Firewatch target is outside the main module: 0x%llX", value); return; }
    }

    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<void*>(target), &mbi, sizeof(mbi)) ||
        mbi.State != MEM_COMMIT ||
        ((mbi.Protect & 0xFF) != PAGE_EXECUTE &&
         (mbi.Protect & 0xFF) != PAGE_EXECUTE_READ &&
         (mbi.Protect & 0xFF) != PAGE_EXECUTE_READWRITE &&
         (mbi.Protect & 0xFF) != PAGE_EXECUTE_WRITECOPY)) {
        Out("Refused: firewatch target is not executable");
        return;
    }
    u8 instruction[5] = {};
    if (!mem::SafeRead(reinterpret_cast<const void*>(target), instruction,
                       sizeof(instruction)) || instruction[0] == 0xCC) {
        Out("Refused: firewatch target is unreadable or already INT3");
        return;
    }
    if (instruction[0] != 0xE8 &&
        !(kind == FireWatchSource && instruction[0] == 0xE9)) {
        Out("Refused: firewatch target is not a supported rel32 transfer");
        return;
    }
    i32 displacement = 0;
    memcpy(&displacement, instruction + 1, sizeof(displacement));
    const u64 callTarget = target + sizeof(instruction) + displacement;
    if (callTarget < g_game.moduleBase ||
        callTarget >= g_game.moduleBase + g_game.moduleSize) {
        Out("Refused: firewatch CALL target 0x%llX is outside the main module", callTarget);
        return;
    }
    if (!g_fireWatchHandler) {
        g_fireWatchHandler = AddVectoredExceptionHandler(1, FireWatchVeh);
        if (!g_fireWatchHandler) {
            Out("ERROR: AddVectoredExceptionHandler failed (error %lu)", GetLastError());
            return;
        }
    }

    g_fireWatchTarget = target;
    g_fireWatchKind = kind;
    g_fireWatchCallTarget = callTarget;
    g_fireWatchOriginalByte = instruction[0];
    g_fireWatchCapture = bp::Capture();
    g_fireWatchWorkItem = g_fireWatchSourceRecord = g_fireWatchUpstream =
        g_fireWatchOwner = 0;
    g_fireWatchParent = g_fireWatchAssociationSource = 0;
    g_fireWatchPaired150 = g_fireWatchPaired20 = 0;
    g_fireWatchRecordIndex = g_fireWatchGroupId = g_fireWatchSourceGroupId = 0;
    memset(g_fireSourceSnapshot, 0, sizeof(g_fireSourceSnapshot));
    memset(g_fireWorkItemSnapshot, 0, sizeof(g_fireWorkItemSnapshot));
    memset(g_fireUpstreamSnapshot, 0, sizeof(g_fireUpstreamSnapshot));
    memset(g_fireOwnerSnapshot, 0, sizeof(g_fireOwnerSnapshot));
    memset(g_fireParentSnapshot, 0, sizeof(g_fireParentSnapshot));
    memset(g_fireAssociationSourceSnapshot, 0,
           sizeof(g_fireAssociationSourceSnapshot));
    memset(g_firePaired150Snapshot, 0, sizeof(g_firePaired150Snapshot));
    memset(g_firePaired20Snapshot, 0, sizeof(g_firePaired20Snapshot));
    g_fireSourceSnapshotValid = g_fireWorkItemSnapshotValid =
        g_fireUpstreamSnapshotValid = g_fireOwnerSnapshotValid = false;
    g_fireParentSnapshotValid = g_fireAssociationSourceSnapshotValid = false;
    g_firePaired150SnapshotValid = g_firePaired20SnapshotValid = false;
    InterlockedExchange(&g_fireWatchRejected, 0);
    InterlockedExchange(&g_fireWatchState, FireWatchArmed);
    if (!PokeFireByte(target, 0xCC)) {
        InterlockedExchange(&g_fireWatchState, FireWatchIdle);
        Out("ERROR: could not arm fire writer capture at 0x%llX", target);
        return;
    }
    const char* kindName = kind == FireWatchOwner ? "owner" :
                           kind == FireWatchSource ? "source" : "writer";
    Out("Fire %s capture armed at VA 0x%llX (RVA 0x%llX), target 0x%llX",
        kindName, target,
        target - g_game.moduleBase, callTarget);
    if (kind == FireWatchOwner)
        Out("  filter [workItem]+0xBC/0xFC = (%.3f, %.3f, %.3f) +/- 0.03",
            kFireX, kFireY, kFireZ);
    else if (kind == FireWatchSource)
        Out("  filter [parent+0x250]+0xBC/0xFC = (%.3f, %.3f, %.3f) +/- 0.03",
            kFireX, kFireY, kFireZ);
    else
        Out("  filter source+0x30 = (%.3f, %.3f, %.3f) +/- 0.03",
            kFireX, kFireY, kFireZ);
}

void FireWatchStatus() {
    const bp::Capture& c = g_fireWatchCapture;
    const char* kindName = g_fireWatchKind == FireWatchOwner ? "owner" :
                           g_fireWatchKind == FireWatchSource ? "source" : "builder";
    Out("=== fire-specific %s capture ===",
        g_fireWatchKind == FireWatchOwner ? "owner/WorkItem" :
        g_fireWatchKind == FireWatchSource ? "parent/source association" :
        "CPU record builder");
    Out("  kind=%s state=%s target=0x%llX rejected=%ld",
        kindName,
        FireWatchStateName(g_fireWatchState), g_fireWatchTarget, g_fireWatchRejected);
    if (!c.hit) {
        Out(g_fireWatchKind == FireWatchOwner ? "  no matching fire owner captured" :
            g_fireWatchKind == FireWatchSource ? "  no matching fire source captured" :
            "  no matching fire record captured");
        return;
    }
    if (g_fireWatchKind == FireWatchOwner) {
        Out("  owner=0x%llX workItem=0x%llX upstream=0x%llX upstream+1E8=0x%08X",
            g_fireWatchOwner, g_fireWatchWorkItem, g_fireWatchUpstream,
            g_fireWatchGroupId);
        Out("  snapshots owner=%d workItem=%d upstream=%d",
            g_fireOwnerSnapshotValid ? 1 : 0, g_fireWorkItemSnapshotValid ? 1 : 0,
            g_fireUpstreamSnapshotValid ? 1 : 0);
    } else if (g_fireWatchKind == FireWatchSource) {
        Out("  parent=0x%llX child=0x%llX source=0x%llX",
            g_fireWatchParent, g_fireWatchOwner, g_fireWatchAssociationSource);
        Out("  snapshots parent=%d child=%d source=%d",
            g_fireParentSnapshotValid ? 1 : 0, g_fireOwnerSnapshotValid ? 1 : 0,
            g_fireAssociationSourceSnapshotValid ? 1 : 0);
    } else {
        Out("  recordIndex=%u workItem=0x%llX sourceRecord=0x%llX upstream=0x%llX",
            g_fireWatchRecordIndex, g_fireWatchWorkItem, g_fireWatchSourceRecord,
            g_fireWatchUpstream);
        Out("  source+D8=0x%08X upstream+1E8=0x%08X%s", g_fireWatchSourceGroupId,
            g_fireWatchGroupId, g_fireWatchSourceGroupId == g_fireWatchGroupId
                ? " MATCH" : " MISMATCH");
        Out("  paired150=0x%llX paired20=0x%llX",
            g_fireWatchPaired150, g_fireWatchPaired20);
        Out("  snapshots source=%d workItem=%d upstream=%d paired150=%d paired20=%d",
            g_fireSourceSnapshotValid ? 1 : 0, g_fireWorkItemSnapshotValid ? 1 : 0,
            g_fireUpstreamSnapshotValid ? 1 : 0,
            g_firePaired150SnapshotValid ? 1 : 0,
            g_firePaired20SnapshotValid ? 1 : 0);
    }
    Out("  thread=%llu rip=0x%016llX rsp=0x%016llX%s", c.threadId, c.rip, c.rsp,
        c.restoreFailed ? " ERROR: instruction NOT restored" : "");
    Out("  rcx 0x%016llX  rdx 0x%016llX  rdi 0x%016llX  r8 0x%016llX",
        c.rcx, c.rdx, c.rdi, c.r8);
    Out("  r9  0x%016llX  r13 0x%016llX  r14 0x%016llX  r15 0x%016llX",
        c.r9, c.r13, c.r14, c.r15);
    if (!c.stackReadable) { Out("  stack was not readable"); return; }
    Out("  stack:");
    for (int i = 0; i < bp::Capture::kStackWords; ++i)
        Out("    [rsp+0x%02X] 0x%016llX", i * 8, c.stack[i]);
}

void ReleaseResources() {
    if (g_counterReadback) { g_counterReadback->Release(); g_counterReadback = nullptr; }
    if (g_counterSource)   { g_counterSource->Release();   g_counterSource = nullptr; }
    if (g_readback) { g_readback->Release(); g_readback = nullptr; }
    if (g_source)   { g_source->Release();   g_source = nullptr; }
    g_outer = g_inner = g_counterOuter = g_counterInner = 0;
    g_counterOuterUsed = g_counterInnerUsed = g_counterSourceUsed = 0;
    g_commandList = 0;
    g_issuedAt = 0;
    InterlockedExchange(&g_error, 0);
    InterlockedExchange(&g_state, Idle);
}

void ReleaseFilterResources() {
    if (g_filterReadback) { g_filterReadback->Release(); g_filterReadback = nullptr; }
    if (g_filterSource)   { g_filterSource->Release();   g_filterSource = nullptr; }
    g_filterOuter = g_filterInner = 0;
    g_filterOuterUsed = g_filterInnerUsed = g_filterSourceUsed = 0;
    g_filterCommandList = 0;
    g_filterIssuedAt = 0;
    InterlockedExchange(&g_filterError, 0);
    InterlockedExchange(&g_filterState, Idle);
}

void ReleaseEmitterResources() {
    if (g_emitterReadback) { g_emitterReadback->Release(); g_emitterReadback = nullptr; }
    if (g_emitterSource)   { g_emitterSource->Release();   g_emitterSource = nullptr; }
    g_emitterOuter = g_emitterInner = g_emitterCommandList = 0;
    g_emitterByteSize = 0;
    g_emitterStride = g_emitterCount = 0;
    g_emitterSimulation = false;
    g_emitterHeapType = D3D12_HEAP_TYPE_DEFAULT;
    g_emitterIssuedAt = 0;
    InterlockedExchange(&g_emitterError, 0);
    InterlockedExchange(&g_emitterState, Idle);
}

bool SourceFromOuter(u64 outer, u64* innerOut, ID3D12Resource** sourceOut) {
    u64 inner = 0, source = 0;
    if (!ReadAt(outer + 0x30, &inner) || !inner) return false;
    if (!ReadAt(inner + 0x168, &source) || !source) return false;
    if (innerOut) *innerOut = inner;
    if (sourceOut) *sourceOut = reinterpret_cast<ID3D12Resource*>(source);
    return true;
}

void RecordWriterHit(LONG method, void* destination, u32 destinationOffset,
                     void* source, u32 sourceOffset, u32 byteCount, u64 caller) {
    const u64 destinationAddress = reinterpret_cast<u64>(destination);
    const bool matches = method == 0x658
        ? destinationAddress == g_writerTargetInner
        : destinationAddress == g_writerTargetOuter;
    if (!matches || InterlockedCompareExchange(&g_writerArmed, 0, 0) == 0) return;

    const LONG hit = InterlockedIncrement(&g_writerHits);
    if (hit != 1) return;

    u64 sourceInner = 0, sourceResource = 0;
    const u64 sourceAddress = reinterpret_cast<u64>(source);
    if (sourceAddress && ReadAt(sourceAddress + 0x30, &sourceInner) && sourceInner) {
        // +0x650 copies from another ordinary BufferD3D12 resource. The two
        // upload variants use the staging resource held at inner +0xD8.
        const u64 resourceOffset = method == 0x650 ? 0x168 : 0xD8;
        ReadAt(sourceInner + resourceOffset, &sourceResource);
    }

    g_writerDestination = destinationAddress;
    g_writerSource = sourceAddress;
    g_writerSourceInner = sourceInner;
    g_writerSourceResource = sourceResource;
    g_writerCaller = caller;
    g_writerDestinationOffset = destinationOffset;
    g_writerSourceOffset = sourceOffset;
    g_writerByteCount = byteCount;
    g_writerThread = GetCurrentThreadId();
    g_writerSeenAt = GetTickCount64();
    MemoryBarrier();
    InterlockedExchange(&g_writerMethod, method);
}

void HookCopyOuterUpload(void* self, void* destination, u32 destinationOffset,
                         void* source, u32 sourceOffset, u32 byteCount) {
    RecordWriterHit(0x648, destination, destinationOffset, source, sourceOffset, byteCount,
                    reinterpret_cast<u64>(_ReturnAddress()));
    g_writerOriginalOuterUpload(self, destination, destinationOffset, source, sourceOffset,
                                byteCount);
}

void HookCopyOuterCopy(void* self, void* destination, u32 destinationOffset,
                       void* source, u32 sourceOffset, u32 byteCount) {
    RecordWriterHit(0x650, destination, destinationOffset, source, sourceOffset, byteCount,
                    reinterpret_cast<u64>(_ReturnAddress()));
    g_writerOriginalOuterCopy(self, destination, destinationOffset, source, sourceOffset,
                              byteCount);
}

void HookCopyInnerUpload(void* self, void* destination, u32 destinationOffset,
                         void* source, u32 sourceOffset, u32 byteCount) {
    RecordWriterHit(0x658, destination, destinationOffset, source, sourceOffset, byteCount,
                    reinterpret_cast<u64>(_ReturnAddress()));
    g_writerOriginalInnerUpload(self, destination, destinationOffset, source, sourceOffset,
                                byteCount);
}

bool StopWriter(bool verbose) {
    if (!g_writerCommandObject || !g_writerCloneVtable || !g_writerOriginalVtable) {
        if (verbose) Out("Emitter writer probe is not installed");
        InterlockedExchange(&g_writerArmed, 0);
        return true;
    }

    void* volatile* objectVtable = reinterpret_cast<void* volatile*>(g_writerCommandObject);
    void* previous = InterlockedCompareExchangePointer(
        objectVtable, g_writerOriginalVtable, g_writerCloneVtable);
    if (previous != g_writerCloneVtable && previous != g_writerOriginalVtable) {
        if (verbose) Out("ERROR: command-object vtable changed; probe did not overwrite it");
        return false;
    }
    InterlockedExchange(&g_writerArmed, 0);
    if (verbose) Out("Emitter writer probe stopped; observed %ld matching copies", g_writerHits);
    return true;
}

void ArmWriter() {
    if (!g_cfg.allowManyLights) { Out("Refused: [Explorer] AllowManyLights=0"); return; }
    if (!g_emitterSource || !g_emitterOuter || !g_emitterInner) {
        Out("Run `manylights emitterprepare` or `manylights simprepare` first");
        return;
    }
    if (InterlockedCompareExchange(&g_writerArmed, 0, 0) != 0) {
        Out("Emitter writer probe is already armed");
        return;
    }

    const bp::Capture* capture = bp::UserCapture();
    const u64 ripRva = capture->rip >= g_game.moduleBase
        ? capture->rip - g_game.moduleBase : 0;
    if (!capture->hit || !capture->rcx ||
        (ripRva != kCopyOuterUploadRva && ripRva != kCopyOuterCopyRva &&
         ripRva != kCopyInnerUploadRva)) {
        Out("Need a completed `bpcapture 0x%llX` first", kCopyOuterCopyRva);
        return;
    }

    void** originalVtable = nullptr;
    if (!ReadAt(capture->rcx, &originalVtable) || !originalVtable) {
        Out("ERROR: captured command object has no readable vtable");
        return;
    }
    u64 outerUpload = 0, outerCopy = 0, innerUpload = 0;
    if (!ReadAt(reinterpret_cast<u64>(originalVtable) + 0x648, &outerUpload) ||
        !ReadAt(reinterpret_cast<u64>(originalVtable) + 0x650, &outerCopy) ||
        !ReadAt(reinterpret_cast<u64>(originalVtable) + 0x658, &innerUpload) ||
        outerUpload != g_game.moduleBase + kCopyOuterUploadRva ||
        outerCopy != g_game.moduleBase + kCopyOuterCopyRva ||
        innerUpload != g_game.moduleBase + kCopyInnerUploadRva) {
        Out("ERROR: command-object copy-wrapper signatures do not match this build");
        return;
    }

    const size_t tableBytes = kCommandVtableEntries * sizeof(void*);
    void** clone = reinterpret_cast<void**>(VirtualAlloc(
        nullptr, tableBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!clone || !mem::SafeRead(originalVtable, clone, tableBytes)) {
        if (clone) VirtualFree(clone, 0, MEM_RELEASE);
        Out("ERROR: could not clone command-object vtable");
        return;
    }

    g_writerCommandObject = capture->rcx;
    g_writerOriginalVtable = originalVtable;
    g_writerCloneVtable = clone;
    g_writerOriginalOuterUpload = reinterpret_cast<EngineCopyFn>(clone[kCopyOuterUploadIndex]);
    g_writerOriginalOuterCopy = reinterpret_cast<EngineCopyFn>(clone[kCopyOuterCopyIndex]);
    g_writerOriginalInnerUpload = reinterpret_cast<EngineCopyFn>(clone[kCopyInnerUploadIndex]);
    g_writerTargetOuter = g_emitterOuter;
    g_writerTargetInner = g_emitterInner;
    g_writerTargetResource = reinterpret_cast<u64>(g_emitterSource);
    g_writerDestination = g_writerSource = g_writerSourceInner = g_writerSourceResource = 0;
    g_writerCaller = 0;
    g_writerDestinationOffset = g_writerSourceOffset = g_writerByteCount = g_writerThread = 0;
    g_writerSeenAt = 0;
    InterlockedExchange(&g_writerHits, 0);
    InterlockedExchange(&g_writerMethod, 0);

    clone[kCopyOuterUploadIndex] = reinterpret_cast<void*>(&HookCopyOuterUpload);
    clone[kCopyOuterCopyIndex] = reinterpret_cast<void*>(&HookCopyOuterCopy);
    clone[kCopyInnerUploadIndex] = reinterpret_cast<void*>(&HookCopyInnerUpload);
    InterlockedExchange(&g_writerArmed, 1);
    void* previous = InterlockedCompareExchangePointer(
        reinterpret_cast<void* volatile*>(g_writerCommandObject), clone, originalVtable);
    if (previous != originalVtable) {
        InterlockedExchange(&g_writerArmed, 0);
        Out("ERROR: command-object vtable changed before probe installation");
        return;
    }

    Out("Armed emitter writer probe on command object 0x%llX", g_writerCommandObject);
    Out("  target outer=0x%llX inner=0x%llX resource=0x%llX",
        g_writerTargetOuter, g_writerTargetInner, g_writerTargetResource);
    Out("  only exact target writes through wrappers +0x648/+0x650/+0x658 are recorded");
}

bool ValidateTarget(ID3D12Resource* source, u64 expectedSize, bool exactSize,
                    D3D12_RESOURCE_DESC* descOut) {
    if (!source || !descOut) return false;
    __try {
        *descOut = source->GetDesc();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&g_error, static_cast<LONG>(GetExceptionCode()));
        return false;
    }
    const bool sizeMatches = exactSize ? descOut->Width == expectedSize
                                       : descOut->Width >= expectedSize;
    return descOut->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
           sizeMatches &&
           (descOut->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0;
}

bool ValidateEmitterTarget(ID3D12Resource* source, u64 expectedSize,
                           D3D12_RESOURCE_DESC* descOut) {
    if (!source || !descOut) return false;
    __try {
        *descOut = source->GetDesc();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&g_emitterError, static_cast<LONG>(GetExceptionCode()));
        return false;
    }
    return descOut->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
           descOut->Width >= expectedSize;
}

void CopyAtProducer(EXCEPTION_POINTERS* info) {
    if (InterlockedCompareExchange(&g_state, Copying, Armed) != Armed) return;

    const CONTEXT* context = info ? info->ContextRecord : nullptr;
    u64 currentOuter = 0, currentCounterOuter = 0, holder = 0, commandList = 0;
    u64 currentInner = 0, currentCounterInner = 0;
    u32 currentCounterStride = 0, currentCounterCount = 0;
    ID3D12Resource* currentSource = nullptr;
    ID3D12Resource* currentCounterSource = nullptr;
    if (!context ||
        !ReadAt(context->Rsp + 0x58, &currentOuter) || currentOuter != g_outer ||
        !SourceFromOuter(currentOuter, &currentInner, &currentSource) ||
        currentInner != g_inner || currentSource != g_source ||
        !ReadAt(context->Rsp + 0x50, &currentCounterOuter) || !currentCounterOuter ||
        !SourceFromOuter(currentCounterOuter, &currentCounterInner, &currentCounterSource) ||
        !ReadAt(currentCounterInner + 0xC0, &currentCounterStride) ||
        !ReadAt(currentCounterInner + 0xC4, &currentCounterCount) ||
        currentCounterStride != kCounterStride || currentCounterCount != kCounterCount ||
        !ReadAt(context->Rdi + 0x800, &holder) || !holder ||
        !ReadAt(holder + 8, &commandList) || !commandList) {
        InterlockedExchange(&g_error, ERROR_INVALID_DATA);
        InterlockedExchange(&g_state, Failed);
        return;
    }

    D3D12_RESOURCE_DESC currentCounterDesc = {};
    if (!ValidateTarget(currentCounterSource, kCounterByteSize, false, &currentCounterDesc)) {
        if (!g_error) InterlockedExchange(&g_error, ERROR_INVALID_DATA);
        InterlockedExchange(&g_state, Failed);
        return;
    }

    auto* list = reinterpret_cast<ID3D12GraphicsCommandList*>(commandList);
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    for (u32 i = 0; i < 2; ++i) {
        barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    }
    barriers[0].Transition.pResource = g_source;
    barriers[1].Transition.pResource = currentCounterSource;

    __try {
        list->ResourceBarrier(2, barriers);
        list->CopyBufferRegion(g_readback, 0, g_source, 0, kByteSize);
        list->CopyBufferRegion(g_counterReadback, 0, currentCounterSource, 0, kCounterByteSize);
        for (u32 i = 0; i < 2; ++i) {
            barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            barriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        list->ResourceBarrier(2, barriers);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&g_error, static_cast<LONG>(GetExceptionCode()));
        InterlockedExchange(&g_state, Failed);
        return;
    }

    g_commandList = commandList;
    g_counterOuterUsed = currentCounterOuter;
    g_counterInnerUsed = currentCounterInner;
    g_counterSourceUsed = reinterpret_cast<u64>(currentCounterSource);
    g_issuedAt = GetTickCount64();
    InterlockedExchange(&g_state, Issued);
}

void CopyAtFilter(EXCEPTION_POINTERS* info) {
    if (InterlockedCompareExchange(&g_filterState, Copying, Armed) != Armed) return;

    const CONTEXT* context = info ? info->ContextRecord : nullptr;
    u64 currentOuter = 0, currentInner = 0, holder = 0, commandList = 0;
    u32 currentStride = 0, currentCount = 0;
    ID3D12Resource* currentSource = nullptr;
    if (!context || !(currentOuter = context->R12) ||
        !SourceFromOuter(currentOuter, &currentInner, &currentSource) ||
        !ReadAt(currentInner + 0xC0, &currentStride) ||
        !ReadAt(currentInner + 0xC4, &currentCount) ||
        currentStride != kStride || currentCount != kCount ||
        !ReadAt(context->Rbx + 0x800, &holder) || !holder ||
        !ReadAt(holder + 8, &commandList) || !commandList) {
        InterlockedExchange(&g_filterError, ERROR_INVALID_DATA);
        InterlockedExchange(&g_filterState, Failed);
        return;
    }

    D3D12_RESOURCE_DESC currentDesc = {};
    if (!ValidateTarget(currentSource, kByteSize, true, &currentDesc)) {
        if (!g_filterError) InterlockedExchange(&g_filterError, ERROR_INVALID_DATA);
        InterlockedExchange(&g_filterState, Failed);
        return;
    }

    auto* list = reinterpret_cast<ID3D12GraphicsCommandList*>(commandList);
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = currentSource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;

    __try {
        list->ResourceBarrier(1, &barrier);
        list->CopyBufferRegion(g_filterReadback, 0, currentSource, 0, kByteSize);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        list->ResourceBarrier(1, &barrier);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&g_filterError, static_cast<LONG>(GetExceptionCode()));
        InterlockedExchange(&g_filterState, Failed);
        return;
    }

    g_filterOuterUsed = currentOuter;
    g_filterInnerUsed = currentInner;
    g_filterSourceUsed = reinterpret_cast<u64>(currentSource);
    g_filterCommandList = commandList;
    g_filterIssuedAt = GetTickCount64();
    InterlockedExchange(&g_filterState, Issued);
}

void CopyEmitterInput(EXCEPTION_POINTERS* info) {
    if (InterlockedCompareExchange(&g_emitterState, Copying, Armed) != Armed) return;

    const CONTEXT* context = info ? info->ContextRecord : nullptr;
    u64 holder = 0, commandList = 0;
    if (!context || !g_emitterSource || !g_emitterReadback ||
        !ReadAt(context->Rdi + 0x800, &holder) || !holder ||
        !ReadAt(holder + 8, &commandList) || !commandList) {
        InterlockedExchange(&g_emitterError, ERROR_INVALID_DATA);
        InterlockedExchange(&g_emitterState, Failed);
        return;
    }

    auto* list = reinterpret_cast<ID3D12GraphicsCommandList*>(commandList);
    D3D12_RESOURCE_BARRIER barrier = {};
    const bool needsBarrier = g_emitterHeapType == D3D12_HEAP_TYPE_DEFAULT;
    if (needsBarrier) {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = g_emitterSource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    }

    __try {
        if (needsBarrier) list->ResourceBarrier(1, &barrier);
        list->CopyBufferRegion(g_emitterReadback, 0, g_emitterSource, 0, g_emitterByteSize);
        if (needsBarrier) {
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            list->ResourceBarrier(1, &barrier);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&g_emitterError, static_cast<LONG>(GetExceptionCode()));
        InterlockedExchange(&g_emitterState, Failed);
        return;
    }

    g_emitterCommandList = commandList;
    g_emitterIssuedAt = GetTickCount64();
    InterlockedExchange(&g_emitterState, Issued);
}

void Prepare() {
    if (!g_cfg.allowManyLights) {
        Out("Refused: [Explorer] AllowManyLights=0");
        return;
    }
    if (g_state == Armed || g_state == Copying) {
        Out("Refused: a ManyLights capture is currently %s", StateName(g_state));
        return;
    }

    const bp::Capture* capture = bp::UserCapture();
    const u64 expectedRip = g_game.moduleBase + kBindingReturnRva;
    if (!capture->hit || !capture->stackReadable || capture->rip != expectedRip) {
        Out("Need a completed `bpcapture 0x%llX` first; current capture RIP is 0x%llX",
            kBindingReturnRva, capture->rip);
        return;
    }

    const u64 outer = capture->stack[0x58 / 8];
    const u64 counterOuter = capture->stack[0x50 / 8];
    u64 bindingWrapper = 0;
    u32 bindingStride = 0, innerStride = 0, innerCount = 0;
    u32 counterStride = 0, counterCount = 0;
    u64 inner = 0, counterInner = 0;
    ID3D12Resource* source = nullptr;
    ID3D12Resource* counterSource = nullptr;
    if (!ReadAt(capture->rax + 8, &bindingWrapper) || !bindingWrapper ||
        !ReadAt(bindingWrapper + 0x2C, &bindingStride) ||
        !SourceFromOuter(outer, &inner, &source) ||
        !ReadAt(inner + 0xC0, &innerStride) ||
        !ReadAt(inner + 0xC4, &innerCount) ||
        !SourceFromOuter(counterOuter, &counterInner, &counterSource) ||
        !ReadAt(counterInner + 0xC0, &counterStride) ||
        !ReadAt(counterInner + 0xC4, &counterCount)) {
        Out("ERROR: live BufferD3D12 chain is unreadable");
        return;
    }
    if (bindingStride != kStride || innerStride != kStride || innerCount != kCount) {
        Out("ERROR: layout mismatch: binding stride=%u inner stride=%u count=%u",
            bindingStride, innerStride, innerCount);
        return;
    }
    if (counterStride != kCounterStride || counterCount != kCounterCount) {
        Out("ERROR: counter layout mismatch: stride=%u count=%u", counterStride, counterCount);
        return;
    }

    D3D12_RESOURCE_DESC sourceDesc = {}, counterDesc = {};
    if (!ValidateTarget(source, kByteSize, true, &sourceDesc)) {
        Out("ERROR: native resource is not the expected %llu-byte UAV buffer", kByteSize);
        return;
    }
    if (!ValidateTarget(counterSource, kCounterByteSize, false, &counterDesc)) {
        Out("ERROR: native counter cannot provide the %llu-byte UAV view: dim=%u width=%llu flags=0x%X",
            kCounterByteSize, static_cast<u32>(counterDesc.Dimension), counterDesc.Width,
            static_cast<u32>(counterDesc.Flags));
        return;
    }

    ID3D12Device* device = nullptr;
    HRESULT hr = source->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&device));
    if (FAILED(hr) || !device) {
        Out("ERROR: ID3D12Resource::GetDevice failed: 0x%08X", static_cast<u32>(hr));
        return;
    }

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC readbackDesc = sourceDesc;
    readbackDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    ID3D12Resource* readback = nullptr;
    hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                         __uuidof(ID3D12Resource),
                                         reinterpret_cast<void**>(&readback));
    if (FAILED(hr) || !readback) {
        device->Release();
        Out("ERROR: CreateCommittedResource(READBACK) failed: 0x%08X", static_cast<u32>(hr));
        return;
    }

    D3D12_RESOURCE_DESC counterReadbackDesc = counterDesc;
    counterReadbackDesc.Width = kCounterByteSize;
    counterReadbackDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    ID3D12Resource* counterReadback = nullptr;
    hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &counterReadbackDesc,
                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                         __uuidof(ID3D12Resource),
                                         reinterpret_cast<void**>(&counterReadback));
    device->Release();
    if (FAILED(hr) || !counterReadback) {
        readback->Release();
        Out("ERROR: CreateCommittedResource(counter READBACK) failed: 0x%08X", static_cast<u32>(hr));
        return;
    }

    ReleaseResources();
    source->AddRef();
    counterSource->AddRef();
    g_source = source;
    g_readback = readback;
    g_counterSource = counterSource;
    g_counterReadback = counterReadback;
    g_outer = outer;
    g_inner = inner;
    g_counterOuter = counterOuter;
    g_counterInner = counterInner;
    Out("Prepared instrumented ManyLights readback:");
    Out("  outer=0x%llX inner=0x%llX source=0x%llX readback=0x%llX",
        g_outer, g_inner, reinterpret_cast<u64>(g_source), reinterpret_cast<u64>(g_readback));
    Out("  counter outer=0x%llX inner=0x%llX source=0x%llX readback=0x%llX",
        g_counterOuter, g_counterInner, reinterpret_cast<u64>(g_counterSource),
        reinterpret_cast<u64>(g_counterReadback));
    Out("  validated %u records x %u bytes = %llu bytes", kCount, kStride, kByteSize);
    Out("  validated %u counters x %u bytes = %llu bytes",
        kCounterCount, kCounterStride, kCounterByteSize);
    Out("  counter native resource width=%llu; copying the validated 512-byte view prefix",
        counterDesc.Width);
    Out("Next: `manylights arm` (one-shot breakpoint at RVA 0x%llX)", kAfterDispatchRva);
}

void Arm() {
    if (!g_cfg.allowManyLights) { Out("Refused: [Explorer] AllowManyLights=0"); return; }
    if (!g_source || !g_readback || !g_counterSource || !g_counterReadback) {
        Out("Run `manylights prepare` first");
        return;
    }
    const LONG state = g_state;
    if (state == Armed || state == Copying) { Out("Already %s", StateName(state)); return; }

    const u64 target = g_game.moduleBase + kAfterDispatchRva;
    const u8 expected[3] = { 0x48, 0x8B, 0xCF };  // mov rcx,rdi
    u8 actual[3] = {};
    if (!mem::SafeRead(reinterpret_cast<const void*>(target), actual, sizeof(actual)) ||
        memcmp(actual, expected, sizeof(expected)) != 0) {
        Out("ERROR: producer post-dispatch signature mismatch at 0x%llX", target);
        return;
    }

    InterlockedExchange(&g_error, 0);
    g_commandList = 0;
    g_issuedAt = 0;
    InterlockedExchange(&g_state, Armed);
    if (!bp::ArmCaptureCallback(target, "manylights readback", CopyAtProducer)) {
        InterlockedExchange(&g_state, Idle);
        Out("ERROR: could not arm: %s", bp::OutcomeName(bp::kUserCapture));
        return;
    }
    Out("Armed one-shot ManyLights readback at RVA 0x%llX", kAfterDispatchRva);
    Out("It will copy only after the exact private-emitter producer dispatch.");
}

void PrepareFilter() {
    if (!g_cfg.allowManyLights) {
        Out("Refused: [Explorer] AllowManyLights=0");
        return;
    }
    if (g_filterState == Armed || g_filterState == Copying) {
        Out("Refused: a filtered ManyLights capture is currently %s",
            FilterStateName(g_filterState));
        return;
    }

    const bp::Capture* capture = bp::UserCapture();
    const u64 expectedRip = g_game.moduleBase + kFilterBindingReadyRva;
    if (!capture->hit || capture->rip != expectedRip || !capture->r12) {
        Out("Need a completed `bpcapture 0x%llX` first; current capture RIP is 0x%llX",
            kFilterBindingReadyRva, capture->rip);
        return;
    }

    const u64 outer = capture->r12;
    u64 inner = 0;
    u32 stride = 0, count = 0;
    ID3D12Resource* source = nullptr;
    if (!SourceFromOuter(outer, &inner, &source) ||
        !ReadAt(inner + 0xC0, &stride) || !ReadAt(inner + 0xC4, &count)) {
        Out("ERROR: filtered ManyLights BufferD3D12 chain is unreadable");
        return;
    }
    if (stride != kStride || count != kCount) {
        Out("ERROR: filtered layout mismatch: stride=%u count=%u", stride, count);
        return;
    }

    D3D12_RESOURCE_DESC sourceDesc = {};
    if (!ValidateTarget(source, kByteSize, true, &sourceDesc)) {
        Out("ERROR: filtered resource is not the expected %llu-byte UAV buffer", kByteSize);
        return;
    }

    ID3D12Device* device = nullptr;
    HRESULT hr = source->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&device));
    if (FAILED(hr) || !device) {
        Out("ERROR: filtered ID3D12Resource::GetDevice failed: 0x%08X", static_cast<u32>(hr));
        return;
    }

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC readbackDesc = sourceDesc;
    readbackDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    ID3D12Resource* readback = nullptr;
    hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                         __uuidof(ID3D12Resource),
                                         reinterpret_cast<void**>(&readback));
    device->Release();
    if (FAILED(hr) || !readback) {
        Out("ERROR: CreateCommittedResource(filtered READBACK) failed: 0x%08X",
            static_cast<u32>(hr));
        return;
    }

    ReleaseFilterResources();
    source->AddRef();
    g_filterSource = source;
    g_filterReadback = readback;
    g_filterOuter = outer;
    g_filterInner = inner;
    Out("Prepared filtered ManyLights readback:");
    Out("  outer=0x%llX inner=0x%llX source=0x%llX readback=0x%llX",
        g_filterOuter, g_filterInner, reinterpret_cast<u64>(g_filterSource),
        reinterpret_cast<u64>(g_filterReadback));
    Out("  validated %u records x %u bytes = %llu bytes", kCount, kStride, kByteSize);
    Out("Next: `manylights filterarm` (one-shot after filter Dispatch at RVA 0x%llX)",
        kFilterAfterDispatchRva);
}

void ArmFilter() {
    if (!g_cfg.allowManyLights) { Out("Refused: [Explorer] AllowManyLights=0"); return; }
    if (!g_filterSource || !g_filterReadback) {
        Out("Run `manylights filterprepare` first");
        return;
    }
    const LONG state = g_filterState;
    if (state == Armed || state == Copying) {
        Out("Already %s", FilterStateName(state));
        return;
    }

    const u64 target = g_game.moduleBase + kFilterAfterDispatchRva;
    const u8 expected[3] = { 0x48, 0x8B, 0x5C };  // mov rbx,[rsp+0x50]
    u8 actual[3] = {};
    if (!mem::SafeRead(reinterpret_cast<const void*>(target), actual, sizeof(actual)) ||
        memcmp(actual, expected, sizeof(expected)) != 0) {
        Out("ERROR: filter post-dispatch signature mismatch at 0x%llX", target);
        return;
    }

    InterlockedExchange(&g_filterError, 0);
    g_filterOuterUsed = g_filterInnerUsed = g_filterSourceUsed = 0;
    g_filterCommandList = 0;
    g_filterIssuedAt = 0;
    InterlockedExchange(&g_filterState, Armed);
    if (!bp::ArmCaptureCallback(target, "filtered manylights readback", CopyAtFilter)) {
        InterlockedExchange(&g_filterState, Idle);
        Out("ERROR: could not arm filtered readback: %s", bp::OutcomeName(bp::kUserCapture));
        return;
    }
    Out("Armed one-shot filtered ManyLights readback at RVA 0x%llX", kFilterAfterDispatchRva);
    Out("It will copy only after the exact 512-group filter dispatch.");
}

void PrepareEmitter(bool simulation) {
    if (!g_cfg.allowManyLights) {
        Out("Refused: [Explorer] AllowManyLights=0");
        return;
    }
    if (InterlockedCompareExchange(&g_writerArmed, 0, 0) != 0) {
        Out("Refused: run `manylights writerstop` before changing the writer target");
        return;
    }
    if (g_emitterState == Armed || g_emitterState == Copying) {
        Out("Refused: an emitter-input capture is currently %s",
            EmitterStateName(g_emitterState));
        return;
    }

    const char* bufferName = simulation ? "gpuEmitterSimulationVariablesBuffer"
                                        : "gpuEmitterVariablesBuffer";
    const u64 bindingRva = simulation ? kSimulationBindingReadyRva : kEmitterBindingReadyRva;
    const u32 expectedStride = simulation ? kSimulationStride : kEmitterStride;
    const bp::Capture* capture = bp::UserCapture();
    const u64 expectedRip = g_game.moduleBase + bindingRva;
    if (!capture->hit || capture->rip != expectedRip || !capture->rbx) {
        Out("Need a completed `bpcapture 0x%llX` first; current capture RIP is 0x%llX",
            bindingRva, capture->rip);
        return;
    }

    const u64 outer = capture->rbx;
    u64 bindingWrapper = 0, inner = 0;
    u32 bindingCount = 0, bindingStride = 0, innerStride = 0, innerCount = 0;
    ID3D12Resource* source = nullptr;
    if (!ReadAt(capture->rax + 8, &bindingWrapper) || !bindingWrapper ||
        !ReadAt(bindingWrapper + 0x28, &bindingCount) ||
        !ReadAt(bindingWrapper + 0x2C, &bindingStride) ||
        !SourceFromOuter(outer, &inner, &source) ||
        !ReadAt(inner + 0xC0, &innerStride) ||
        !ReadAt(inner + 0xC4, &innerCount)) {
        Out("ERROR: %s chain is unreadable", bufferName);
        return;
    }
    const bool layoutMatches = simulation
        ? bindingCount == kSimulationViewCount && innerCount >= bindingCount
        : innerCount == kEmitterCount;
    if (bindingStride != expectedStride || innerStride != expectedStride || !layoutMatches) {
        Out("ERROR: emitter layout mismatch: binding count=%u stride=%u inner count=%u stride=%u",
            bindingCount, bindingStride, innerCount, innerStride);
        return;
    }
    const u32 captureCount = simulation ? bindingCount : innerCount;
    const u64 byteSize = static_cast<u64>(captureCount) * expectedStride;

    D3D12_RESOURCE_DESC sourceDesc = {};
    if (!ValidateEmitterTarget(source, byteSize, &sourceDesc)) {
        Out("ERROR: emitter resource is not a buffer of at least %llu bytes", byteSize);
        return;
    }

    D3D12_HEAP_PROPERTIES sourceHeap = {};
    D3D12_HEAP_FLAGS sourceHeapFlags = D3D12_HEAP_FLAG_NONE;
    HRESULT hr = source->GetHeapProperties(&sourceHeap, &sourceHeapFlags);
    if (FAILED(hr)) {
        Out("ERROR: emitter GetHeapProperties failed: 0x%08X", static_cast<u32>(hr));
        return;
    }
    if (sourceHeap.Type != D3D12_HEAP_TYPE_DEFAULT &&
        sourceHeap.Type != D3D12_HEAP_TYPE_UPLOAD) {
        Out("ERROR: unsupported emitter heap type=%u; no copy armed",
            static_cast<u32>(sourceHeap.Type));
        return;
    }

    ID3D12Device* device = nullptr;
    hr = source->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&device));
    if (FAILED(hr) || !device) {
        Out("ERROR: emitter ID3D12Resource::GetDevice failed: 0x%08X", static_cast<u32>(hr));
        return;
    }

    D3D12_HEAP_PROPERTIES readbackHeap = {};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    readbackHeap.CreationNodeMask = 1;
    readbackHeap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC readbackDesc = sourceDesc;
    readbackDesc.Width = byteSize;
    readbackDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    ID3D12Resource* readback = nullptr;
    hr = device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                         __uuidof(ID3D12Resource),
                                         reinterpret_cast<void**>(&readback));
    device->Release();
    if (FAILED(hr) || !readback) {
        Out("ERROR: CreateCommittedResource(emitter READBACK) failed: 0x%08X",
            static_cast<u32>(hr));
        return;
    }

    ReleaseEmitterResources();
    source->AddRef();
    g_emitterSource = source;
    g_emitterReadback = readback;
    g_emitterOuter = outer;
    g_emitterInner = inner;
    g_emitterHeapType = sourceHeap.Type;
    g_emitterByteSize = byteSize;
    g_emitterStride = expectedStride;
    g_emitterCount = captureCount;
    g_emitterSimulation = simulation;
    Out("Prepared %s readback:", bufferName);
    Out("  outer=0x%llX inner=0x%llX source=0x%llX readback=0x%llX",
        g_emitterOuter, g_emitterInner, reinterpret_cast<u64>(g_emitterSource),
        reinterpret_cast<u64>(g_emitterReadback));
    Out("  validated %u records x %u bytes = %llu bytes; resource width=%llu heap=%u flags=0x%X",
        g_emitterCount, g_emitterStride, g_emitterByteSize, sourceDesc.Width,
        static_cast<u32>(sourceHeap.Type), static_cast<u32>(sourceHeapFlags));
    Out("Next: `manylights %sarm` (one-shot after producer Dispatch at RVA 0x%llX)",
        simulation ? "sim" : "emitter", kAfterDispatchRva);
}

void ArmEmitter() {
    if (!g_cfg.allowManyLights) { Out("Refused: [Explorer] AllowManyLights=0"); return; }
    if (!g_emitterSource || !g_emitterReadback) {
        Out("Run `manylights emitterprepare` or `manylights simprepare` first");
        return;
    }
    if (g_emitterState == Armed || g_emitterState == Copying) {
        Out("Already %s", EmitterStateName(g_emitterState));
        return;
    }

    const u64 target = g_game.moduleBase + kAfterDispatchRva;
    const u8 expected[3] = { 0x48, 0x8B, 0xCF };  // mov rcx,rdi
    u8 actual[3] = {};
    if (!mem::SafeRead(reinterpret_cast<const void*>(target), actual, sizeof(actual)) ||
        memcmp(actual, expected, sizeof(expected)) != 0) {
        Out("ERROR: producer post-dispatch signature mismatch at 0x%llX", target);
        return;
    }

    InterlockedExchange(&g_emitterError, 0);
    g_emitterCommandList = 0;
    g_emitterIssuedAt = 0;
    InterlockedExchange(&g_emitterState, Armed);
    if (!bp::ArmCaptureCallback(target, "emitter input readback", CopyEmitterInput)) {
        InterlockedExchange(&g_emitterState, Idle);
        Out("ERROR: could not arm emitter readback: %s", bp::OutcomeName(bp::kUserCapture));
        return;
    }
    Out("Armed one-shot %s readback at RVA 0x%llX",
        g_emitterSimulation ? "gpuEmitterSimulationVariablesBuffer"
                            : "gpuEmitterVariablesBuffer",
        kAfterDispatchRva);
}

bool SafeLabel(const char* input, char* output, size_t outputSize) {
    const char* src = (input && *input) ? input : "latest";
    size_t n = 0;
    for (; src[n] && n + 1 < outputSize; ++n) {
        const unsigned char c = static_cast<unsigned char>(src[n]);
        if (!std::isalnum(c) && c != '-' && c != '_') return false;
        output[n] = static_cast<char>(c);
    }
    output[n] = 0;
    return src[n] == 0 && n != 0;
}

void SaveFireCapture(const char* label) {
    if (!g_fireWatchCapture.hit) {
        Out("No matching fire capture to save");
        return;
    }
    char cleanLabel[64] = {};
    if (!SafeLabel(label, cleanLabel, sizeof(cleanLabel))) {
        Out("Invalid label; use only letters, digits, '-' and '_'");
        return;
    }
    struct Blob { const char* kind; const u8* data; size_t size; bool valid; };
    const Blob builderBlobs[] = {
        { "source", g_fireSourceSnapshot, sizeof(g_fireSourceSnapshot),
          g_fireSourceSnapshotValid },
        { "workitem", g_fireWorkItemSnapshot, sizeof(g_fireWorkItemSnapshot),
          g_fireWorkItemSnapshotValid },
        { "upstream", g_fireUpstreamSnapshot, sizeof(g_fireUpstreamSnapshot),
          g_fireUpstreamSnapshotValid },
        { "paired150", g_firePaired150Snapshot, sizeof(g_firePaired150Snapshot),
          g_firePaired150SnapshotValid },
        { "paired20", g_firePaired20Snapshot, sizeof(g_firePaired20Snapshot),
          g_firePaired20SnapshotValid },
    };
    const Blob ownerBlobs[] = {
        { "owner", g_fireOwnerSnapshot, sizeof(g_fireOwnerSnapshot),
          g_fireOwnerSnapshotValid },
        { "workitem", g_fireWorkItemSnapshot, sizeof(g_fireWorkItemSnapshot),
          g_fireWorkItemSnapshotValid },
        { "upstream", g_fireUpstreamSnapshot, sizeof(g_fireUpstreamSnapshot),
          g_fireUpstreamSnapshotValid },
    };
    const Blob sourceBlobs[] = {
        { "parent", g_fireParentSnapshot, sizeof(g_fireParentSnapshot),
          g_fireParentSnapshotValid },
        { "child", g_fireOwnerSnapshot, sizeof(g_fireOwnerSnapshot),
          g_fireOwnerSnapshotValid },
        { "source", g_fireAssociationSourceSnapshot,
          sizeof(g_fireAssociationSourceSnapshot),
          g_fireAssociationSourceSnapshotValid },
    };
    const bool ownerMode = g_fireWatchKind == FireWatchOwner;
    const bool sourceMode = g_fireWatchKind == FireWatchSource;
    const Blob* blobs = sourceMode ? sourceBlobs : ownerMode ? ownerBlobs : builderBlobs;
    const size_t blobCount = sourceMode ? _countof(sourceBlobs) :
                             ownerMode ? _countof(ownerBlobs) : _countof(builderBlobs);
    for (size_t i = 0; i < blobCount; ++i) {
        const Blob& blob = blobs[i];
        if (!blob.valid) { Out("Snapshot %s was unreadable at capture time", blob.kind); continue; }
        const std::string path = GameDirectory() +
                                 (sourceMode ? "\\fire-source-" :
                                  ownerMode ? "\\fire-owner-" : "\\fire-builder-") +
                                 cleanLabel +
                                 "-" + blob.kind + ".bin";
        bool saved = false;
        if (FILE* file = fopen(path.c_str(), "wb")) {
            saved = fwrite(blob.data, 1, blob.size, file) == blob.size;
            fclose(file);
        }
        Out("Fire %s snapshot: %s%s", blob.kind, path.c_str(),
            saved ? "" : " (SAVE FAILED)");
    }
}

void Readback(const char* label) {
    if (g_state != Issued && g_state != Read) {
        Out("No completed GPU copy; state=%s error=0x%08X", StateName(g_state), static_cast<u32>(g_error));
        return;
    }
    const ULONGLONG age = GetTickCount64() - g_issuedAt;
    if (age < kReadDelayMs) {
        Out("GPU copy is only %llu ms old; wait at least %llu ms and run `manylights read` again",
            age, kReadDelayMs);
        return;
    }

    char cleanLabel[64] = {};
    if (!SafeLabel(label, cleanLabel, sizeof(cleanLabel))) {
        Out("ERROR: label may contain only letters, digits, '-' and '_'");
        return;
    }

    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(kByteSize) };
    HRESULT hr = g_readback->Map(0, &readRange, &mapped);
    if (FAILED(hr) || !mapped) {
        Out("ERROR: READBACK Map failed: 0x%08X", static_cast<u32>(hr));
        return;
    }

    void* counterMapped = nullptr;
    D3D12_RANGE counterReadRange = { 0, static_cast<SIZE_T>(kCounterByteSize) };
    hr = g_counterReadback->Map(0, &counterReadRange, &counterMapped);
    if (FAILED(hr) || !counterMapped) {
        D3D12_RANGE written = { 0, 0 };
        g_readback->Unmap(0, &written);
        Out("ERROR: counter READBACK Map failed: 0x%08X", static_cast<u32>(hr));
        return;
    }

    const auto* lights = static_cast<const ManyLight*>(mapped);
    u32 piSlots = 0, nonzero = 0, shown = 0;
    for (u32 i = 0; i < kCount; ++i) {
        const ManyLight& light = lights[i];
        const bool isActive = std::fabs(light.position[3] - 3.14159265f) < 0.0001f;
        if (isActive) ++piSlots;
        if (light.position[0] != 0.0f || light.position[1] != 0.0f ||
            light.position[2] != 0.0f || light.position[3] != 0.0f) ++nonzero;
        if (isActive && shown < 8) {
            Out("  [%u] pos=(%.4f, %.4f, %.4f, %.7f) color=(%.5f, %.5f, %.5f, %.5f)",
                i, light.position[0], light.position[1], light.position[2], light.position[3],
                light.color[0], light.color[1], light.color[2], light.color[3]);
            ++shown;
        }
    }

    const auto* counters = static_cast<const u32*>(counterMapped);
    u32 nonzeroCounters = 0, maximum = 0, maximumIndex = 0;
    u64 counterSum = 0;
    for (u32 i = 0; i < kCounterCount; ++i) {
        const u32 value = counters[i];
        counterSum += value;
        if (value) {
            ++nonzeroCounters;
            Out("  counter[%u]=%u", i, value);
        }
        if (value > maximum) {
            maximum = value;
            maximumIndex = i;
        }
    }

    const std::string path = GameDirectory() + "\\manylights-" + cleanLabel + ".bin";
    const std::string counterPath = GameDirectory() + "\\manylights-counter-" + cleanLabel + ".bin";
    bool saved = false, counterSaved = false;
    if (FILE* file = fopen(path.c_str(), "wb")) {
        saved = fwrite(mapped, 1, static_cast<size_t>(kByteSize), file) == kByteSize;
        fclose(file);
    }
    if (FILE* file = fopen(counterPath.c_str(), "wb")) {
        counterSaved = fwrite(counterMapped, 1, static_cast<size_t>(kCounterByteSize), file) ==
                       kCounterByteSize;
        fclose(file);
    }
    D3D12_RANGE written = { 0, 0 };
    g_counterReadback->Unmap(0, &written);
    g_readback->Unmap(0, &written);

    InterlockedExchange(&g_state, Read);
    Out("ManyLights allocation: pi slots=%u nonzero=%u / %u, age=%llu ms",
        piSlots, nonzero, kCount, age);
    Out("  pi slots include stale data; use the captured counters to identify live records");
    Out("Counter readback: nonzero=%u / %u sum=%llu max=%u at [%u]",
        nonzeroCounters, kCounterCount, counterSum, maximum, maximumIndex);
    Out("Raw captures: %s%s", path.c_str(), saved ? "" : " (SAVE FAILED)");
    Out("              %s%s", counterPath.c_str(), counterSaved ? "" : " (SAVE FAILED)");
}

void ReadFilter(const char* label) {
    if (g_filterState != Issued && g_filterState != Read) {
        Out("No completed filtered GPU copy; state=%s error=0x%08X",
            FilterStateName(g_filterState), static_cast<u32>(g_filterError));
        return;
    }
    const ULONGLONG age = GetTickCount64() - g_filterIssuedAt;
    if (age < kReadDelayMs) {
        Out("Filtered GPU copy is only %llu ms old; wait at least %llu ms and run `manylights filterread` again",
            age, kReadDelayMs);
        return;
    }

    char cleanLabel[64] = {};
    if (!SafeLabel(label, cleanLabel, sizeof(cleanLabel))) {
        Out("ERROR: label may contain only letters, digits, '-' and '_'");
        return;
    }

    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(kByteSize) };
    const HRESULT hr = g_filterReadback->Map(0, &readRange, &mapped);
    if (FAILED(hr) || !mapped) {
        Out("ERROR: filtered READBACK Map failed: 0x%08X", static_cast<u32>(hr));
        return;
    }

    const auto* lights = static_cast<const ManyLight*>(mapped);
    u32 piSlots = 0, nonzero = 0, shown = 0;
    for (u32 i = 0; i < kCount; ++i) {
        const ManyLight& light = lights[i];
        const bool isActive = std::fabs(light.position[3] - 3.14159265f) < 0.0001f;
        if (isActive) ++piSlots;
        if (light.position[0] != 0.0f || light.position[1] != 0.0f ||
            light.position[2] != 0.0f || light.position[3] != 0.0f) ++nonzero;
        if (isActive && shown < 8) {
            Out("  [%u] pos=(%.4f, %.4f, %.4f, %.7f) color=(%.5f, %.5f, %.5f, %.5f)",
                i, light.position[0], light.position[1], light.position[2], light.position[3],
                light.color[0], light.color[1], light.color[2], light.color[3]);
            ++shown;
        }
    }

    const std::string path = GameDirectory() + "\\manylights-filtered-" + cleanLabel + ".bin";
    bool saved = false;
    if (FILE* file = fopen(path.c_str(), "wb")) {
        saved = fwrite(mapped, 1, static_cast<size_t>(kByteSize), file) == kByteSize;
        fclose(file);
    }
    D3D12_RANGE written = { 0, 0 };
    g_filterReadback->Unmap(0, &written);

    InterlockedExchange(&g_filterState, Read);
    Out("Filtered ManyLights allocation: pi slots=%u nonzero=%u / %u, age=%llu ms",
        piSlots, nonzero, kCount, age);
    Out("Raw capture: %s%s", path.c_str(), saved ? "" : " (SAVE FAILED)");
}

void ReadEmitter(const char* label) {
    if (g_emitterState != Issued && g_emitterState != Read) {
        Out("No completed emitter-input copy; state=%s error=0x%08X",
            EmitterStateName(g_emitterState), static_cast<u32>(g_emitterError));
        return;
    }
    const ULONGLONG age = GetTickCount64() - g_emitterIssuedAt;
    if (age < kReadDelayMs) {
        Out("Emitter GPU copy is only %llu ms old; wait at least %llu ms",
            age, kReadDelayMs);
        return;
    }

    char cleanLabel[64] = {};
    if (!SafeLabel(label, cleanLabel, sizeof(cleanLabel))) {
        Out("ERROR: label may contain only letters, digits, '-' and '_'");
        return;
    }

    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(g_emitterByteSize) };
    const HRESULT hr = g_emitterReadback->Map(0, &readRange, &mapped);
    if (FAILED(hr) || !mapped) {
        Out("ERROR: emitter READBACK Map failed: 0x%08X", static_cast<u32>(hr));
        return;
    }

    const auto* bytes = static_cast<const u8*>(mapped);
    u32 nonzeroRecords = 0;
    u64 nonzeroBytes = 0;
    for (u32 record = 0; record < g_emitterCount; ++record) {
        bool nonzero = false;
        const u64 base = static_cast<u64>(record) * g_emitterStride;
        for (u32 offset = 0; offset < g_emitterStride; ++offset) {
            if (bytes[base + offset] != 0) {
                nonzero = true;
                ++nonzeroBytes;
            }
        }
        if (nonzero) ++nonzeroRecords;
    }

    const char* prefix = g_emitterSimulation ? "\\emitter-simulation-variables-"
                                             : "\\emitter-variables-";
    const std::string path = GameDirectory() + prefix + cleanLabel + ".bin";
    bool saved = false;
    if (FILE* file = fopen(path.c_str(), "wb")) {
        saved = fwrite(mapped, 1, static_cast<size_t>(g_emitterByteSize), file) ==
                g_emitterByteSize;
        fclose(file);
    }
    D3D12_RANGE written = { 0, 0 };
    g_emitterReadback->Unmap(0, &written);

    InterlockedExchange(&g_emitterState, Read);
    Out("%s: nonzero records=%u / %u, nonzero bytes=%llu, age=%llu ms",
        g_emitterSimulation ? "Emitter simulation variables" : "Emitter variables",
        nonzeroRecords, g_emitterCount, nonzeroBytes, age);
    Out("Raw capture: %s%s", path.c_str(), saved ? "" : " (SAVE FAILED)");
}

void Status() {
    Out("=== ManyLights GPU readback (instrumented) ===");
    Out("  enabled=%d state=%s error=0x%08X", g_cfg.allowManyLights ? 1 : 0,
        StateName(g_state), static_cast<u32>(g_error));
    Out("  outer=0x%llX inner=0x%llX source=0x%llX readback=0x%llX",
        g_outer, g_inner, reinterpret_cast<u64>(g_source), reinterpret_cast<u64>(g_readback));
    Out("  counter prepared outer=0x%llX inner=0x%llX source=0x%llX readback=0x%llX",
        g_counterOuter, g_counterInner, reinterpret_cast<u64>(g_counterSource),
        reinterpret_cast<u64>(g_counterReadback));
    Out("  counter copied   outer=0x%llX inner=0x%llX source=0x%llX",
        g_counterOuterUsed, g_counterInnerUsed, g_counterSourceUsed);
    Out("  commandList=0x%llX issuedAt=%llu", g_commandList, g_issuedAt);
    Out("  filtered enabled=%d state=%s error=0x%08X", g_cfg.allowManyLights ? 1 : 0,
        FilterStateName(g_filterState), static_cast<u32>(g_filterError));
    Out("  filtered prepared outer=0x%llX inner=0x%llX source=0x%llX readback=0x%llX",
        g_filterOuter, g_filterInner, reinterpret_cast<u64>(g_filterSource),
        reinterpret_cast<u64>(g_filterReadback));
    Out("  filtered copied   outer=0x%llX inner=0x%llX source=0x%llX",
        g_filterOuterUsed, g_filterInnerUsed, g_filterSourceUsed);
    Out("  filtered commandList=0x%llX issuedAt=%llu", g_filterCommandList, g_filterIssuedAt);
    Out("  emitter state=%s error=0x%08X heap=%u",
        EmitterStateName(g_emitterState), static_cast<u32>(g_emitterError),
        static_cast<u32>(g_emitterHeapType));
    Out("  emitter outer=0x%llX inner=0x%llX source=0x%llX readback=0x%llX",
        g_emitterOuter, g_emitterInner, reinterpret_cast<u64>(g_emitterSource),
        reinterpret_cast<u64>(g_emitterReadback));
    Out("  emitter commandList=0x%llX issuedAt=%llu",
        g_emitterCommandList, g_emitterIssuedAt);
    Out("  emitter kind=%s records=%u stride=%u bytes=%llu",
        g_emitterSimulation ? "simulation-variables" : "variables",
        g_emitterCount, g_emitterStride, g_emitterByteSize);
    Out("  writer armed=%ld hits=%ld method=+0x%lX commandObject=0x%llX",
        g_writerArmed, g_writerHits, g_writerMethod, g_writerCommandObject);
    Out("  writer target outer=0x%llX inner=0x%llX resource=0x%llX",
        g_writerTargetOuter, g_writerTargetInner, g_writerTargetResource);
    if (g_writerMethod) {
        Out("  writer caller=0x%llX thread=%u seenAt=%llu",
            g_writerCaller, g_writerThread, g_writerSeenAt);
        Out("  writer dst=0x%llX +0x%X src=0x%llX +0x%X bytes=%u",
            g_writerDestination, g_writerDestinationOffset, g_writerSource,
            g_writerSourceOffset, g_writerByteCount);
        Out("  writer source inner=0x%llX resource=0x%llX",
            g_writerSourceInner, g_writerSourceResource);
    }
}

}  // namespace

void Command(const char* action, const char* label) {
    if (cdt::instruments::OwnsCodeAddress(g_game.moduleBase + kFilterAfterDispatchRva) &&
        action && *action && _stricmp(action, "status") != 0 && _stricmp(action, "help") != 0) {
        Out("Refused: recurring telemetry owns GPU capture. Set [Lights] ManyLights=0 and restart for manual research captures.");
        return;
    }
    if (!action || !*action || _stricmp(action, "help") == 0) {
        Out("manylights prepare       consume bpcapture at RVA 0x%llX and create READBACK", kBindingReturnRva);
        Out("manylights arm           one-shot copy after producer Dispatch");
        Out("manylights status        show instrument state and native pointers");
        Out("manylights read [label]  validate and save manylights-<label>.bin");
        Out("manylights filterprepare consume bpcapture at RVA 0x%llX", kFilterBindingReadyRva);
        Out("manylights filterarm     one-shot copy after filtered-light Dispatch");
        Out("manylights filterread [label] save manylights-filtered-<label>.bin");
        Out("manylights emitterprepare consume bpcapture at RVA 0x%llX", kEmitterBindingReadyRva);
        Out("manylights emitterarm     one-shot gpuEmitterVariablesBuffer copy");
        Out("manylights emitterread [label] save emitter-variables-<label>.bin");
        Out("manylights simprepare    consume bpcapture at RVA 0x%llX", kSimulationBindingReadyRva);
        Out("manylights simarm        one-shot gpuEmitterSimulationVariablesBuffer copy");
        Out("manylights simread [label] save emitter-simulation-variables-<label>.bin");
        Out("manylights writerarm     after bpcapture 0x%llX, trace exact target copies", kCopyOuterCopyRva);
        Out("manylights writerstop    restore the command-object vtable");
        Out("manylights poolwatch <VA> one-shot capture of the first CPU-page writer");
        Out("manylights poolstatus    show writer RIP, registers and stack");
        Out("manylights poolstop      cancel and restore page protection");
        Out("manylights firewatch [RVA] capture only the known lamp's 0xE0 append");
        Out("manylights ownerwatch [RVA] capture its owner before queue insertion");
        Out("manylights sourcewatch [RVA] capture its parent/source association");
        Out("manylights firestatus    show work item, source and upstream group");
        Out("manylights firesave [label] save the three bounded hit snapshots");
        Out("manylights firestop      cancel the conditional breakpoint");
        Out("manylights reset         release retained D3D12 resources");
    } else if (_stricmp(action, "prepare") == 0) {
        Prepare();
    } else if (_stricmp(action, "arm") == 0) {
        Arm();
    } else if (_stricmp(action, "status") == 0) {
        Status();
    } else if (_stricmp(action, "read") == 0) {
        Readback(label);
    } else if (_stricmp(action, "filterprepare") == 0) {
        PrepareFilter();
    } else if (_stricmp(action, "filterarm") == 0) {
        ArmFilter();
    } else if (_stricmp(action, "filterread") == 0) {
        ReadFilter(label);
    } else if (_stricmp(action, "emitterprepare") == 0) {
        PrepareEmitter(false);
    } else if (_stricmp(action, "emitterarm") == 0) {
        ArmEmitter();
    } else if (_stricmp(action, "emitterread") == 0) {
        ReadEmitter(label);
    } else if (_stricmp(action, "simprepare") == 0) {
        PrepareEmitter(true);
    } else if (_stricmp(action, "simarm") == 0) {
        ArmEmitter();
    } else if (_stricmp(action, "simread") == 0) {
        ReadEmitter(label);
    } else if (_stricmp(action, "writerarm") == 0) {
        ArmWriter();
    } else if (_stricmp(action, "writerstop") == 0) {
        StopWriter(true);
    } else if (_stricmp(action, "poolwatch") == 0) {
        ArmPoolWatch(label);
    } else if (_stricmp(action, "poolstatus") == 0) {
        PoolWatchStatus();
    } else if (_stricmp(action, "poolstop") == 0) {
        StopPoolWatch(true);
    } else if (_stricmp(action, "firewatch") == 0) {
        ArmFireWatch(label, FireWatchBuilder);
    } else if (_stricmp(action, "ownerwatch") == 0) {
        ArmFireWatch(label, FireWatchOwner);
    } else if (_stricmp(action, "sourcewatch") == 0) {
        ArmFireWatch(label, FireWatchSource);
    } else if (_stricmp(action, "firestatus") == 0) {
        FireWatchStatus();
    } else if (_stricmp(action, "firesave") == 0) {
        SaveFireCapture(label);
    } else if (_stricmp(action, "firestop") == 0) {
        StopFireWatch(true);
    } else if (_stricmp(action, "reset") == 0) {
        if (g_state == Copying || g_filterState == Copying || g_emitterState == Copying) {
            Out("Refused: capture is raw=%s filtered=%s emitter=%s", StateName(g_state),
                FilterStateName(g_filterState), EmitterStateName(g_emitterState));
        } else {
            if (g_state == Armed || g_filterState == Armed || g_emitterState == Armed)
                bp::Cancel(bp::kUserCapture);
            StopFireWatch(false);
            StopPoolWatch(false);
            StopWriter(false);
            ReleaseResources();
            ReleaseFilterResources();
            ReleaseEmitterResources();
            Out("ManyLights readback reset");
        }
    } else {
        Out("Unknown manylights action: %s", action);
    }
}

void Shutdown() {
    StopFireWatch(false);
    if (g_fireWatchHandler) {
        RemoveVectoredExceptionHandler(g_fireWatchHandler);
        g_fireWatchHandler = nullptr;
    }
    StopPoolWatch(false);
    if (g_poolWatchHandler) {
        RemoveVectoredExceptionHandler(g_poolWatchHandler);
        g_poolWatchHandler = nullptr;
    }
    StopWriter(false);
    ReleaseResources();
    ReleaseFilterResources();
    ReleaseEmitterResources();
}

}}  // namespace ch::manylights
