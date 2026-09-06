// Discovery: find the console's show/hide handlers and its `this` pointer by
// walking the machine code around four engine strings.
//
// Every constant used here lives in signatures.h. Read ANALYSIS.md section 5
// before changing any of them.
#include "discovery.h"
#include "mem.h"
#include "rtti.h"
#include "signatures.h"
#include "breakpoint.h"

#include <cstring>

namespace ch { namespace discovery {

namespace {

// ---- small shared helpers --------------------------------------------------

// A gate is `(74|90) ?? 45 33 C9 45 33 C0`. Only the six register bytes are
// matched, so a changed jump displacement does not hide it.
u64 FindGateNear(u64 from, int window) {
    for (int back = 4; back < window; ++back) {
        const u64 p = from - back;
        u8 b[8] = {};
        if (!mem::SafeRead(reinterpret_cast<const void*>(p), b, 8)) continue;
        if (b[0] != 0x74 && b[0] != 0x90) continue;
        if (memcmp(b + 2, sig::kGateTail, sizeof(sig::kGateTail)) == 0) return p;
    }
    return 0;
}

bool NopGate(u64 gate, const char* which) {
    u8 cur[2] = {};
    if (!mem::SafeRead(reinterpret_cast<const void*>(gate), cur, 2)) return false;
    if (cur[0] == 0x90) {
        Log("%s at 0x%llX (bytes: %02X %02X) [already patched]", which, gate, cur[0], cur[1]);
        return true;
    }
    Log("%s at 0x%llX (bytes: %02X %02X)", which, gate, cur[0], cur[1]);
    if (!g_cfg.patchGates) {
        Log("  -> not patched ([Console] PatchGates=0)");
        return false;
    }
    const u8 nops[2] = { 0x90, 0x90 };
    return mem::Patch(gate, nops, 2, which);
}

// Forward search for `lea reg,[rip+d32]` starting at `from`. Returns the
// instruction address in `atOut` and its target as the result.
u64 FindLeaRipForward(u64 from, int window, u8 wantModRm, u64* atOut) {
    for (int i = 0; i < window; ++i) {
        const u64 p = from + i;
        u8 b[3] = {};
        if (!mem::SafeRead(reinterpret_cast<const void*>(p), b, 3)) continue;
        if ((b[0] != 0x48 && b[0] != 0x4C) || b[1] != sig::kLeaRip) continue;
        if (wantModRm && b[2] != wantModRm) continue;
        if ((b[2] & 0xC7) != 0x05) continue;
        if (atOut) *atOut = p;
        return mem::RipTarget(p, 3, 7);
    }
    return 0;
}

u64 FindCallForward(u64 from, int window) {
    for (int i = 0; i < window; ++i) {
        const u64 p = from + i;
        u8 op = 0;
        if (!mem::SafeRead(reinterpret_cast<const void*>(p), &op, 1)) continue;
        if (op != sig::kCallRel32) continue;
        i32 rel = 0;
        if (!mem::SafeRead(reinterpret_cast<const void*>(p + 1), &rel, 4)) continue;
        return p + 5 + static_cast<i64>(rel);
    }
    return 0;
}

// `mov rcx,[rip+d32]` behind the gate: the command-system context pointer.
u64 FindCtxPointerBehind(u64 gate, int window) {
    for (int back = 1; back < window; ++back) {
        const u64 p = gate - back;
        u8 b[3] = {};
        if (!mem::SafeRead(reinterpret_cast<const void*>(p), b, 3)) continue;
        if (b[0] != 0x48 || b[1] != sig::kMovRegRip || b[2] != sig::kModRmRcx) continue;
        return mem::RipTarget(p, 3, 7);
    }
    return 0;
}

using RegisterCmdFn = u64 (*)(void*, const char*, u64, u64);

u64 CallRegisterCmd(u64 fn, u64 ctx, const char* name) {
    __try {
        return reinterpret_cast<RegisterCmdFn>(fn)(reinterpret_cast<void*>(ctx), name, 0, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("RegisterCmd(%s) EXCEPTION 0x%08X", name, GetExceptionCode());
        return 0;
    }
}

void PatchDevVar(const char* name, u64 strAddr) {
    if (!strAddr) return;
    const auto xrefs = mem::FindXrefs(g_game.moduleBase, strAddr, sig::kMaxDevVarXrefs);
    for (u64 x : xrefs) {
        for (int back = 0; back < sig::kDevVarBackWindow; ++back) {
            const u64 p = x - back;
            u8 b[7] = {};
            if (!mem::SafeRead(reinterpret_cast<const void*>(p), b, 7)) continue;
            if (b[0] != sig::kCmpDwordRip[0] || b[1] != sig::kCmpDwordRip[1]) continue;
            if (b[6] != sig::kCmpDwordImm) continue;

            const u64 var = mem::RipTarget(p, 2, 7);
            i32 before = 0;
            if (!mem::SafeRead(reinterpret_cast<const void*>(var), &before, 4)) continue;
            Log("%s variable at 0x%llX = %d", name, var, before);
            if (!g_cfg.patchDevFlags) {
                Log("  -> not patched ([Console] PatchDevFlags=0)");
                return;
            }
            const i32 one = 1;
            if (mem::Patch(var, &one, 4, name)) {
                i32 after = 0;
                mem::SafeRead(reinterpret_cast<const void*>(var), &after, 4);
                Log("  -> Set %s = 1 (read-back: %d)", name, after);
            }
            return;
        }
    }
    Log("%s variable NOT found via CMP pattern", name);
}

}  // namespace

// ---------------------------------------------------------------------------

void EarlyPatch() {
    const bp::Outcome initial = g_cfg.earlyBreakpoint ? bp::Outcome::RefusedNoModule
                                                    : bp::Outcome::Disabled;
    bp::SetOutcome(bp::kConsole, initial);
    bp::SetOutcome(bp::kDebugCommands, initial);
    u64 base = 0, size = 0;
    if (!mem::GetModuleRange(nullptr, &base, &size)) return;

    // Publish the module range NOW, not in Init().
    //
    // This runs inline from DllMain; Init() runs on a thread that sleeps five
    // seconds first. Everything below used only the locals, which was harmless
    // until bp::Arm() gained an executable-section check - that check reads
    // g_game.moduleBase, saw zero, and silently refused both startup slots. The
    // owner pointers were then never captured, and because OpenLog() has not run
    // yet the refusal was not even recorded. A resolved debugRegFunc is not
    // evidence that its breakpoint was armed. Diagnosed by Codex in
    // ../STARTUP_CAPTURE_REGRESSION_20260905.md.
    g_game.moduleBase = base;
    g_game.moduleSize = size;

    // Pass 1: NOP every full-pattern gate in every executable section.
    int patched = 0;
    for (const auto& sec : mem::CodeSections(base)) {
        if (sec.size() < sizeof(sig::kGateFull)) continue;
        const u8* p = reinterpret_cast<const u8*>(sec.start);
        const u64 n = sec.size() - sizeof(sig::kGateFull);
        for (u64 i = 0; i < n; ++i) {
            if (p[i] != sig::kGateFull[0]) continue;
            if (memcmp(p + i, sig::kGateFull, sizeof(sig::kGateFull)) != 0) continue;
            if (!g_cfg.patchGates) { ++patched; continue; }
            const u8 nops[2] = { 0x90, 0x90 };
            DWORD old = 0;
            void* at = reinterpret_cast<void*>(sec.start + i);
            if (VirtualProtect(at, 2, PAGE_EXECUTE_READWRITE, &old)) {
                memcpy(at, nops, 2);
                DWORD ignored = 0;
                VirtualProtect(at, 2, old, &ignored);
                ++patched;
            }
        }
    }
    g_game.earlyPatchCount = patched;
    g_game.earlyPatched    = patched > 0;

    if (!g_cfg.earlyBreakpoint) return;

    // Preserve missing-string/xref/prologue failures until logging is available.
    bp::SetOutcome(bp::kConsole, bp::Outcome::NoTarget);
    bp::SetOutcome(bp::kDebugCommands, bp::Outcome::NoTarget);

    // Pass 2: arm the one-shot breakpoints that capture `this`.
    //
    // Both are resolved the same way - engine string, its single xref, the start
    // of the enclosing function - because names survive game updates where
    // addresses do not.
    const u64 strShow = mem::FindString(base, size, sig::kShowDebugConsole);
    if (strShow) {
        const auto xrefs = mem::FindXrefs(base, strShow, 1);
        if (!xrefs.empty()) {
            const u64 fn = mem::FindFunctionStart(xrefs[0], sig::kPrologueBackWindow);
            bp::Arm(bp::kConsole, fn, &g_game.parentThis, "console handlers");
        }
    }

    // The function that registers all 208 PearlAbyssEngine.Debug.* commands.
    // Verified statically: it does `mov rbx,rcx` once and passes that same rbx
    // as the owner to every one of its 208 bind calls, so this single capture is
    // the owner for all of them. DEBUG_COMMANDS_20260905.md, "Verified".
    const u64 strAnchor = mem::FindString(base, size, sig::kDebugRegAnchor);
    if (strAnchor) {
        const auto xrefs = mem::FindXrefs(base, strAnchor, 1);
        if (!xrefs.empty()) {
            const u64 fn = mem::FindFunctionStart(xrefs[0], sig::kDebugRegBackWindow);
            g_game.debugRegFunc = fn;
            bp::Arm(bp::kDebugCommands, fn, &g_game.debugThis, "debug command registration");
        }
    }
}

void DisarmEarlyBreakpoint() { bp::Shutdown(); }

bool Discover() {
    const u64 base = g_game.moduleBase;
    const u64 size = g_game.moduleSize;

    // ---------------------------------------------------- phase 1: strings --
    Log("--- Phase 1: String Discovery ---");
    g_game.strShow   = mem::FindString(base, size, sig::kShowDebugConsole);
    Log("ShowDebugConsole string: 0x%llX", g_game.strShow);
    g_game.strHide   = mem::FindString(base, size, sig::kHideDebugConsole);
    Log("HideDebugConsole string: 0x%llX", g_game.strHide);
    g_game.strToggle = mem::FindString(base, size, sig::kToggleDebugMode);
    Log("ToggleDebugMode string: 0x%llX", g_game.strToggle);
    g_game.strEch    = mem::FindString(base, size, sig::kEngineConsoleCmd);
    Log("EngineConsoleCommandHandler string: 0x%llX", g_game.strEch);

    // ----------------------------------- phase 1b: the Debug.* command system --
    //
    // A second, separate command system. Its names cannot be typed into the chat
    // console - the chat handler never looks in this registry - so the only way
    // in is to call the per-command stub with the owner captured by the second
    // one-shot breakpoint. ../DEBUG_COMMANDS_20260905.md has the derivation.
    //
    // Deliberately placed before the ShowDebugConsole check below. The two
    // systems share nothing but this module: if a future build renames the
    // console strings, the debug commands should still be reachable, and the
    // failure should name the thing that actually failed.
    Log("--- Phase 1b: PearlAbyssEngine.Debug.* command system ---");
    {
        const u64 anchor = mem::FindString(base, size, sig::kDebugRegAnchor);
        Log("Anchor string: 0x%llX (%s)", anchor, sig::kDebugRegAnchor);
        if (anchor) {
            const auto xrefs = mem::FindXrefs(base, anchor, 2);
            Log("  xrefs: %zu", xrefs.size());
            if (!xrefs.empty()) {
                if (!g_game.debugRegFunc)
                    g_game.debugRegFunc =
                        mem::FindFunctionStart(xrefs[0], sig::kDebugRegBackWindow);
                Log("  registration function: 0x%llX", g_game.debugRegFunc);

                // The block is: ... call <find-or-create> ... call <bind>.
                // Take the second call as bind. It is only used to recognise the
                // end of a registration block when a stub cannot be found.
                int seen = 0;
                for (int i = 0; i < sig::kDebugBindFwdWindow; ++i) {
                    const u64 p = xrefs[0] + i;
                    u8 op = 0;
                    if (!mem::SafeRead(reinterpret_cast<const void*>(p), &op, 1)) break;
                    if (op != sig::kCallRel32) continue;
                    i32 rel = 0;
                    if (!mem::SafeRead(reinterpret_cast<const void*>(p + 1), &rel, 4)) break;
                    const u64 target = p + 5 + static_cast<i64>(rel);
                    if (++seen == 2) { g_game.debugBindFunc = target; break; }
                    i += 4;
                }
                Log("  bind function: 0x%llX", g_game.debugBindFunc);
            }
        }
        if (!g_game.debugRegFunc) {
            Log("  NOT FOUND - debugcmd/debugfeature will refuse. The anchor name");
            Log("  may have been renamed by a game update; check DEBUG_COMMANDS_*.md");
        }
        Log("  owner captured so far: 0x%llX", g_game.debugThis);
        bp::ReportSlot(bp::kDebugCommands);
    }


    if (!g_game.strShow) {
        Log("CRITICAL: Could not find ShowDebugConsole string in memory!");
        Log("  The engine strings are the one anchor that has survived every");
        Log("  update so far. If this fails, the executable is not the game,");
        Log("  or the string set was renamed - check ANALYSIS.md section 5.");
        return false;
    }

    // ------------------------------------------------------ phase 2: xrefs --
    Log("--- Phase 2: Cross-Reference Analysis ---");
    const auto showXrefs = mem::FindXrefs(base, g_game.strShow);
    Log("ShowDebugConsole xrefs: %zu", showXrefs.size());
    for (size_t i = 0; i < showXrefs.size(); ++i)
        Log("  xref[%zu]: 0x%llX", i, showXrefs[i]);

    const auto hideXrefs   = g_game.strHide   ? mem::FindXrefs(base, g_game.strHide)   : std::vector<u64>{};
    const auto toggleXrefs = g_game.strToggle ? mem::FindXrefs(base, g_game.strToggle) : std::vector<u64>{};
    Log("HideDebugConsole xrefs: %zu", hideXrefs.size());
    Log("ToggleDebugMode xrefs: %zu", toggleXrefs.size());

    const u64 devStr = mem::FindString(base, size, "_isDev");
    if (devStr) {
        Log("_isDev string: 0x%llX", devStr);
        const auto devXrefs = mem::FindXrefs(base, devStr);
        Log("_isDev xrefs: %zu", devXrefs.size());
        for (size_t i = 0; i < devXrefs.size() && i < 8; ++i)
            Log("  xref[%zu]: 0x%llX", i, devXrefs[i]);
    }

    if (showXrefs.empty()) {
        Log("No ShowDebugConsole xrefs. The string exists but nothing references");
        Log("it in a form we recognise. See ANALYSIS.md section 7 - this is the");
        Log("failure mode a register-allocation change produces.");
        return false;
    }

    // ------------------------------------------ phase 3: address extraction --
    Log("--- Phase 3: Extract addresses from code ---");
    const u64 R = showXrefs[0];
    LogHexDump(reinterpret_cast<void*>(R - 0x40), 0x18, "ShowDebugConsole xref context");

    g_game.gate1 = FindGateNear(R, sig::kGateBackWindow);
    if (g_game.gate1) {
        NopGate(g_game.gate1, "Gate1");
        g_game.globalCtxPtr = FindCtxPointerBehind(g_game.gate1, sig::kCtxBackWindow);
        if (g_game.globalCtxPtr) {
            u64 ctx = 0;
            mem::SafeReadPtr(g_game.globalCtxPtr, &ctx);
            Log("Global context ptr: [0x%llX] = 0x%llX", g_game.globalCtxPtr, ctx);
        }
    } else {
        Log("Gate1 NOT found within 0x%X bytes of the xref", sig::kGateBackWindow);
    }

    u8 after[1] = {};
    if (mem::SafeRead(reinterpret_cast<const void*>(R + 7), after, 1) &&
        after[0] == sig::kCallRel32) {
        i32 rel = 0;
        if (mem::SafeRead(reinterpret_cast<const void*>(R + 8), &rel, 4)) {
            g_game.registerCmdFunc = R + 12 + static_cast<i64>(rel);
            Log("RegisterCommandFunc = 0x%llX", g_game.registerCmdFunc);
        }
    }

    u64 leaAt = 0;
    g_game.showConsoleFunc = FindLeaRipForward(R, sig::kHandlerFwdWindow, sig::kModRmRcx, &leaAt);
    if (g_game.showConsoleFunc) {
        Log("Show handler = 0x%llX", g_game.showConsoleFunc);
        g_game.setCallbackFunc = FindCallForward(leaAt + 7, sig::kSetCallbackWindow);
        if (g_game.setCallbackFunc) Log("SetCallback = 0x%llX", g_game.setCallbackFunc);
    }

    if (!hideXrefs.empty()) {
        const u64 H = hideXrefs[0];
        g_game.gate2 = FindGateNear(H, sig::kGateBackWindow);
        if (g_game.gate2) NopGate(g_game.gate2, "Gate2");
        g_game.hideConsoleFunc = FindLeaRipForward(H, sig::kHandlerFwdWindow, sig::kModRmRcx, nullptr);
        if (g_game.hideConsoleFunc) Log("Hide handler = 0x%llX", g_game.hideConsoleFunc);
    }

    if (g_game.gate1) {
        g_game.parentFuncAddr = mem::FindFunctionStart(g_game.gate1, sig::kPrologueBackWindow);
        if (g_game.parentFuncAddr) {
            Log("Parent function at 0x%llX", g_game.parentFuncAddr);
            LogHexDump(reinterpret_cast<void*>(g_game.parentFuncAddr), 8, "Parent function prologue");
        }
    }
    if (g_game.showConsoleFunc)
        LogHexDump(reinterpret_cast<void*>(g_game.showConsoleFunc), 8, "Show handler prologue");
    if (g_game.hideConsoleFunc)
        LogHexDump(reinterpret_cast<void*>(g_game.hideConsoleFunc), 8, "Hide handler prologue");

    if (!toggleXrefs.empty()) {
        g_game.toggleFunc = FindLeaRipForward(toggleXrefs[0], sig::kHandlerFwdWindow,
                                              sig::kModRmRcx, nullptr);
        if (g_game.toggleFunc) Log("ToggleDebugMode handler = 0x%llX", g_game.toggleFunc);
    }

    // --------------------------------------- phase 4: command-system detail --
    Log("--- Phase 4: Command System Deep Inspection ---");
    u64 ctx = 0;
    if (g_game.globalCtxPtr && mem::SafeReadPtr(g_game.globalCtxPtr, &ctx) && ctx) {
        Log("Command system context = 0x%llX", ctx);
        const std::string ctxName = rtti::NameForObject(ctx);
        if (!ctxName.empty()) Log("  RTTI: %s", ctxName.c_str());

        if (g_game.registerCmdFunc) {
            g_game.cmdObjShow = CallRegisterCmd(g_game.registerCmdFunc, ctx, sig::kShowDebugConsole);
            if (g_game.cmdObjShow) {
                Log("Show cmdObj = 0x%llX", g_game.cmdObjShow);
                LogHexDump(reinterpret_cast<void*>(g_game.cmdObjShow), 0x0C, "Show cmdObj memory");
            }
            g_game.cmdObjHide = CallRegisterCmd(g_game.registerCmdFunc, ctx, sig::kHideDebugConsole);
            if (g_game.cmdObjHide) Log("Hide cmdObj = 0x%llX", g_game.cmdObjHide);
        }
    }

    if (g_cfg.verboseDiscovery && g_game.strEch) {
        const auto echXrefs = mem::FindXrefs(base, g_game.strEch);
        Log("EngineConsoleCommandHandler xrefs: %zu", echXrefs.size());
        for (size_t i = 0; i < echXrefs.size() && i < sig::kMaxEchXrefs; ++i) {
            Log("  xref[%zu]: 0x%llX", i, echXrefs[i]);
            LogHexDump(reinterpret_cast<void*>(echXrefs[i] - 0x20), 0x10, "  ECH xref context");
            const u64 fn = mem::FindFunctionStart(echXrefs[i], sig::kEchPrologueWindow);
            if (fn) {
                Log("  ECH dispatch function[%zu] at 0x%llX", i, fn);
                LogHexDump(reinterpret_cast<void*>(fn), 8, "    ECH prologue");
                if (!g_game.echDispatch) g_game.echDispatch = fn;
            }
        }
    }

    if (g_cfg.verboseDiscovery && g_game.parentFuncAddr) {
        int gates = 0;
        for (u64 p = g_game.parentFuncAddr; p < g_game.parentFuncAddr + sig::kParentGateSpan; ++p) {
            u8 b[11] = {};
            if (!mem::SafeRead(reinterpret_cast<const void*>(p), b, 11)) continue;
            const bool unp = memcmp(b, sig::kGateFull, 11) == 0;
            const bool pat = memcmp(b, sig::kGateFullPatched, 11) == 0;
            if (!unp && !pat) continue;
            Log("  Gate at 0x%llX (bytes: %02X %02X) %s", p, b[0], b[1],
                pat ? "[patched]" : "[UNPATCHED]");
            ++gates;
            p += 10;
        }
        Log("Total gates in parent function: %d", gates);
    }

    DisarmEarlyBreakpoint();
    bp::ReportStartup();
    Log("VEH parentThis capture: 0x%llX", g_game.parentThis);
    Log("VEH debugThis  capture: 0x%llX", g_game.debugThis);
    if (!g_game.debugThis || !g_game.parentThis)
        Log("  Missing owners: use the saved last-arm/completion diagnostics above;"
            " a resolved function alone does not prove it was armed.");

    if (g_cfg.verboseDiscovery && g_game.cmdObjShow && g_game.showConsoleFunc) {
        Log("--- Scanning cmdObj for stored handler address 0x%llX ---", g_game.showConsoleFunc);
        for (int off = 0; off < sig::kCmdObjScanSize; off += 8) {
            u64 v = 0;
            if (!mem::SafeReadPtr(g_game.cmdObjShow + off, &v)) continue;
            if (v == g_game.showConsoleFunc) {
                Log("  FOUND handler at cmdObj+0x%X", off);
                for (int n = -0x20; n <= 0x20; n += 8) {
                    u64 nb = 0;
                    if (mem::SafeReadPtr(g_game.cmdObjShow + off + n, &nb))
                        Log("    cmdObj+0x%X = 0x%llX", off + n, nb);
                }
                continue;
            }
            if (!mem::PlausiblePointer(v) || off < 0x18) continue;
            for (int inner = 0; inner < sig::kCmdObjInnerScan; inner += 8) {
                u64 iv = 0;
                if (!mem::SafeReadPtr(v + inner, &iv)) break;
                if (iv != g_game.showConsoleFunc) continue;
                Log("  FOUND handler at [cmdObj+0x%X]+0x%X", off, inner);
                LogHexDump(reinterpret_cast<void*>(v), 0x10, "    handler container");
                break;
            }
        }
    }

    // ------------------------------------------- phase 5: dev flag patching --
    Log("--- Phase 5: Dev flag patching ---");
    for (const char* name : sig::kDevVarNames) {
        const u64 s = mem::FindString(base, size, name);
        if (!s) continue;
        Log("Found dev var string '%s' at 0x%llX", name, s);
        PatchDevVar(name, s);
    }

    // ---------------------------------------- phase 6: dispatcher name scan --
    if (g_cfg.verboseDiscovery) {
        Log("--- Phase 6: ExecuteCommand search ---");
        for (const char* name : sig::kDispatcherNames) {
            const u64 s = mem::FindString(base, size, name);
            if (!s) continue;
            Log("Found '%s' at 0x%llX", name, s);
            const auto xs = mem::FindXrefs(base, s, sig::kMaxEchXrefs);
            for (u64 x : xs) Log("  xref: 0x%llX", x);
        }
    }

    // ------------------------------------------------------------- summary --
    Log("--- Summary ---");
    Log("g_showConsoleFunc  = 0x%llX", g_game.showConsoleFunc);
    Log("g_hideConsoleFunc  = 0x%llX", g_game.hideConsoleFunc);
    Log("g_toggleFunc       = 0x%llX", g_game.toggleFunc);
    Log("g_gates            = 0x%llX / 0x%llX", g_game.gate1, g_game.gate2);
    Log("g_globalCtxPtrAddr = 0x%llX", g_game.globalCtxPtr);
    Log("g_registerCmdFunc  = 0x%llX", g_game.registerCmdFunc);
    Log("g_setCallbackFunc  = 0x%llX", g_game.setCallbackFunc);
    Log("g_parentFuncAddr   = 0x%llX", g_game.parentFuncAddr);
    Log("g_parentThis       = 0x%llX", g_game.parentThis);
    Log("g_cmdObjShow/Hide  = 0x%llX / 0x%llX", g_game.cmdObjShow, g_game.cmdObjHide);
    Log("module base/size   = 0x%llX / %llu (0x%llX)", base, size, size);
    Log("NOTE: these are RVAs for THIS build only. Compare against "
        "CURRENT_STATE.md section 9 before reusing them.");

    return g_game.showConsoleFunc != 0;
}

void DumpRegisteredCommands() {
    if (!g_game.registerCmdFunc || !g_game.globalCtxPtr) return;
    u64 ctx = 0;
    if (!mem::SafeReadPtr(g_game.globalCtxPtr, &ctx) || !ctx) return;

    // The command names the engine itself carries. Registering a name that
    // already exists returns the existing object, which is how the original
    // enumerates them; nothing new is created.
    static const char* const kKnown[] = {
        sig::kShowDebugConsole, sig::kHideDebugConsole, sig::kToggleDebugMode,
    };

    const std::string path = GameDirectory() + "\\ConsoleCommands.txt";
    FILE* f = fopen(path.c_str(), "w");
    if (!f) {
        Log("ERROR: Could not write commands file: %s", path.c_str());
        return;
    }
    fprintf(f, "=== Crimson Desert Console Commands ===\n");
    size_t n = 0;
    for (const char* name : kKnown) {
        const u64 obj = CallRegisterCmd(g_game.registerCmdFunc, ctx, name);
        fprintf(f, "%-40s cmdObj=0x%llX\n", name, obj);
        if (obj) ++n;
    }
    fprintf(f, "Total: %zu registered commands\n", n);
    fclose(f);
    Log("Found %zu registered commands", n);
    Log("Commands written to: %s", path.c_str());
}

}}  // namespace ch::discovery

