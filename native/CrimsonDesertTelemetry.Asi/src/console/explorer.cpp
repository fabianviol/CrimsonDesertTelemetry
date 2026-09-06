// The file-driven memory explorer.
//
// Drop lines into <game dir>\inject_cmd.txt WHILE THE GAME IS RUNNING; results
// appear in inject_result.txt. The command file is deleted after it is read, so
// each drop runs once.
#include "explorer.h"
#include "console.h"
#include "mem.h"
#include "rtti.h"
#include "debugcmd.h"
#include "breakpoint.h"
#include "signatures.h"
#include "manylights.h"

#include <cctype>
#include <cstring>
#include <cstdlib>

namespace ch { namespace explorer {

namespace {

volatile bool g_running = false;
HANDLE        g_thread  = nullptr;
std::string   g_cmdPath;
std::string   g_resultPath;

u64 ParseHex(const char* s) {
    if (!s || !*s) return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    return _strtoui64(s, nullptr, 16);
}

// --- guarded calls (no C++ objects allowed in a __try frame) ---------------

using Fn4 = u64 (*)(u64, u64, u64, u64);
using Fn5 = u64 (*)(u64, u64, u64, u64);

bool CallGuarded(u64 fn, u64 a1, u64 a2, u64 a3, u64 a4, u64* out, DWORD* code) {
    __try {
        *out = reinterpret_cast<Fn4>(fn)(a1, a2, a3, a4);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *code = GetExceptionCode();
        return false;
    }
}

bool VCallGuarded(u64 obj, u64 fn, u64 a1, u64 a2, u64 a3, u64* out, DWORD* code) {
    __try {
        *out = reinterpret_cast<Fn5>(fn)(obj, a1, a2, a3);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *code = GetExceptionCode();
        return false;
    }
}

bool WriteGuarded(u64 addr, const u8* bytes, size_t n) {
    __try {
        memcpy(reinterpret_cast<void*>(addr), bytes, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// --- breakpoint capture -----------------------------------------------------

// Accepts either form and says which it took. An RVA is smaller than the module
// (0x16F1F000); a VA lies inside [base, base+size). The two ranges cannot
// overlap while the module loads at 0x140000000, so no prefix is needed - but
// both are always printed, because every document in this repository records
// addresses as RVAs and every log line here has to be comparable to them.
bool ResolveTargetAddress(u64 value, u64* vaOut, u64* rvaOut) {
    const u64 base = g_game.moduleBase, size = g_game.moduleSize;
    if (!base || !size) return false;
    if (value < size)                              { *rvaOut = value;        *vaOut = base + value; return true; }
    if (value >= base && value < base + size)      { *vaOut  = value;        *rvaOut = value - base; return true; }
    return false;
}

void CmdBpCapture(const char* arg) {
    if (!g_cfg.allowBreakpoints) { Out("Refused: [Explorer] AllowBreakpoints=0"); return; }
    if (!arg || !*arg) { Out("usage: bpcapture <rva|va>   e.g. bpcapture 35AF920"); return; }

    u64 va = 0, rva = 0;
    if (!ResolveTargetAddress(ParseHex(arg), &va, &rva)) {
        Out("Not an address in CrimsonDesert.exe: %s", arg);
        Out("  module 0x%llX + 0x%llX", g_game.moduleBase, g_game.moduleSize);
        Out("  give an RVA (below the module size) or a VA inside the module.");
        return;
    }
    Out("=== bpcapture ===");
    Out("  module : CrimsonDesert.exe");
    Out("  RVA    : 0x%llX", rva);
    Out("  VA     : 0x%llX", va);

    if (bp::ArmCapture(va, "bpcapture")) {
        Out("  armed. It fires once, on the first thread to reach it.");
        Out("  Run `bpstatus` after the code has had a chance to run.");
    } else {
        Out("  NOT armed: %s", bp::OutcomeName(bp::kUserCapture));
        Out("  A retained address in another slot stays reserved even after cancel.");
    }
}

void CmdBpStatus() {
    Out("=== breakpoint slots ===");
    static const char* const kNames[] = { "console", "debug-commands", "bpcapture" };
    for (int i = 0; i < bp::kSlotCount; ++i) {
        const u64 addr = bp::SlotAddress(i);
        Out("  [%d] %-15s %-10s 0x%llX %s", i, kNames[i], bp::SlotStateName(i), addr,
            bp::SlotWhat(i));
        bp::ReportSlot(i);
    }
    const bp::Capture* c = bp::UserCapture();
    Out("");
    if (!c->hit) {
        Out("No capture available. Check last-arm and completion above before concluding a non-hit.");
        return;
    }
    Out("=== capture ===");
    Out("  thread %llu%s", c->threadId, c->restoreFailed ? "   WARNING: original byte NOT restored" : "");
    if (c->lateTraps) Out("  %llu other thread(s) trapped the same address", c->lateTraps);
    Out("  rip 0x%016llX  rsp 0x%016llX  rbp 0x%016llX  fl 0x%llX", c->rip, c->rsp, c->rbp, c->eflags);
    Out("  rcx 0x%016llX  rdx 0x%016llX  r8  0x%016llX  r9  0x%016llX", c->rcx, c->rdx, c->r8, c->r9);
    Out("  rax 0x%016llX  rbx 0x%016llX  rsi 0x%016llX  rdi 0x%016llX", c->rax, c->rbx, c->rsi, c->rdi);
    Out("  r10 0x%016llX  r11 0x%016llX  r12 0x%016llX  r13 0x%016llX", c->r10, c->r11, c->r12, c->r13);
    Out("  r14 0x%016llX  r15 0x%016llX", c->r14, c->r15);
    if (c->callbackException)
        Out("  callback exception 0x%08X", c->callbackException);
    Out("");
    if (!c->stackReadable) { Out("  stack was not readable"); return; }
    Out("  stack:");
    for (int i = 0; i < bp::Capture::kStackWords; ++i)
        Out("    [rsp+0x%02X] 0x%016llX", i * 8, c->stack[i]);
    Out("");
    Out("  Nothing above was dereferenced - following a pointer inside an exception");
    Out("  handler turns a wrong guess into a second exception. Walk it from here:");
    Out("    dump %llX 100        deref %llX 0        vtable %llX", c->rcx, c->rcx, c->rcx);
}

void CmdBpCancel() {
    bool any = false;
    for (int i = 0; i < bp::kSlotCount; ++i) if (bp::Cancel(i)) { Out("cancelled slot %d", i); any = true; }
    if (!any) Out("Nothing was armed. (A slot that already fired is 'completed', not armed.)");
}

// --- commands ---------------------------------------------------------------

void CmdHelp() {
    Out("=== Live Explorer Commands ===");
    Out("");
    Out("  rtti [filter]           Scan RTTI for class names (e.g. rtti Light)");
    Out("  strings <filter>        Scan data sections for strings");
    Out("  xrefs <hex_addr>        Find code references to an address");
    Out("  dump <hex_addr> [size]  Hex dump memory (default 256 bytes)");
    Out("  vtable <hex_addr> [n]   Dump object vtable (n entries, default 32)");
    Out("  deref <hex_addr> <offsets>  Walk pointer chain (comma-separated)");
    Out("  find <class_filter>     Find objects matching an RTTI class name");
    Out("  call <func> [a1..a4]    Call a function at an address");
    Out("  vcall <obj> <slot> [a1..a3]  Virtual call (passes this automatically)");
    Out("  write <addr> <hex_bytes>  Write bytes to memory");
    Out("  exec <command_text>     Type a command into the game console");
    Out("  help                    Show this help");
    Out("");
    Out("  --- engine debug commands (a separate system, see below) ---");
    Out("  debugstatus             Owner, gate, and whether the system resolved");
    Out("  debugmode               Toggle engine debug mode - DO THIS FIRST");
    Out("  debuglist [filter]      The 99 rendering features with their ids");
    Out("  debugfeature <id|name>  Toggle one rendering feature");
    Out("  debugcmd <full_name>    Invoke any PearlAbyssEngine.Debug.* command");
    Out("");
    Out("  --- one-shot execute breakpoints ---");
    Out("  bpcapture <rva|va>      Arm; records registers on the first hit");
    Out("  bpstatus                Slot states and the capture");
    Out("  bpcancel                Disarm anything still armed");
    Out("  manylights <action>     Targeted ManyLights GPU readback (help: manylights help)");
    Out("");
    Out("All addresses are hex (no 0x prefix needed). Arguments cannot contain");
    Out("spaces - the parser is a single sscanf, same as the original.");
    Out("");
    Out("Example: rtti LightComponent");
    Out("Example: strings IESProfile");
    Out("Example: dump 145BE3B48 200");
    Out("Example: deref 145BE3B48 10,A0");
    Out("Example: exec /settimeofdayupperlimit");
    Out("");
    Out("The debug* commands are NOT the in-game console. They reach the engine's");
    Out("own PearlAbyssEngine.Debug.* registry, which the chat console cannot see,");
    Out("so these names are not typeable in game. Every one of them is gated on");
    Out("engine debug mode: run `debugmode` first and look for the game's own");
    Out("'Debug mode is enabled' message on screen. Nothing is invoked unless its");
    Out("stub has been classified, so a refusal means unrecognised, not broken.");
    Out("Example: debugmode");
    Out("Example: debugfeature PointLightFlickering");
    Out("Example: debugcmd PearlAbyssEngine.Debug.ToggleUIRendering");
}

void CmdRtti(const char* filter) {
    Out("=== RTTI Class Scanner ===");
    Out("Filter: %s", (filter && *filter) ? filter : "(none)");
    if (!g_game.moduleBase) { Out("ERROR: Cannot get module info"); return; }

    Out("Module: 0x%llX - 0x%llX (%llu MB)", g_game.moduleBase,
        g_game.moduleBase + g_game.moduleSize, g_game.moduleSize >> 20);

    const auto secs = mem::DataSections(g_game.moduleBase);
    Out("Scanning %zu readable sections:", secs.size());
    for (const auto& s : secs)
        Out("  %s: 0x%llX - 0x%llX (%llu KB)", s.name, s.start, s.end, s.size() >> 10);
    Out("");

    const auto hits = rtti::Scan(g_game.moduleBase, filter, sig::kRttiMaxResults);
    for (const auto& td : hits) Out("  0x%llX  %s", td.address, td.name.c_str());
    if (static_cast<int>(hits.size()) >= sig::kRttiMaxResults)
        Out("(hit limit of %d entries, use a filter)", sig::kRttiMaxResults);
    Out("Total: %zu classes found", hits.size());
}

void CmdStrings(const char* filter) {
    if (!filter || !*filter) { Out("ERROR: strings requires a filter pattern"); return; }
    Out("=== String Scanner: '%s' ===", filter);
    const size_t len = strlen(filter);
    int found = 0;
    for (const auto& sec : mem::DataSections(g_game.moduleBase)) {
        if (sec.size() < len + 1) continue;
        const char* base = reinterpret_cast<const char*>(sec.start);
        for (u64 i = 0; i + len < sec.size(); ++i) {
            if (memcmp(base + i, filter, len) != 0) continue;
            // Only report if it looks like the start of a printable string.
            if (i > 0) {
                const unsigned char prev = static_cast<unsigned char>(base[i - 1]);
                if (prev >= 0x20 && prev < 0x7F) continue;
            }
            char buf[256] = {};
            if (!mem::SafeReadString(sec.start + i, buf, sizeof(buf))) continue;
            Out("  0x%llX: \"%s\"", sec.start + i, buf);
            if (++found >= sig::kStringScanMax) {
                Out("(hit limit of %d, refine your filter)", sig::kStringScanMax);
                Out("Total: %d strings found", found);
                return;
            }
        }
    }
    Out("Total: %d strings found", found);
}

void CmdXrefs(u64 addr) {
    Out("=== XRef Scanner: 0x%llX ===", addr);
    const auto hits = mem::FindXrefs(g_game.moduleBase, addr, sig::kXrefScanMax);
    Out("Found %zu references:", hits.size());
    for (size_t i = 0; i < hits.size(); ++i) {
        Out("  [%zu] 0x%llX", i, hits[i]);
        u8 b[16] = {};
        if (mem::SafeRead(reinterpret_cast<const void*>(hits[i]), b, 16)) {
            char hex[16 * 3 + 1] = {};
            for (int k = 0; k < 16; ++k) sprintf_s(hex + k * 3, 4, "%02X ", b[k]);
            Out("       %s", hex);
        }
    }
}

void CmdDump(u64 addr, int size) {
    if (size <= 0) size = 256;
    if (size > 0x4000) size = 0x4000;
    Out("=== Memory Dump: 0x%llX (%d bytes) ===", addr, size);
    for (int off = 0; off < size; off += 16) {
        const u64 line = addr + off;
        u8 buf[16] = {};
        if (!mem::SafeRead(reinterpret_cast<const void*>(line), buf, 16)) {
            Out("  0x%llX: <inaccessible>", line);
            continue;
        }
        char hex[16 * 3 + 1] = {};
        char asc[17] = {};
        for (int i = 0; i < 16; ++i) {
            sprintf_s(hex + i * 3, 4, "%02X ", buf[i]);
            asc[i] = (buf[i] >= 0x20 && buf[i] < 0x7F) ? static_cast<char>(buf[i]) : '.';
        }
        Out("  0x%llX: %s | %s", line, hex, asc);
    }
}

void CmdVtable(u64 obj, int count) {
    if (count <= 0) count = 32;
    if (count > 512) count = 512;
    u64 vt = 0;
    if (!mem::SafeReadPtr(obj, &vt) || !mem::PlausiblePointer(vt)) {
        Out("ERROR: Invalid vtable pointer at 0x%llX (value: 0x%llX)", obj, vt);
        return;
    }
    Out("=== Vtable Dump: obj=0x%llX vtable=0x%llX ===", obj, vt);
    const std::string name = rtti::NameForVtable(vt);
    if (!name.empty()) Out("  RTTI: %s", name.c_str());
    for (int i = 0; i < count; ++i) {
        u64 fn = 0;
        if (!mem::SafeReadPtr(vt + i * 8, &fn) || !mem::PlausiblePointer(fn)) {
            Out("  [%3d] +0x%03X: <end> 0x%llX", i, i * 8, fn);
            break;
        }
        u8 b[8] = {};
        char hex[8 * 3 + 1] = {};
        if (mem::SafeRead(reinterpret_cast<const void*>(fn), b, 8))
            for (int k = 0; k < 8; ++k) sprintf_s(hex + k * 3, 4, "%02X ", b[k]);
        Out("  [%3d] +0x%03X: 0x%llX  %s", i, i * 8, fn, hex);
    }
}

void CmdDeref(u64 start, const char* offsets) {
    if (!offsets || !*offsets) {
        Out("Usage: deref <addr> <offset1>,<offset2>,...");
        return;
    }
    Out("=== Deref Chain: start=0x%llX offsets=%s ===", start, offsets);
    Out("  [start] 0x%llX", start);

    u64 cur = start;
    int step = 0;
    const char* p = offsets;
    while (*p) {
        char* endp = nullptr;
        const u64 off = _strtoui64(p, &endp, 16);
        if (endp == p) break;
        u64 next = 0;
        if (!mem::SafeReadPtr(cur + off, &next) || !mem::PlausiblePointer(next)) {
            Out("  [%d] [0x%llX + 0x%llX] = 0x%llX", step, cur, off, next);
            Out("  (chain broken: invalid pointer)");
            return;
        }
        Out("  [%d] [0x%llX + 0x%llX] = 0x%llX", step, cur, off, next);
        cur = next;
        ++step;
        p = (*endp == ',') ? endp + 1 : endp;
        while (*p == ' ') ++p;
        if (*p == 0) break;
    }
    const std::string name = rtti::NameForObject(cur);
    if (!name.empty()) Out("  final RTTI: %s", name.c_str());
}

void CmdFind(const char* filter) {
    if (!filter || !*filter) { Out("Usage: find <class_filter>"); return; }
    Out("=== Object Scanner: '%s' ===", filter);
    Out("Scanning for vtable pointers matching RTTI filter...");

    const auto tds = rtti::Scan(g_game.moduleBase, filter, 256);
    Out("Found %zu matching type descriptors:", tds.size());
    for (const auto& td : tds) Out("  TD 0x%llX: %s", td.address, td.name.c_str());
    if (tds.empty()) return;

    Out("Scanning writable module sections for live object instances...");
    int found = 0;
    for (const auto& sec : mem::Sections(g_game.moduleBase)) {
        if (!sec.writable() || sec.executable()) continue;
        for (u64 p = sec.start; p + 8 <= sec.end; p += 8) {
            u64 vt = 0;
            if (!mem::SafeReadPtr(p, &vt) || !mem::PlausiblePointer(vt)) continue;
            const u64 td = rtti::TypeDescForVtable(vt);
            if (!td) continue;
            bool match = false;
            for (const auto& t : tds) if (t.address == td) { match = true; break; }
            if (!match) continue;
            Out("  INSTANCE at global 0x%llX -> vtable 0x%llX [%s]",
                p, vt, rtti::NameForVtable(vt).c_str());
            ++found;
        }
    }
    if (!found)
        Out("  (no global instances found - these objects are heap-allocated)");
    Out("Total instances: %d", found);
    Out("");
    Out("NOTE: this scans the module's writable sections only, never the heap.");
    Out("Most engine objects live on the heap, so an empty result here does not");
    Out("mean the class has no live instances. Use a heap scanner for a census.");
}

void CmdCall(u64 fn, u64 a1, u64 a2, u64 a3, u64 a4) {
    if (!g_cfg.allowCall) { Out("ERROR: call is disabled ([Explorer] AllowCall=0)"); return; }
    if (!mem::PlausiblePointer(fn)) { Out("ERROR: implausible function address 0x%llX", fn); return; }
    Out("=== Calling 0x%llX (args: 0x%llX, 0x%llX, 0x%llX, 0x%llX) ===", fn, a1, a2, a3, a4);
    u64 result = 0;
    DWORD code = 0;
    if (CallGuarded(fn, a1, a2, a3, a4, &result, &code))
        Out("  Result: 0x%llX (%lld)", result, static_cast<i64>(result));
    else
        Out("  EXCEPTION: 0x%08X", code);
}

void CmdVCall(u64 obj, int slot, u64 a1, u64 a2, u64 a3) {
    if (!g_cfg.allowCall) { Out("ERROR: vcall is disabled ([Explorer] AllowCall=0)"); return; }
    u64 vt = 0;
    if (!mem::SafeReadPtr(obj, &vt) || !mem::PlausiblePointer(vt)) {
        Out("ERROR: Invalid vtable at obj 0x%llX", obj);
        return;
    }
    u64 fn = 0;
    if (!mem::SafeReadPtr(vt + static_cast<u64>(slot) * 8, &fn) || !mem::PlausiblePointer(fn)) {
        Out("ERROR: Invalid function pointer at vtable[%d]", slot);
        return;
    }
    Out("=== VCall: obj=0x%llX slot=%d func=0x%llX ===", obj, slot, fn);
    Out("  args: this=0x%llX, a1=0x%llX, a2=0x%llX, a3=0x%llX", obj, a1, a2, a3);
    u64 result = 0;
    DWORD code = 0;
    if (VCallGuarded(obj, fn, a1, a2, a3, &result, &code))
        Out("  Result: 0x%llX (%lld)", result, static_cast<i64>(result));
    else
        Out("  EXCEPTION: 0x%08X", code);
}

void CmdWrite(u64 addr, const char* hexBytes) {
    if (!g_cfg.allowWrite) { Out("ERROR: write is disabled ([Explorer] AllowWrite=0)"); return; }
    if (!addr || !hexBytes || !*hexBytes) { Out("Usage: write <addr> <hex_bytes>"); return; }

    std::vector<u8> bytes;
    for (const char* p = hexBytes; p[0] && p[1]; ) {
        if (!isxdigit(static_cast<unsigned char>(p[0])) ||
            !isxdigit(static_cast<unsigned char>(p[1]))) { ++p; continue; }
        char pair[3] = { p[0], p[1], 0 };
        bytes.push_back(static_cast<u8>(strtol(pair, nullptr, 16)));
        p += 2;
    }
    if (bytes.empty()) { Out("ERROR: No valid hex bytes"); return; }

    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(addr), bytes.size(),
                        PAGE_EXECUTE_READWRITE, &old)) {
        Out("ERROR: VirtualProtect failed for 0x%llX", addr);
        return;
    }
    const bool ok = WriteGuarded(addr, bytes.data(), bytes.size());
    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void*>(addr), bytes.size(), old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(addr), bytes.size());

    if (ok) Out("Wrote %zu bytes to 0x%llX", bytes.size(), addr);
    else    Out("ERROR: Write exception at 0x%llX", addr);
}

// --- poll thread ------------------------------------------------------------

DWORD WINAPI PollThread(LPVOID) {
    while (g_running) {
        Sleep(g_cfg.pollIntervalMs);
        if (GetFileAttributesA(g_cmdPath.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        Sleep(50);   // let whoever is writing the file finish

        std::vector<std::string> lines;
        if (FILE* f = fopen(g_cmdPath.c_str(), "r")) {
            char buf[1024];
            while (fgets(buf, sizeof(buf), f)) {
                std::string s(buf);
                while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
                if (!s.empty()) lines.push_back(s);
            }
            fclose(f);
        }
        DeleteFileA(g_cmdPath.c_str());
        if (lines.empty()) continue;

        FILE* out = fopen(g_resultPath.c_str(), "w");
        if (!out) continue;
        SetResultFile(out);
        Out("=== Live Explorer Results ===");
        for (const auto& l : lines) Execute(l);
        Out("=== Done ===");
        SetResultFile(nullptr);
        fclose(out);
    }
    return 0;
}

}  // namespace

void Execute(const std::string& line) {
    if (line.empty() || line[0] == '#') return;

    char cmd[64] = {}, a1[512] = {}, a2[64] = {}, a3[64] = {}, a4[64] = {}, a5[64] = {};
    const int n = sscanf_s(line.c_str(), "%63s %511s %63s %63s %63s %63s",
                           cmd, static_cast<unsigned>(sizeof(cmd)),
                           a1,  static_cast<unsigned>(sizeof(a1)),
                           a2,  static_cast<unsigned>(sizeof(a2)),
                           a3,  static_cast<unsigned>(sizeof(a3)),
                           a4,  static_cast<unsigned>(sizeof(a4)),
                           a5,  static_cast<unsigned>(sizeof(a5)));
    if (n < 1) return;

    if      (_stricmp(cmd, "help")    == 0) CmdHelp();
    else if (_stricmp(cmd, "rtti")    == 0) CmdRtti(a1);
    else if (_stricmp(cmd, "strings") == 0) CmdStrings(a1);
    else if (_stricmp(cmd, "xrefs")   == 0) CmdXrefs(ParseHex(a1));
    else if (_stricmp(cmd, "dump")    == 0) CmdDump(ParseHex(a1), a2[0] ? atoi(a2) : 256);
    else if (_stricmp(cmd, "vtable")  == 0) CmdVtable(ParseHex(a1), a2[0] ? atoi(a2) : 32);
    else if (_stricmp(cmd, "deref")   == 0) CmdDeref(ParseHex(a1), a2);
    else if (_stricmp(cmd, "find")    == 0) CmdFind(a1);
    else if (_stricmp(cmd, "call")    == 0)
        CmdCall(ParseHex(a1), ParseHex(a2), ParseHex(a3), ParseHex(a4), ParseHex(a5));
    else if (_stricmp(cmd, "vcall")   == 0)
        CmdVCall(ParseHex(a1), atoi(a2), ParseHex(a3), ParseHex(a4), ParseHex(a5));
    else if (_stricmp(cmd, "write")   == 0) CmdWrite(ParseHex(a1), a2);
    else if (_stricmp(cmd, "debugstatus")  == 0) dbg::Status();
    else if (_stricmp(cmd, "debuglist")    == 0) dbg::List(a1);
    else if (_stricmp(cmd, "debugfeature") == 0) dbg::InvokeFeature(a1);
    else if (_stricmp(cmd, "debugmode")    == 0) dbg::Invoke("PearlAbyssEngine.ToggleDebugMode");
    else if (_stricmp(cmd, "debugcmd")     == 0) dbg::Invoke(a1);
    else if (_stricmp(cmd, "bpcapture")    == 0) CmdBpCapture(a1);
    else if (_stricmp(cmd, "bpstatus")     == 0) CmdBpStatus();
    else if (_stricmp(cmd, "bpcancel")     == 0) CmdBpCancel();
    else if (_stricmp(cmd, "manylights")   == 0) manylights::Command(a1, a2);
    else if (_stricmp(cmd, "exec")    == 0) {
        // exec takes the rest of the line, not just the first token, so a
        // command with arguments still works: "exec /setres 1920 1080".
        const size_t sp = line.find_first_of(" \t");
        std::string rest = (sp == std::string::npos) ? std::string() : line.substr(sp + 1);
        while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) rest.erase(0, 1);
        console::ExecConsoleCommand(rest.c_str());
    }
    else Out("Unknown command: %s (type 'help' for commands)", cmd);
}

void Start() {
    const std::string dir = GameDirectory();
    g_cmdPath    = dir + "\\" + g_cfg.cmdFile;
    g_resultPath = dir + "\\" + g_cfg.resultFile;

    DeleteFileA(g_cmdPath.c_str());
    g_running = true;
    g_thread = CreateThread(nullptr, 0, PollThread, nullptr, 0, nullptr);
    Log("Live Explorer watching %s", g_cmdPath.c_str());
    Log("  results go to %s", g_resultPath.c_str());
    Log("  create the command file WHILE the game runs, not before launch");
}

void Stop() {
    g_running = false;
    if (g_thread) {
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
}

}}  // namespace ch::explorer

