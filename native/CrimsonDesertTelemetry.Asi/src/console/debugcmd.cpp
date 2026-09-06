#include "debugcmd.h"
#include "features.h"
#include "mem.h"
#include "signatures.h"
#include "breakpoint.h"

#include <cstring>

namespace ch { namespace dbg {

namespace {

// __try cannot share a frame with anything that needs unwinding.
using DebugStubFn = void (*)(void*);

bool CallStubGuarded(u64 stub, u64 owner) {
    __try {
        reinterpret_cast<DebugStubFn>(stub)(reinterpret_cast<void*>(owner));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Out("  EXCEPTION 0x%08X inside the stub - nothing further is trustworthy",
            GetExceptionCode());
        return false;
    }
}

bool IsNoOpStub(u64 stub) {
    u8 b[3] = {};
    if (!mem::SafeRead(reinterpret_cast<const void*>(stub), b, 3)) return false;
    if (b[0] == sig::kRetNear) return true;                       // C3
    return b[0] == sig::kRetImm16 && b[1] == 0 && b[2] == 0;       // C2 00 00
}

// The ToggleRenderingFeature stub shape, all 99 of them identical but for the
// id byte. See DEBUG_COMMANDS_20260905.md. Returns the id, or -1.
int FeatureIdOfStub(u64 stub) {
    u8 b[sizeof(sig::kFeatureStubHead) + 2] = {};
    if (!mem::SafeRead(reinterpret_cast<const void*>(stub), b, sizeof(b))) return -1;
    if (memcmp(b, sig::kFeatureStubHead, sizeof(sig::kFeatureStubHead)) != 0) return -1;
    const u8* tail = b + sizeof(sig::kFeatureStubHead);
    if (tail[0] == sig::kMovDlImm8) return tail[1];   // B2 xx
    if (tail[0] == 0x33 && tail[1] == 0xD2) return 0; // xor edx,edx  ->  id 0
    return -1;
}

// Walks the registration block that follows the name's `lea rdx`, looking for
// whichever comes first: the inline `lea rcx,<stub>`, the register-sourced
// `mov [rbp-0x10],rsi` that marks a shared callback, or the bind call that ends
// the block. Anchoring on the *call* rather than on a fixed instruction layout
// is deliberate - a second tail variant exists and a layout-shaped scan misses
// it silently. PLAYBOOK section 3.
bool ScanBlock(u64 from, u64* stubOut, bool* sharedOut) {
    *stubOut = 0;
    *sharedOut = false;
    for (int i = 0; i < sig::kDebugBlockWindow; ++i) {
        const u64 p = from + i;
        u8 b[4] = {};
        if (!mem::SafeRead(reinterpret_cast<const void*>(p), b, 4)) return false;

        // 48 89 75 F0 : mov [rbp-0x10],rsi - callback held in a register
        if (b[0] == 0x48 && b[1] == 0x89 && b[2] == 0x75 && b[3] == 0xF0) {
            *sharedOut = true;
            return true;
        }
        // 48 8D 0D d32 : lea rcx,[rip+d32] - the inline per-command stub
        if (b[0] == 0x48 && b[1] == 0x8D && b[2] == 0x0D) {
            *stubOut = mem::RipTarget(p, 3, 7);
            return *stubOut != 0;
        }
        // E8 d32 : a call. The first is find-or-create, the second is bind.
        // Reaching bind without having seen a stub means the block is shaped in
        // a way this scanner does not know; say so instead of guessing.
        if (b[0] == sig::kCallRel32 && g_game.debugBindFunc) {
            i32 rel = 0;
            if (mem::SafeRead(reinterpret_cast<const void*>(p + 1), &rel, 4) &&
                p + 5 + static_cast<i64>(rel) == g_game.debugBindFunc)
                return false;
        }
    }
    return false;
}

// No shlwapi dependency for one substring search.
bool ContainsNoCase(const char* haystack, const char* needle) {
    const size_t n = strlen(needle);
    if (n == 0) return true;
    for (const char* p = haystack; *p; ++p)
        if (_strnicmp(p, needle, n) == 0) return true;
    return false;
}

const char* BehaviourName(Behaviour b) {
    switch (b) {
        case Behaviour::ToggleKnown: return "ToggleKnown";
        case Behaviour::NoOp:        return "NoOp";
        case Behaviour::SharedStub:  return "SharedStub";
        case Behaviour::Unknown:     return "Unknown";
        default:                     return "Unresolved";
    }
}

bool IsListedNoOp(const char* shortName) {
    for (int i = 0; i < kNoOpCount; ++i)
        if (_stricmp(kNoOpFeatures[i], shortName) == 0) return true;
    return false;
}

// The part after the last '.', which is how a user will usually type it.
const char* ShortName(const char* full) {
    const char* dot = strrchr(full, '.');
    return dot ? dot + 1 : full;
}

}  // namespace

// ---------------------------------------------------------------- resolve --

Resolved Resolve(const char* name) {
    Resolved r;
    r.name = name ? name : "";
    if (r.name.empty()) { r.note = "no name given"; return r; }

    if (!g_game.moduleBase) { r.note = "module not resolved"; return r; }

    r.nameString = mem::FindString(g_game.moduleBase, g_game.moduleSize, r.name.c_str());
    if (!r.nameString) { r.note = "command name not present in this build"; return r; }

    // Every command name measured on this build has exactly one reference: the
    // `lea rdx` of its single registration. Taking [0] out of several would be a
    // guess, and the thing being guessed at is the address this code then calls.
    const auto xrefs = mem::FindXrefs(g_game.moduleBase, r.nameString, 4);
    if (xrefs.empty()) { r.note = "name string is never referenced"; return r; }
    if (xrefs.size() > 1) {
        r.behaviour = Behaviour::Unknown;
        r.note = "name has more than one reference; which one registers it is a "
                 "guess, and this code would be calling the result";
        return r;
    }
    r.xref = xrefs[0];

    bool shared = false;
    if (!ScanBlock(r.xref, &r.stub, &shared)) {
        r.behaviour = Behaviour::Unknown;
        r.note = "registration block not in a recognised shape";
        return r;
    }
    if (shared) {
        r.behaviour = Behaviour::SharedStub;
        r.note = "callback comes from a register, shared with other commands; "
                 "such stubs re-apply state rather than change it";
        return r;
    }

    if (IsNoOpStub(r.stub)) {
        r.behaviour = Behaviour::NoOp;
        r.note = "stub body is `ret` - registered but does nothing";
        return r;
    }

    r.featureId = FeatureIdOfStub(r.stub);
    if (r.featureId >= 0) {
        r.behaviour = Behaviour::ToggleKnown;
        r.note = "ToggleRenderingFeature";
        return r;
    }

    // ToggleDebugMode is the one hand-verified non-feature stub: it inverts the
    // gate byte itself and prints an on-screen message. Anything else that gets
    // here is shaped differently from everything that has been read, and is
    // refused rather than guessed at.
    if (_stricmp(ShortName(r.name.c_str()), "ToggleDebugMode") == 0) {
        r.behaviour = Behaviour::ToggleKnown;
        r.note = "debug-mode toggle, verified by hand";
        return r;
    }

    r.behaviour = Behaviour::Unknown;
    r.note = "stub shape not recognised - not classified, so not invoked";
    return r;
}

// ------------------------------------------------------------------- gate --

int ReadGate() {
    const u64 owner = g_game.debugThis;
    if (!owner) return -1;
    u64 settings = 0;
    if (!mem::SafeReadPtr(owner + sig::kDebugOwnerSettings, &settings) || !settings) return -1;
    u64 inner = 0;
    if (!mem::SafeReadPtr(settings, &inner) || !inner) return -1;
    u8 gate = 0;
    if (!mem::SafeRead(reinterpret_cast<const void*>(inner + sig::kDebugModeFlag), &gate, 1))
        return -1;
    return gate;
}

// ----------------------------------------------------------------- invoke --

namespace {

// Tries the name as given, then under each known prefix. Returns the first that
// resolves to something other than "name not present".
Resolved ResolveWithPrefixes(const char* name) {
    Resolved r = Resolve(name);
    if (r.nameString) return r;
    for (const char* prefix : sig::kDebugPrefixes) {
        std::string full = std::string(prefix) + name;
        Resolved t = Resolve(full.c_str());
        if (t.nameString) return t;
    }
    return r;
}

}  // namespace

void Invoke(const char* name) {
    if (!g_cfg.allowDebugCommands) {
        Out("Refused: [Console] AllowDebugCommands=0");
        return;
    }
    Resolved r = ResolveWithPrefixes(name);

    Out("Debug command : %s", r.name.c_str());
    Out("  name string : 0x%llX   xref : 0x%llX", r.nameString, r.xref);
    Out("  stub        : 0x%llX", r.stub);
    Out("  behaviour   : %s (%s)", BehaviourName(r.behaviour), r.note);
    if (r.featureId >= 0) Out("  feature id  : %d", r.featureId);

    if (r.behaviour != Behaviour::ToggleKnown) {
        Out("  result      : refused");
        if (r.behaviour == Behaviour::NoOp) {
            Out("  A call would return without doing anything. `ret` does not set RAX,");
            Out("  so there is no return value to read either - it would tell you nothing.");
        }
        return;
    }

    const u64 owner = g_game.debugThis;
    if (!owner) {
        Out("  owner       : NOT CAPTURED");
        Out("  result      : refused");
        bp::ReportStartup();
        Out("  Nothing can be invoked without its `this`. See the retained startup outcome above.");
        return;
    }
    Out("  owner       : 0x%llX", owner);

    const int before = ReadGate();
    const bool isDebugMode = _stricmp(ShortName(r.name.c_str()), "ToggleDebugMode") == 0;
    if (before == 0 && !isDebugMode) {
        Out("  debug gate  : 0  -> the stub will return immediately.");
        Out("  Run `debugmode` first; the game prints 'Debug mode is enabled' on screen.");
        Out("  result      : refused");
        return;
    }

    const bool ok = CallStubGuarded(r.stub, owner);
    const int after = ReadGate();

    Out("  debug gate  : %d -> %d", before, after);
    if (!isDebugMode) {
        Out("                (a precondition, not a result: for anything but");
        Out("                 ToggleDebugMode an unchanged gate says nothing)");
    }
    Out("  result      : %s", ok ? "invoked" : "exception");
    if (ok)
        Out("  'invoked' means the call returned. There is no return protocol, so");
        Out("  whether it did what its name says has to be observed on screen.");
}

void InvokeFeature(const char* idOrName) {
    if (!idOrName || !*idOrName) { Out("usage: debugfeature <id|name>"); return; }

    const char* shortName = nullptr;
    int wantId = -1;

    bool numeric = true;
    for (const char* p = idOrName; *p; ++p) if (*p < '0' || *p > '9') { numeric = false; break; }

    if (numeric) {
        wantId = atoi(idOrName);
        for (int i = 0; i < kFeatureCount; ++i)
            if (kFeatures[i].id == wantId) { shortName = kFeatures[i].name; break; }
        if (!shortName) {
            Out("No feature with id %d in the table. `debuglist` shows what there is.", wantId);
            Out("Ids are not contiguous - the enum has gaps.");
            return;
        }
    } else {
        for (int i = 0; i < kFeatureCount; ++i)
            if (_stricmp(kFeatures[i].name, idOrName) == 0) {
                shortName = kFeatures[i].name; wantId = kFeatures[i].id; break;
            }
        if (!shortName) {
            if (IsListedNoOp(idOrName)) {
                Out("'%s' is registered but its stub body is `ret`. Refused.", idOrName);
                return;
            }
            Out("'%s' is not in the feature table. `debuglist %s` to search.", idOrName, idOrName);
            return;
        }
    }

    const std::string full = std::string(sig::kToggleFeaturePrefix) + shortName;
    Resolved r = Resolve(full.c_str());

    // The table is a hint. The stub is the authority: if they disagree the table
    // has gone stale against this build, and firing would toggle the wrong thing.
    if (r.behaviour == Behaviour::ToggleKnown && r.featureId != wantId) {
        Out("REFUSED: table says %s = %d, the stub at 0x%llX says %d.",
            shortName, wantId, r.stub, r.featureId);
        Out("The generated table is stale against this build. Regenerate");
        Out("src/features.h (tools/gen-features.sh) before using debugfeature.");
        return;
    }
    Invoke(full.c_str());
}

// ------------------------------------------------------------------- list --

void List(const char* filter) {
    const bool all = !filter || !*filter;
    Out("=== ToggleRenderingFeature (%d with ids, %d no-ops) ===", kFeatureCount, kNoOpCount);
    if (!all) Out("Filter: %s", filter);
    Out("");
    int shown = 0;
    for (int i = 0; i < kFeatureCount; ++i) {
        if (!all && !ContainsNoCase(kFeatures[i].name, filter)) continue;
        Out("  %3d  %s", kFeatures[i].id, kFeatures[i].name);
        ++shown;
    }
    for (int i = 0; i < kNoOpCount; ++i) {
        if (!all && !ContainsNoCase(kNoOpFeatures[i], filter)) continue;
        Out("   --  %-52s unavailable (stub body is `ret`)", kNoOpFeatures[i]);
        ++shown;
    }
    Out("");
    Out("%d shown. Invoke with `debugfeature <id>` or `debugfeature <name>`.", shown);
    Out("Ids come from a generated table and are re-checked against the stub");
    Out("before anything is called.");
}

void Status() {
    bp::ReportStartup();
    Out("=== Debug command system ===");
    Out("  registry root ptr : 0x%llX", g_game.globalCtxPtr);
    Out("  registerCmdFunc   : 0x%llX", g_game.registerCmdFunc);
    Out("  registration fn   : 0x%llX", g_game.debugRegFunc);
    Out("  bind fn           : 0x%llX", g_game.debugBindFunc);
    Out("  owner (this)      : 0x%llX %s", g_game.debugThis,
        g_game.debugThis ? "" : "  <- NOT CAPTURED, nothing can be invoked");
    const int gate = ReadGate();
    if (gate < 0) Out("  debug mode        : unreadable");
    else          Out("  debug mode        : %d %s", gate,
                      gate ? "(open)" : "(closed - run `debugmode` first)");
    Out("  commands enabled  : %s", g_cfg.allowDebugCommands ? "yes" : "no (AllowDebugCommands=0)");
}

}}  // namespace ch::dbg

