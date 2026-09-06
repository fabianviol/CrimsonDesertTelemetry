// One-shot software breakpoints, shared by discovery and the `bpcapture`
// command.
//
// Extracted from discovery.cpp when a second user appeared, and corrected in the
// move. The original was a single flag cleared on the first hit, with the VEH
// matching on it; a breakpoint exception matching nothing falls through to
// EXCEPTION_CONTINUE_SEARCH, which for a breakpoint means the process ends.
// That is fine for a function the game calls once during initialisation and a
// crash for anything a render thread reaches:
//
//     thread A hits the INT3      -> captures, restores the byte, clears the slot
//     thread B executed the same  -> arrives afterwards, matches nothing, dies
//     INT3 a moment earlier
//
// B is not a second hit. It is a trap already in flight, and it still has to be
// walked back over the instruction that has since been restored. So a slot keeps
// its address and original byte for its whole life and moves through three
// states instead:
//
//     Free --Arm--> Armed --first trap, CAS--> Claimed --byte back--> Completed
//
// Only the thread that wins the compare-and-exchange records anything. Every
// other thread trapping on that address, whenever it arrives, is sent back to
// the breakpoint address and continues. If it gets there before the byte is
// restored it simply traps again, which is bounded by the winner finishing.
#pragma once

#include "common.h"

namespace ch { namespace bp {

using HitCallback = void (*)(EXCEPTION_POINTERS* info);

// What the VEH copies out. No pointer is followed here: dereferencing a register
// inside an exception handler turns a wrong guess into a second exception. Walk
// the values afterwards with dump / deref / vtable, where a bad address costs a
// line of output.
struct Capture {
    bool hit = false;
    u64  rip = 0, rsp = 0, rbp = 0, eflags = 0;
    u64  rax = 0, rbx = 0, rcx = 0, rdx = 0, rsi = 0, rdi = 0;
    u64  r8 = 0, r9 = 0, r10 = 0, r11 = 0, r12 = 0, r13 = 0, r14 = 0, r15 = 0;
    u64  threadId = 0;
    static const int kStackWords = 16;
    u64  stack[kStackWords] = {};   // [rsp .. rsp+0x78], zero where unreadable
    bool stackReadable = false;
    bool restoreFailed = false;     // the original byte could not be written back
    u32  callbackException = 0;     // SEH code if an optional hit callback faulted
    u64  lateTraps = 0;             // other threads that arrived after the claim
};

enum SlotIndex { kConsole = 0, kDebugCommands = 1, kUserCapture = 2, kSlotCount = 3 };

// Why an arm did or did not happen. The two startup slots are armed from
// DllMain, before OpenLog() has run, so Log() at that moment writes nowhere.
// Without this, a later "owner is 0" is ambiguous between four quite different
// failures: the target was never resolved, arming was refused, it was armed but
// never hit, or it was hit. Recorded at arm time, printed once the log exists.
enum class Outcome {
    NotAttempted,        // nothing ever called Arm for this slot
    Disabled,            // startup capture disabled by configuration
    NoTarget,            // caller had no address to give - string or xref missing
    RefusedNoModule,     // module range not known yet (the 0.3.0 regression)
    RefusedNotExecutable,
    RefusedUnreadable,
    RefusedAlreadyInt3,
    RefusedSlotBusy,
    RefusedAliasedSlot,  // another slot already covers this address
    RefusedNoHandler,
    RefusedWriteFailed,
    Armed,
};
const char* OutcomeName(int index);
void SetOutcome(int index, Outcome outcome);   // for callers that never reach Arm
// Completion is separate from the last arm attempt, which may have been refused
// while an older slot/capture remained valid. These reports also reach the log.
const char* CompletionName(int index);
void ReportSlot(int index);
void ReportStartup();

// Installs the vectored handler if it is not up yet. Idempotent.
bool EnsureHandler();

// Arms `index` at `addr`. `rcxTarget`, when given, receives RCX on the first hit
// — that is how discovery captures a `this` pointer without a Capture struct.
// Refuses an address that is not in an executable section, and refuses one that
// already holds 0xCC: saving that as "the original byte" would write a permanent
// breakpoint back on restore.
bool Arm(int index, u64 addr, volatile u64* rcxTarget, const char* what,
         HitCallback callback = nullptr);

// Arms the user slot and records the full context on the first hit.
bool ArmCapture(u64 addr, const char* what);

// As ArmCapture, but invokes `callback` once on the winning thread after the
// original byte is restored and before execution resumes at the target. This is
// for small render-thread operations that must happen at an exact call site.
bool ArmCaptureCallback(u64 addr, const char* what, HitCallback callback);

// Moves Armed -> Completed by CAS and restores the byte only if that transition
// was won. Never touches a slot another thread is mid-claim on.
bool Cancel(int index);

// Cancels every slot and removes the handler. Safe to call more than once.
void Shutdown();

// Read-only views for the status command and the log.
const Capture* UserCapture();
u64  SlotAddress(int index);
const char* SlotStateName(int index);
const char* SlotWhat(int index);

}}  // namespace ch::bp

