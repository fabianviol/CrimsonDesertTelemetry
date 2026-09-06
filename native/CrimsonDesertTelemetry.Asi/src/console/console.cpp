// Window hook, console toggle, and the keystroke-synthesising `exec`.
#include "console.h"
#include "mem.h"
#include "signatures.h"

#include <cstring>

namespace ch { namespace console {

namespace {

WNDPROC g_origWndProc = nullptr;
HWND    g_hookedHwnd  = nullptr;
DWORD   g_lastToggle  = 0;
int     g_diagCount    = 0;

struct EnumCtx { DWORD pid; HWND hwnd; };

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM param) {
    auto* ctx = reinterpret_cast<EnumCtx*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    char title[256] = {};
    if (GetWindowTextA(hwnd, title, sizeof(title)) <= 0) return TRUE;
    ctx->hwnd = hwnd;
    return FALSE;
}

// __try cannot live in a function with C++ objects that need unwinding, so each
// guarded call gets its own tiny wrapper.
using Fn1 = u64 (*)(void*);
using Fn2 = u64 (*)(void*, void*);
using Fn0OnThis = u64 (*)(void*);

bool CallGuarded1(u64 fn, u64 arg, u64* result) {
    __try {
        const u64 r = reinterpret_cast<Fn1>(fn)(reinterpret_cast<void*>(arg));
        if (result) *result = r;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("  Call1 EXCEPTION 0x%08X", GetExceptionCode());
        return false;
    }
}

bool CallGuarded2(u64 fn, u64 a, u64 b, u64* result) {
    __try {
        const u64 r = reinterpret_cast<Fn2>(fn)(reinterpret_cast<void*>(a),
                                                reinterpret_cast<void*>(b));
        if (result) *result = r;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("  Notify EXCEPTION 0x%08X", GetExceptionCode());
        return false;
    }
}

// obj->vtable[slot/8](obj)
bool VCallGuarded(u64 obj, int slotByteOffset, u64* result) {
    __try {
        const u64 vt = *reinterpret_cast<u64*>(obj);
        const u64 fn = *reinterpret_cast<u64*>(vt + slotByteOffset);
        const u64 r  = reinterpret_cast<Fn0OnThis>(fn)(reinterpret_cast<void*>(obj));
        if (result) *result = r;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("  VCall EXCEPTION 0x%08X", GetExceptionCode());
        return false;
    }
}

// The hide path from the original, reproduced. Everything it touches is pinned
// to the April 2026 build - see signatures.h. Disabled by HideNotification=0.
void FireHideNotification(u64 uiObject) {
    const u64 showFn = g_game.showConsoleFunc;
    if (!showFn || !uiObject) return;

    const u64 ptrA   = mem::RipTarget(showFn + sig::kShowFnPtrA - 3,
                                      3, sig::kShowFnPtrANext - (sig::kShowFnPtrA - 3));
    const u64 ptrB   = mem::RipTarget(showFn + sig::kShowFnPtrB - 3,
                                      3, sig::kShowFnPtrBNext - (sig::kShowFnPtrB - 3));
    const u64 notify = mem::RipTarget(showFn + sig::kShowFnNotify - 3,
                                      3, sig::kShowFnNotifyNext - (sig::kShowFnNotify - 3));

    u64 receiver = 0, state = 0;
    mem::SafeReadPtr(uiObject + sig::kUiReceiverOffset, &receiver);
    mem::SafeReadPtr(uiObject + sig::kUiStateOffset, &state);

    Log("  Firing hide notification: func=0x%llX receiver=0x%llX", notify, receiver);
    const float f30 = static_cast<float>(static_cast<i32>(state & 0xFFFFFFFFu));
    const float f34 = static_cast<float>(static_cast<i32>(state >> 32));
    Log("    ptrA=0x%llX ptrB=0x%llX visible=0 f30=%.1f f34=%.1f", ptrA, ptrB, f30, f34);

    if (!receiver || !notify) {
        Log("  -> skipped notification (receiver=0x%llX func=0x%llX)", receiver, notify);
        return;
    }

    // 0x28-byte payload, laid out as the original built it on the stack.
    struct Payload {
        u64   a;      // ptrB
        u64   b;      // ptrA
        u64   zero;
        u8    flags[4];
        float f30;
        float f34;
        u32   tail;
    } payload{};
    payload.a    = ptrB;
    payload.b    = ptrA;
    payload.f30  = f30;
    payload.f34  = f34;

    u64 ignored = 0;
    const bool ok = CallGuarded2(notify, receiver, reinterpret_cast<u64>(&payload), &ignored);
    Log(ok ? "  -> notification OK" : "  -> notification FAILED");
}

LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        if (wp == sig::kToggleKey) {
            const DWORD now = GetTickCount();
            if (now - g_lastToggle < sig::kToggleDebounceMs) return 0;
            g_lastToggle = now;
            Toggle();
            return 0;   // swallow, so the game never sees the key
        }
        if (wp == sig::kDiagnosticsKey) Diagnostics();
    } else if (msg == WM_CHAR) {
        // Swallow the character the toggle key would otherwise produce.
        if (wp == 0x60 || wp == 0x7E) return 0;
    }
    return CallWindowProcW(g_origWndProc, hwnd, msg, wp, lp);
}

}  // namespace

HWND FindGameWindow() {
    EnumCtx ctx{ GetCurrentProcessId(), nullptr };
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.hwnd;
}

bool HookWndProc(HWND hwnd) {
    if (g_hookedHwnd || !hwnd) return false;
    g_origWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HookedWndProc)));
    if (!g_origWndProc) return false;
    g_hookedHwnd = hwnd;
    return true;
}

void UnhookWndProc() {
    if (!g_hookedHwnd || !g_origWndProc) return;
    SetWindowLongPtrW(g_hookedHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_origWndProc));
    g_hookedHwnd  = nullptr;
    g_origWndProc = nullptr;
}

void Toggle() {
    if (!g_game.parentThis) {
        Log("~ pressed but parentThis not captured yet");
        return;
    }

    u64 inner = 0, uiObject = 0;
    if (mem::SafeReadPtr(g_game.parentThis + sig::kParentThisIndirect, &inner) && inner)
        VCallGuarded(inner, sig::kUiVtableSlot, &uiObject);

    if (!g_game.consoleVisible) {
        if (!g_game.showConsoleFunc) {
            Log("~ pressed but no show handler was resolved");
            return;
        }
        Log("~ pressed: showing console");
        u64 r = 0;
        if (CallGuarded1(g_game.showConsoleFunc, g_game.parentThis, &r)) {
            g_game.consoleVisible = true;
            Log("  -> show OK");
        } else {
            Log("  -> show FAILED");
        }
        return;
    }

    Log("~ pressed: hiding console");
    u64 r = 0;
    const bool ok = g_game.hideConsoleFunc &&
                    CallGuarded1(g_game.hideConsoleFunc, g_game.parentThis, &r);
    Log(ok ? "  -> hideHandler OK" : "  -> hideHandler FAILED");

    if (g_cfg.hideNotification && uiObject) FireHideNotification(uiObject);
    else if (!g_cfg.hideNotification)
        Log("  -> notification skipped ([Console] HideNotification=0)");

    g_game.consoleVisible = false;
}

void Diagnostics() {
    Log("=== F11 diagnostics (#%d) ===", ++g_diagCount);
    Log("  parentThis=0x%llX visible=%d", g_game.parentThis, g_game.consoleVisible ? 1 : 0);
    Log("  show=0x%llX hide=0x%llX", g_game.showConsoleFunc, g_game.hideConsoleFunc);
    if (g_game.globalCtxPtr) {
        u64 ctx = 0;
        mem::SafeReadPtr(g_game.globalCtxPtr, &ctx);
        Log("  ctx=0x%llX", ctx);
    }
    Log("  window=0x%llX hooked=%d", reinterpret_cast<u64>(g_game.window),
        g_hookedHwnd ? 1 : 0);
}

bool ExecConsoleCommand(const char* text) {
    if (!text || !*text) {
        Out("Usage: exec <console_command>");
        return false;
    }
    HWND hwnd = g_game.window ? g_game.window : FindGameWindow();
    if (!hwnd) {
        Out("ERROR: Game window not found");
        return false;
    }
    Out("=== Executing console command: %s ===", text);

    // Clear whatever is on the input line: Home, Shift+End, Delete.
    PostMessageW(hwnd, WM_KEYDOWN, VK_HOME,  sig::kLpHomeDown);  Sleep(5);
    PostMessageW(hwnd, WM_KEYUP,   VK_HOME,  sig::kLpHomeUp);    Sleep(5);
    PostMessageW(hwnd, WM_KEYDOWN, VK_SHIFT, sig::kLpShiftDown); Sleep(2);
    PostMessageW(hwnd, WM_KEYDOWN, VK_END,   sig::kLpEndDown);   Sleep(5);
    PostMessageW(hwnd, WM_KEYUP,   VK_END,   sig::kLpEndUp);     Sleep(2);
    PostMessageW(hwnd, WM_KEYUP,   VK_SHIFT, sig::kLpShiftUp);   Sleep(5);
    PostMessageW(hwnd, WM_KEYDOWN, VK_DELETE, sig::kLpDelDown);  Sleep(5);
    PostMessageW(hwnd, WM_KEYUP,   VK_DELETE, sig::kLpDelUp);    Sleep(10);

    size_t n = 0;
    for (const char* p = text; *p; ++p, ++n) {
        PostMessageW(hwnd, WM_CHAR, static_cast<WPARAM>(static_cast<unsigned char>(*p)), 1);
        Sleep(2);
    }
    Sleep(20);
    PostMessageW(hwnd, WM_KEYDOWN, VK_RETURN, sig::kLpEnterDown); Sleep(10);
    PostMessageW(hwnd, WM_KEYUP,   VK_RETURN, sig::kLpEnterUp);

    Out("  -> Sent %zu chars + Enter", n);
    Out("  (this only lands if the console is open and the window has focus)");
    return true;
}

}}  // namespace ch::console

