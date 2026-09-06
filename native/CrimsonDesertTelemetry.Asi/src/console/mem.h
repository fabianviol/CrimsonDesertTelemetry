// Module, section and pattern helpers. Everything here is read-only except
// Patch(), which is the one place that calls VirtualProtect.
#pragma once

#include "common.h"

namespace ch { namespace mem {

struct Section {
    u64         start = 0;
    u64         end   = 0;
    u32         flags = 0;
    char        name[9] = {};
    bool executable() const { return (flags & IMAGE_SCN_MEM_EXECUTE) != 0; }
    bool writable()   const { return (flags & IMAGE_SCN_MEM_WRITE) != 0; }
    bool readable()   const { return (flags & IMAGE_SCN_MEM_READ) != 0; }
    u64  size()       const { return end - start; }
};

bool GetModuleRange(const char* moduleName, u64* base, u64* size);
std::vector<Section> Sections(u64 moduleBase);

// Sections carrying data we want to scan: readable, not executable.
std::vector<Section> DataSections(u64 moduleBase);
std::vector<Section> CodeSections(u64 moduleBase);

// SEH-guarded primitives. Every one of these returns false rather than raising,
// so a bad address from a command file cannot take the game down.
bool SafeRead(const void* src, void* dst, size_t n);
bool SafeReadPtr(u64 addr, u64* out);
bool SafeReadString(u64 addr, char* dst, size_t cap);
bool IsReadable(u64 addr, size_t n);

// Whether `addr` looks like it could be a pointer into user address space.
bool PlausiblePointer(u64 v);

// Linear search of the whole module image for a NUL-terminated string. Matches
// len+1 bytes, so the terminator has to be there too.
u64 FindString(u64 moduleBase, u64 moduleSize, const char* needle);

// References to `target`. Recognises `lea reg,[rip+d32]` for every register,
// not just rdx as the original did; `matchedReg` receives the ModRM reg field
// of the first hit so a register-allocation change is visible in the log.
std::vector<u64> FindXrefs(u64 moduleBase, u64 target, size_t max = 0);

// Scans backwards from `from` for `pattern`, ignoring bytes marked 0 in `mask`.
// Returns 0 if not found. `window` is in bytes.
u64 ScanBack(u64 from, const u8* pattern, const u8* mask, size_t len, int window);
u64 ScanForward(u64 from, const u8* pattern, const u8* mask, size_t len, int window);

// Resolve a RIP-relative operand: `at` points at the first opcode byte,
// `dispOffset` is where the rel32 sits, `instrLen` is the whole instruction.
u64 RipTarget(u64 at, int dispOffset, int instrLen);

// Walks backwards from `addr` to the start of the enclosing function, using
// 0xCC padding plus a prologue-shaped first byte. Returns 0 on failure.
u64 FindFunctionStart(u64 addr, int window);
bool LooksLikePrologue(u64 addr);

// The only writer. Logs what it did and refuses when cfg.allowWrite is off for
// explorer-initiated writes (callers pass `fromCommand`).
bool Patch(u64 addr, const void* bytes, size_t n, const char* why);

}}  // namespace ch::mem

