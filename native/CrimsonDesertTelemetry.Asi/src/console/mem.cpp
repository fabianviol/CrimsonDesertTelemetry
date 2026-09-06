#include "mem.h"
#include "signatures.h"

#include <psapi.h>
#include <cstring>

namespace ch { namespace mem {

bool GetModuleRange(const char* moduleName, u64* base, u64* size) {
    HMODULE h = GetModuleHandleA(moduleName && *moduleName ? moduleName : nullptr);
    if (!h) return false;
    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), h, &mi, sizeof(mi))) return false;
    if (base) *base = reinterpret_cast<u64>(mi.lpBaseOfDll);
    if (size) *size = mi.SizeOfImage;
    return true;
}

std::vector<Section> Sections(u64 moduleBase) {
    std::vector<Section> out;
    if (!moduleBase) return out;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(moduleBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return out;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(moduleBase + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return out;

    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        Section s;
        s.start = moduleBase + sec->VirtualAddress;
        s.end   = s.start + sec->Misc.VirtualSize;
        s.flags = sec->Characteristics;
        memcpy(s.name, sec->Name, 8);
        s.name[8] = 0;
        if (s.end > s.start) out.push_back(s);
    }
    return out;
}

// Section names in this executable are misleading - the code lives in sections
// called .data2 and .debug$P (PLAYBOOK.md section 5). Always select by the
// characteristic bits, never by name.
std::vector<Section> DataSections(u64 moduleBase) {
    std::vector<Section> out;
    for (const auto& s : Sections(moduleBase))
        if (s.readable() && !s.executable()) out.push_back(s);
    return out;
}

std::vector<Section> CodeSections(u64 moduleBase) {
    std::vector<Section> out;
    for (const auto& s : Sections(moduleBase))
        if (s.executable()) out.push_back(s);
    return out;
}

bool SafeRead(const void* src, void* dst, size_t n) {
    __try {
        memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeReadPtr(u64 addr, u64* out) {
    u64 v = 0;
    if (!SafeRead(reinterpret_cast<const void*>(addr), &v, sizeof(v))) return false;
    if (out) *out = v;
    return true;
}

bool SafeReadString(u64 addr, char* dst, size_t cap) {
    if (!cap) return false;
    __try {
        const char* p = reinterpret_cast<const char*>(addr);
        size_t i = 0;
        for (; i + 1 < cap && p[i]; ++i) dst[i] = p[i];
        dst[i] = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        dst[0] = 0;
        return false;
    }
}

bool IsReadable(u64 addr, size_t n) {
    if (!addr || n == 0 || n > 0x10000) return false;
    u8 tmp[0x100];
    size_t probe = n < sizeof(tmp) ? n : sizeof(tmp);
    return SafeRead(reinterpret_cast<const void*>(addr), tmp, probe);
}

bool PlausiblePointer(u64 v) {
    // User-mode addresses on x64 Windows, excluding the low 64 KB.
    return v > 0x10000ull && v < 0x7FFFFFFFFFFFull;
}

u64 FindString(u64 moduleBase, u64 moduleSize, const char* needle) {
    if (!moduleBase || !needle) return 0;
    const size_t len = strlen(needle);
    if (moduleSize <= len + 1) return 0;
    const char* base = reinterpret_cast<const char*>(moduleBase);
    const u64 limit = moduleSize - (len + 1);
    for (u64 i = 0; i <= limit; ++i) {
        if (base[i] != needle[0]) continue;
        if (memcmp(base + i, needle, len) == 0 && base[i + len] == 0)
            return moduleBase + i;
    }
    return 0;
}

// Accept `lea reg,[rip+d32]` for any register. Encoding is
//   REX.W (0x48 or 0x4C) | 0x8D | modrm(mod=00, reg=r, rm=101) | rel32
// giving modrm bytes 0x05,0x0D,0x15,0x1D,0x25,0x2D,0x35,0x3D. REX.R (0x4C)
// selects r8-r15 with the same low three bits.
static bool IsRipLea(const u8* p, int* regOut) {
    if ((p[0] != 0x48 && p[0] != 0x4C) || p[1] != sig::kLeaRip) return false;
    const u8 modrm = p[2];
    if ((modrm & 0xC7) != 0x05) return false;     // mod=00, rm=101
    if (regOut) *regOut = ((modrm >> 3) & 7) | (p[0] == 0x4C ? 8 : 0);
    return true;
}

std::vector<u64> FindXrefs(u64 moduleBase, u64 target, size_t max) {
    std::vector<u64> hits;
    if (!moduleBase || !target) return hits;
    for (const auto& s : CodeSections(moduleBase)) {
        if (s.size() < 7) continue;
        const u8* base = reinterpret_cast<const u8*>(s.start);
        const u64 n = s.size() - 7;
        for (u64 i = 0; i < n; ++i) {
            if (!IsRipLea(base + i, nullptr)) continue;
            const i32 disp = *reinterpret_cast<const i32*>(base + i + 3);
            if (s.start + i + 7 + static_cast<i64>(disp) == target) {
                hits.push_back(s.start + i);
                if (max && hits.size() >= max) return hits;
            }
        }
    }
    return hits;
}

u64 ScanBack(u64 from, const u8* pattern, const u8* mask, size_t len, int window) {
    for (int back = 0; back < window; ++back) {
        const u64 p = from - back;
        if (p < 0x10000ull) break;
        if (!IsReadable(p, len)) continue;
        const u8* b = reinterpret_cast<const u8*>(p);
        bool ok = true;
        for (size_t i = 0; i < len; ++i) {
            if (mask && !mask[i]) continue;
            if (b[i] != pattern[i]) { ok = false; break; }
        }
        if (ok) return p;
    }
    return 0;
}

u64 ScanForward(u64 from, const u8* pattern, const u8* mask, size_t len, int window) {
    for (int fwd = 0; fwd < window; ++fwd) {
        const u64 p = from + fwd;
        if (!IsReadable(p, len)) continue;
        const u8* b = reinterpret_cast<const u8*>(p);
        bool ok = true;
        for (size_t i = 0; i < len; ++i) {
            if (mask && !mask[i]) continue;
            if (b[i] != pattern[i]) { ok = false; break; }
        }
        if (ok) return p;
    }
    return 0;
}

u64 RipTarget(u64 at, int dispOffset, int instrLen) {
    i32 disp = 0;
    if (!SafeRead(reinterpret_cast<const void*>(at + dispOffset), &disp, 4)) return 0;
    return at + instrLen + static_cast<i64>(disp);
}

bool LooksLikePrologue(u64 addr) {
    u8 b[3] = {};
    if (!SafeRead(reinterpret_cast<const void*>(addr), b, 3)) return false;
    // 48 89 ..            mov [rsp+x], reg   (home-register spill)
    // 48 83 EC / 48 8B EC sub rsp,x / mov rbp,rsp
    if (b[0] == 0x48) {
        if (b[1] == 0x89) return true;
        if ((b[1] == 0x83 || b[1] == 0x8B) && b[2] == 0xEC) return true;
    }
    // 40 50..57           push r?? with a redundant REX
    if (b[0] == 0x40 && b[1] >= 0x50 && b[1] <= 0x57) return true;
    // 55/56/57 push rbp/rsi/rdi, 53 push rbx
    if (b[0] >= 0x55 && b[0] <= 0x57) return true;
    if (b[0] == 0x53) return true;
    return false;
}

u64 FindFunctionStart(u64 addr, int window) {
    // Walk back looking for int3 padding whose next byte starts a prologue.
    for (int back = 0x10; back < window; ++back) {
        const u64 p = addr - back;
        u8 pad = 0;
        if (!SafeRead(reinterpret_cast<const void*>(p), &pad, 1)) continue;
        if (pad != sig::kInt3) continue;
        u8 next = 0;
        if (!SafeRead(reinterpret_cast<const void*>(p + 1), &next, 1)) continue;
        if (next == sig::kInt3) continue;
        if (LooksLikePrologue(p + 1)) return p + 1;
        Log("  Skipped CC boundary at 0x%llX (byte: 0x%02X) not a prologue", p + 1, next);
    }
    return 0;
}

bool Patch(u64 addr, const void* bytes, size_t n, const char* why) {
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(addr), n, PAGE_EXECUTE_READWRITE, &old)) {
        Log("  PATCH FAILED (VirtualProtect) at 0x%llX for %s (error %lu)",
            addr, why ? why : "?", GetLastError());
        return false;
    }
    bool ok = true;
    __try {
        memcpy(reinterpret_cast<void*>(addr), bytes, n);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void*>(addr), n, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(addr), n);
    Log("  PATCH %s %zu byte(s) at 0x%llX for %s",
        ok ? "OK" : "FAILED (write)", n, addr, why ? why : "?");
    return ok;
}

}}  // namespace ch::mem

