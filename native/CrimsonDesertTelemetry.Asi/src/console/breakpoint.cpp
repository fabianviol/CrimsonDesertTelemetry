#include "breakpoint.h"
#include "mem.h"
#include "signatures.h"
#include "../instruments.h"

namespace ch { namespace bp {

namespace {

enum State : long { StFree = 0, StArmed = 1, StClaimed = 2, StCompleted = 3 };

struct Slot {
    volatile LONG state = StFree;
    u64           addr  = 0;      // kept for the life of the slot, not cleared on hit
    u8            orig  = 0;
    volatile u64* rcxTarget = nullptr;
    Capture*      capture   = nullptr;
    HitCallback   callback  = nullptr;
    const char*   what      = "";
    volatile LONG lateTraps = 0;
    volatile LONG completion = 0;  // 0 pending, 1 captured, 2 cancelled
    Outcome       outcome   = Outcome::NotAttempted;
};

Slot     g_slot[kSlotCount];
Capture  g_userCapture;
PVOID    g_handler = nullptr;

// Writes one byte into code. Not mem::Patch: that logs, and this runs inside an
// exception handler.
bool PokeByte(u64 addr, u8 value) {
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(addr), 1, PAGE_EXECUTE_READWRITE, &old))
        return false;
    *reinterpret_cast<volatile u8*>(addr) = value;
    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void*>(addr), 1, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(addr), 1);
    return true;
}

void FillCapture(Capture& c, EXCEPTION_POINTERS* info, u64 at) {
    const CONTEXT* x = info->ContextRecord;
    c.rip = at;               // the breakpoint address, not wherever RIP points now
    c.rsp = x->Rsp; c.rbp = x->Rbp; c.eflags = x->EFlags;
    c.rax = x->Rax; c.rbx = x->Rbx; c.rcx = x->Rcx; c.rdx = x->Rdx;
    c.rsi = x->Rsi; c.rdi = x->Rdi;
    c.r8  = x->R8;  c.r9  = x->R9;  c.r10 = x->R10; c.r11 = x->R11;
    c.r12 = x->R12; c.r13 = x->R13; c.r14 = x->R14; c.r15 = x->R15;
    c.threadId = GetCurrentThreadId();
    // The stack is the one thing worth reading here: a pass entry says little
    // without its return address and the arguments spilled around it. Guarded,
    // and a failure only costs the window.
    c.stackReadable = mem::SafeRead(reinterpret_cast<const void*>(x->Rsp),
                                    c.stack, sizeof(c.stack));
    c.hit = true;
}

LONG CALLBACK BreakpointVeh(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT)
        return EXCEPTION_CONTINUE_SEARCH;
    const u64 at = reinterpret_cast<u64>(info->ExceptionRecord->ExceptionAddress);

    for (int i = 0; i < kSlotCount; ++i) {
        Slot& s = g_slot[i];
        if (s.state == StFree || s.addr != at) continue;

        // Exactly one thread does the work. Everyone else falls through to the
        // shared exit below, which is the whole point: a thread that executed
        // this INT3 before the byte came back is in flight, not a second hit,
        // and it still has to leave.
        if (InterlockedCompareExchange(&s.state, StClaimed, StArmed) == StArmed) {
            if (s.rcxTarget) *s.rcxTarget = info->ContextRecord->Rcx;
            if (s.capture)   FillCapture(*s.capture, info, at);
            const bool restored = PokeByte(s.addr, s.orig);
            if (s.capture && !restored) s.capture->restoreFailed = true;
            if (s.callback) {
                __try {
                    s.callback(info);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    if (s.capture) s.capture->callbackException = GetExceptionCode();
                }
            }
            InterlockedExchange(&s.completion, 1);
            InterlockedExchange(&s.state, StCompleted);
        } else {
            const LONG n = InterlockedIncrement(&s.lateTraps);
            if (s.capture) s.capture->lateTraps = static_cast<u64>(n);
        }

        // Rewind so the instruction the 0xCC displaced actually runs. A late
        // thread that gets here before the byte is back simply traps again;
        // that loop is bounded by the winner finishing.
        info->ContextRecord->Rip = at;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

bool InExecutableSection(u64 addr) {
    if (!g_game.moduleBase) return false;
    for (const auto& section : mem::CodeSections(g_game.moduleBase))
        if (addr >= section.start && addr < section.end) return true;
    return false;
}

}  // namespace

bool EnsureHandler() {
    if (g_handler) return true;
    g_handler = AddVectoredExceptionHandler(1, BreakpointVeh);
    return g_handler != nullptr;
}

// Records the outcome as well as returning. At startup Log() writes nowhere, so
// the stored value is the only trace that survives to the log being opened.
#define BP_REFUSE(reason, ...) do { s.outcome = (reason); Log(__VA_ARGS__); return false; } while (0)

bool Arm(int index, u64 addr, volatile u64* rcxTarget, const char* what,
         HitCallback callback) {
    if (index < 0 || index >= kSlotCount) return false;
    Slot& s = g_slot[index];
    if (!addr) { s.outcome = Outcome::NoTarget; return false; }
    if (cdt::instruments::OwnsCodeAddress(addr))
        BP_REFUSE(Outcome::NoTarget, "bp: address belongs to the recurring telemetry detour; disable Lights.ManyLights before research capture");
    if (s.state == StArmed || s.state == StClaimed)
        BP_REFUSE(Outcome::RefusedSlotBusy, "bp: slot %d is still in use", index);

    // Two slots on one address is a hang, not a conflict: the VEH walks the
    // slots in order, and a stale completed slot found first rewinds RIP without
    // restoring the newly armed slot's 0xCC, so the thread traps forever. Found
    // by Codex while building the regression harness.
    for (int i = 0; i < kSlotCount; ++i)
        if (i != index && g_slot[i].state != StFree && g_slot[i].addr == addr)
            BP_REFUSE(Outcome::RefusedAliasedSlot,
                      "bp: refusing 0x%llX - slot %d already covers it", addr, i);

    // The check that caused the 0.3.0 startup regression. It is correct and
    // stays; what was wrong was calling it before anyone had published the
    // module range. Say which of the two it is, so the next failure is not
    // ambiguous again.
    if (!g_game.moduleBase || !g_game.moduleSize)
        BP_REFUSE(Outcome::RefusedNoModule,
                  "bp: refusing 0x%llX - module range not known yet", addr);
    if (!InExecutableSection(addr))
        BP_REFUSE(Outcome::RefusedNotExecutable,
                  "bp: refusing 0x%llX - not inside an executable section", addr);

    u8 orig = 0;
    if (!mem::SafeRead(reinterpret_cast<const void*>(addr), &orig, 1))
        BP_REFUSE(Outcome::RefusedUnreadable, "bp: refusing 0x%llX - unreadable", addr);
    if (orig == sig::kInt3) {
        // Saving 0xCC as "the original" writes a permanent breakpoint back on
        // restore. Whoever put it there is not us.
        BP_REFUSE(Outcome::RefusedAlreadyInt3, "bp: refusing 0x%llX - it already holds 0xCC", addr);
    }
    if (!EnsureHandler())
        BP_REFUSE(Outcome::RefusedNoHandler, "bp: AddVectoredExceptionHandler failed");

    s.addr = addr;
    s.orig = orig;
    s.rcxTarget = rcxTarget;
    // A refused request must not erase a previous capture or race with an
    // already-armed slot's handler. Reset only after all arm prechecks passed.
    if (index == kUserCapture) {
        g_userCapture = Capture();
        s.capture = &g_userCapture;
    }
    s.what = what ? what : "";
    s.callback = callback;
    s.lateTraps = 0;
    InterlockedExchange(&s.completion, 0);
    InterlockedExchange(&s.state, StArmed);

    if (!PokeByte(addr, sig::kInt3)) {
        InterlockedExchange(&s.state, StFree);
        BP_REFUSE(Outcome::RefusedWriteFailed, "bp: could not write 0xCC at 0x%llX", addr);
    }
    s.outcome = Outcome::Armed;
    return true;
}
#undef BP_REFUSE

void SetOutcome(int index, Outcome outcome) {
    if (index >= 0 && index < kSlotCount) g_slot[index].outcome = outcome;
}

const char* OutcomeName(int index) {
    if (index < 0 || index >= kSlotCount) return "?";
    switch (g_slot[index].outcome) {
        case Outcome::NotAttempted:         return "never attempted";
        case Outcome::Disabled:             return "disabled by configuration";
        case Outcome::NoTarget:             return "no target resolved";
        case Outcome::RefusedNoModule:      return "REFUSED: module range unknown";
        case Outcome::RefusedNotExecutable: return "REFUSED: not executable";
        case Outcome::RefusedUnreadable:    return "REFUSED: unreadable";
        case Outcome::RefusedAlreadyInt3:   return "REFUSED: already 0xCC";
        case Outcome::RefusedSlotBusy:      return "REFUSED: slot busy";
        case Outcome::RefusedAliasedSlot:   return "REFUSED: address aliases another slot";
        case Outcome::RefusedNoHandler:     return "REFUSED: no VEH";
        case Outcome::RefusedWriteFailed:   return "REFUSED: could not write 0xCC";
        case Outcome::Armed:                return "armed";
        default:                            return "?";
    }
}

bool ArmCapture(u64 addr, const char* what) {
    return Arm(kUserCapture, addr, nullptr, what);
}

bool ArmCaptureCallback(u64 addr, const char* what, HitCallback callback) {
    return Arm(kUserCapture, addr, nullptr, what, callback);
}

const char* CompletionName(int index) {
    if (index < 0 || index >= kSlotCount) return "?";
    switch (g_slot[index].completion) {
        case 1: return "captured";
        case 2: return "cancelled without capture";
        default: return "no capture or cancellation";
    }
}

void ReportSlot(int index) {
    if (index < 0 || index >= kSlotCount) return;
    Out("  slot %d: state=%s address=0x%llX last-arm=%s completion=%s", index,
        SlotStateName(index), SlotAddress(index), OutcomeName(index), CompletionName(index));
}

void ReportStartup() {
    Out("=== startup capture diagnostics ===");
    Out("  console owner=0x%llX", g_game.parentThis);
    ReportSlot(kConsole);
    Out("  debug owner=0x%llX registration=0x%llX", g_game.debugThis, g_game.debugRegFunc);
    ReportSlot(kDebugCommands);
}

bool Cancel(int index) {
    if (index < 0 || index >= kSlotCount) return false;
    Slot& s = g_slot[index];
    // Only the transition out of Armed is ours to make. Claimed means a handler
    // is mid-capture and will restore the byte itself.
    if (InterlockedCompareExchange(&s.state, StCompleted, StArmed) != StArmed) return false;
    PokeByte(s.addr, s.orig);
    InterlockedExchange(&s.completion, 2);
    return true;
}

void Shutdown() {
    for (int i = 0; i < kSlotCount; ++i) Cancel(i);
    if (g_handler) { RemoveVectoredExceptionHandler(g_handler); g_handler = nullptr; }
}

const Capture* UserCapture() { return &g_userCapture; }
u64 SlotAddress(int index) {
    return (index >= 0 && index < kSlotCount) ? g_slot[index].addr : 0;
}
const char* SlotWhat(int index) {
    return (index >= 0 && index < kSlotCount) ? g_slot[index].what : "";
}
const char* SlotStateName(int index) {
    if (index < 0 || index >= kSlotCount) return "?";
    switch (g_slot[index].state) {
        case StFree:      return "free";
        case StArmed:     return "armed";
        case StClaimed:   return "claimed";
        case StCompleted: return "completed";
        default:          return "?";
    }
}

}}  // namespace ch::bp
