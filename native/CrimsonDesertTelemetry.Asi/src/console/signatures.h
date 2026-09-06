// Every build-dependent constant recovered from the original mod, in one place.
//
// When a game update breaks discovery, this is the only file that should need
// editing. ../ANALYSIS.md section 5 explains where each value came from and how
// likely it is to survive an update. The playbook rule applies here too:
// structures persist, instruction bytes break. Prefer widening a pattern over
// re-deriving a new fixed one.
#pragma once

#include "common.h"

namespace ch { namespace sig {

// ---------------------------------------------------------- engine strings --
// Still present in build 25116796; verified in RTTI_AND_CONSOLE.md.
constexpr const char* kShowDebugConsole = "PearlAbyssEngine.ShowDebugConsole";
constexpr const char* kHideDebugConsole = "PearlAbyssEngine.HideDebugConsole";
constexpr const char* kToggleDebugMode  = "PearlAbyssEngine.ToggleDebugMode";
constexpr const char* kEngineConsoleCmd = "EngineConsoleCommandHandler";

// Dev/debug variables the original writes 1 into (phase 5).
inline const char* const kDevVarNames[] = {
    "_isDev", "_isDevLevel", "_isDebug", "_debugMode", "_debugLevel",
    "isDebugBuild", "DevMenuEnabled", "ConsoleEnabled", "bEnableConsole",
    "_isDevMode", "_isRelease", "IsDebugConsoleEnabled",
};

// Informational only (phase 6); nothing is patched through these.
inline const char* const kDispatcherNames[] = {
    "ExecuteCommand", "ExecuteConsoleCommand", "ProcessCommand", "RunCommand",
    "DispatchCommand", "OnConsoleCommand", "ConsoleCommand", "DebugCommand",
};

// ------------------------------------------------------------ gate pattern --
//
//   74 43        je    +0x43
//   45 33 C9     xor   r9d, r9d
//   45 33 C0     xor   r8d, r8d
//   48 8D 15 ..  lea   rdx, [rip+disp32]
//
// The early scan matches all eleven bytes, so it also depends on the 0x43 jump
// displacement. The late scan around a known xref matches only the six register
// bytes plus a leading 0x74-or-0x90, so it survives a changed displacement.
constexpr u8 kGateFull[] = {
    0x74, 0x43, 0x45, 0x33, 0xC9, 0x45, 0x33, 0xC0, 0x48, 0x8D, 0x15,
};
constexpr u8 kGateFullPatched[] = {
    0x90, 0x90, 0x45, 0x33, 0xC9, 0x45, 0x33, 0xC0, 0x48, 0x8D, 0x15,
};
// The six bytes that identify a gate regardless of the jump distance.
constexpr u8 kGateTail[] = { 0x45, 0x33, 0xC9, 0x45, 0x33, 0xC0 };

// ------------------------------------------------------ instruction shapes --
// ModRM /5 with mod=00 rm=101 is RIP-relative. The register field selects the
// destination, so the third byte enumerates `lea reg,[rip+d32]`:
//   0x05 rax  0x0D rcx  0x15 rdx  0x1D rbx  0x25 rsp  0x2D rbp  0x35 rsi  0x3D rdi
// The original recognises only 0x15 (rdx). We accept all of them and log which
// one matched, because a register-allocation change is the single most likely
// way for a game update to break xref discovery.
constexpr u8 kLeaRip   = 0x8D;   // 48 8D /r
constexpr u8 kMovRegRip = 0x8B;  // 48 8B /r   (mov reg,[rip+d32])
constexpr u8 kModRmRdx = 0x15;
constexpr u8 kModRmRcx = 0x0D;

// 83 3D <rel32> FF   cmp dword ptr [rip+d32], -1     (dev variable test)
constexpr u8 kCmpDwordRip[] = { 0x83, 0x3D };
constexpr u8 kCmpDwordImm   = 0xFF;

constexpr u8 kCallRel32 = 0xE8;
constexpr u8 kInt3      = 0xCC;

// ------------------------------------------------------- search windows -----
// All empirical, taken from the original. Widening them costs scan time and
// raises the false-positive rate; narrowing them loses matches.
constexpr int kGateBackWindow      = 0x100;   // xref  -> gate
constexpr int kCtxBackWindow       = 0x20;    // gate  -> mov rcx,[rip+x]
constexpr int kHandlerFwdWindow    = 0x80;    // xref  -> lea rcx,[rip+x]
constexpr int kSetCallbackWindow   = 0x40;    // lea   -> call
constexpr int kPrologueBackWindow  = 0x1000;  // gate  -> enclosing function start
constexpr int kEchPrologueWindow   = 0x800;   // ECH xref -> dispatch function start
constexpr int kParentGateSpan      = 0x4F5;   // parent function body scanned for gates
constexpr int kCmdObjScanSize      = 0x200;   // cmdObj bytes scanned for the handler
constexpr int kCmdObjInnerScan     = 0x80;    // one level of indirection below that
constexpr int kNearFuncWindow      = 0x1000;  // +/- around registerCmdFunc
constexpr int kDevVarBackWindow    = 0x40;    // dev-var xref -> cmp instruction
constexpr int kMaxDevVarXrefs      = 4;
constexpr int kMaxEchXrefs         = 3;
constexpr int kMaxParentCallers    = 5;
constexpr int kMaxNearFuncs        = 10;

// ------------------------------------------ hide-path structure offsets -----
//
// THE MOST FRAGILE THING IN THIS FILE.
//
// The original reads three rel32 displacements out of the show handler's own
// machine code at these byte offsets, to recover two pointers and the function
// that notifies the UI that the console closed. Nothing validates them: if the
// game function was recompiled they yield garbage, and the call is only saved
// by its __try. They affect the hide path only; showing the console works
// without them.
//
// If HideConsole misbehaves on a new build, disable it with
// [Console] HideNotification=0 rather than guessing new offsets.
constexpr int kShowFnPtrA   = 0x6C;   // -> ptrA, next instruction at +0x70
constexpr int kShowFnPtrB   = 0x7D;   // -> ptrB, next instruction at +0x81
constexpr int kShowFnNotify = 0xA2;   // -> notification function, next at +0xA6
constexpr int kShowFnPtrANext   = 0x70;
constexpr int kShowFnPtrBNext   = 0x81;
constexpr int kShowFnNotifyNext = 0xA6;

constexpr int kParentThisIndirect = 0x08;    // parentThis -> object
constexpr int kUiVtableSlot       = 0x148;   // virtual call on that object
constexpr int kUiReceiverOffset   = 0x18;    // notification receiver
constexpr int kUiStateOffset      = 0x30;    // logged as two floats

// ------------------------------------------------------------- input keys --
constexpr int kToggleKey        = VK_OEM_3;  // 0xC0, the ~ / backtick key
constexpr int kDiagnosticsKey   = VK_F11;
constexpr u32 kToggleDebounceMs = 300;

// exec: scancodes used to clear the console input line before typing.
// lParam values are (repeat=1 | scancode<<16), with 0xC0 in the high byte on
// key-up. They matter: some input layers ignore messages with a zero scancode.
constexpr LPARAM kLpHomeDown = 0x00470001;  constexpr LPARAM kLpHomeUp = 0xC0470001;
constexpr LPARAM kLpEndDown  = 0x004F0001;  constexpr LPARAM kLpEndUp  = 0xC04F0001;
constexpr LPARAM kLpShiftDown= 0x002A0001;  constexpr LPARAM kLpShiftUp= 0xC02A0001;
constexpr LPARAM kLpDelDown  = 0x00530001;  constexpr LPARAM kLpDelUp  = 0xC0530001;
constexpr LPARAM kLpEnterDown= 0x001C0001;  constexpr LPARAM kLpEnterUp= 0xC01C0001;

// ------------------------------------------- PearlAbyssEngine.Debug.* -------
// A second command system, unreachable from the chat console. Full derivation
// in ../DEBUG_COMMANDS_20260905.md. Everything here is either a name (stable
// across builds, per PLAYBOOK section 1) or a field offset (mostly stable);
// no stub or function address is hard-coded.

// Anchor for finding the registration function that owns all 208 of these
// commands. Any name registered by that function would do; this one is used
// because its stub is also the one the fire investigation wants.
constexpr const char* kDebugRegAnchor = "PearlAbyssEngine.Debug.ToggleRenderingFeature.PointLight";

constexpr const char* kToggleFeaturePrefix = "PearlAbyssEngine.Debug.ToggleRenderingFeature.";
constexpr const char* kDebugPrefixes[] = {
    "PearlAbyssEngine.Debug.ToggleRenderingFeature.",
    "PearlAbyssEngine.Debug.",
    "PearlAbyssEngine.",
};

// How far past a name's `lea rdx` the registration block is scanned for the
// stub. A block is about 0x4A bytes; twice that is generous and still bounded.
constexpr int kDebugBlockWindow = 0x60;

// The registration function is one 0x3C43-byte block with no internal padding,
// and the anchor sits about 0xC50 bytes into it. kPrologueBackWindow (0x1000)
// would *just* reach, which is too close to call; this is sized to the measured
// function rather than to the measured distance.
constexpr int kDebugRegBackWindow = 0x4000;

// From the anchor's `lea rdx`, the second `call rel32` is the bind. The first
// is find-or-create. Used only to teach the block scanner where a block ends.
constexpr int kDebugBindFwdWindow = 0x60;

// The owner reaches the debug-mode flag as [[owner + 0x38] + 0x49]. Every
// ToggleRenderingFeature stub tests it and returns when it is zero.
constexpr int kDebugOwnerSettings = 0x38;
constexpr int kDebugModeFlag      = 0x49;

// The invariant head of a ToggleRenderingFeature stub, up to but excluding the
// id: mov rax,[rcx+0x38] / mov rdx,[rax] / cmp byte [rdx+0x49],0 / je +0x14 /
// mov rax,[rcx+0x30]. The id follows as either `B2 xx` or `33 D2` (id 0).
constexpr u8 kFeatureStubHead[] = {
    0x48, 0x8B, 0x41, 0x38, 0x48, 0x8B, 0x10, 0x80, 0x7A, 0x49, 0x00,
    0x74, 0x14, 0x48, 0x8B, 0x41, 0x30,
};
constexpr u8 kMovDlImm8 = 0xB2;
constexpr u8 kRetNear   = 0xC3;
constexpr u8 kRetImm16  = 0xC2;

// ------------------------------------------------------------------ RTTI ----
constexpr const char* kTypeDescClass  = ".?AV";   // class
constexpr const char* kTypeDescStruct = ".?AU";   // struct
constexpr int kTypeDescNameOffset = 0x10;         // MSVC TypeDescriptor::name
constexpr int kRttiScanStride     = 8;
constexpr int kRttiMaxResults     = 5000;
constexpr int kStringScanMax      = 500;
constexpr int kXrefScanMax        = 100;

}}  // namespace ch::sig

