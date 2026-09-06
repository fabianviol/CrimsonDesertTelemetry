#include "rtti.h"
#include "mem.h"
#include "signatures.h"

#include <cctype>
#include <cstring>

namespace ch { namespace rtti {

namespace {

std::string Lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

// MSVC _RTTICompleteObjectLocator (64-bit layout).
#pragma pack(push, 4)
struct CompleteObjectLocator {
    u32 signature;        // 1 on x64
    u32 offset;
    u32 cdOffset;
    u32 pTypeDescriptor;  // RVA
    u32 pClassDescriptor; // RVA
    u32 pSelf;            // RVA of this structure - gives us the image base
};
#pragma pack(pop)

}  // namespace

std::string Demangle(const char* mangled) {
    if (!mangled) return {};
    const std::string s(mangled);
    // Only class (.?AV) and struct (.?AU) descriptors are handled.
    if (s.size() < 6) return s;
    if (s.compare(0, 4, sig::kTypeDescClass) != 0 &&
        s.compare(0, 4, sig::kTypeDescStruct) != 0)
        return s;

    std::string body = s.substr(4);
    if (body.size() >= 2 && body.compare(body.size() - 2, 2, "@@") == 0)
        body = body.substr(0, body.size() - 2);
    if (body.empty()) return s;

    // Name components are stored innermost-first: "LightComponent@pa" is
    // pa::LightComponent. Templates carry '?$' and further '@'-separated
    // arguments; we leave those parts alone rather than half-decoding them.
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i <= body.size(); ++i) {
        if (i == body.size() || body[i] == '@') {
            parts.push_back(body.substr(start, i - start));
            start = i + 1;
        }
    }
    std::string out;
    for (size_t i = parts.size(); i-- > 0;) {
        if (parts[i].empty()) continue;
        if (!out.empty()) out += "::";
        out += parts[i];
    }
    return out.empty() ? s : out;
}

std::vector<TypeDesc> Scan(u64 moduleBase, const char* filter, int cap) {
    std::vector<TypeDesc> out;
    const std::string want = Lower(filter ? filter : "");

    for (const auto& sec : mem::DataSections(moduleBase)) {
        for (u64 p = sec.start; p + 0x20 < sec.end; p += sig::kRttiScanStride) {
            // TypeDescriptor: { void* pVFTable; void* spare; char name[]; }
            const u64 nameAddr = p + sig::kTypeDescNameOffset;
            char head[4] = {};
            if (!mem::SafeRead(reinterpret_cast<const void*>(nameAddr), head, 4)) continue;
            if (memcmp(head, sig::kTypeDescClass, 4) != 0 &&
                memcmp(head, sig::kTypeDescStruct, 4) != 0)
                continue;

            char raw[512] = {};
            if (!mem::SafeReadString(nameAddr, raw, sizeof(raw))) continue;
            if (strlen(raw) < 6) continue;

            TypeDesc td;
            td.address = p;
            td.raw     = raw;
            td.name    = Demangle(raw);
            if (!want.empty() && Lower(td.name).find(want) == std::string::npos &&
                Lower(td.raw).find(want) == std::string::npos)
                continue;

            out.push_back(std::move(td));
            if (cap > 0 && static_cast<int>(out.size()) >= cap) return out;
        }
    }
    return out;
}

u64 TypeDescForVtable(u64 vtable) {
    if (!mem::PlausiblePointer(vtable)) return 0;
    u64 colAddr = 0;
    if (!mem::SafeReadPtr(vtable - 8, &colAddr)) return 0;
    if (!mem::PlausiblePointer(colAddr)) return 0;

    CompleteObjectLocator col{};
    if (!mem::SafeRead(reinterpret_cast<const void*>(colAddr), &col, sizeof(col))) return 0;
    if (col.signature != 1) return 0;          // x64 locators carry signature 1
    if (col.pSelf == 0 || col.pTypeDescriptor == 0) return 0;

    // pSelf is this locator's own RVA, so the image base falls out of it.
    const u64 imageBase = colAddr - col.pSelf;
    return imageBase + col.pTypeDescriptor;
}

std::string NameForVtable(u64 vtable) {
    const u64 td = TypeDescForVtable(vtable);
    if (!td) return {};
    char raw[512] = {};
    if (!mem::SafeReadString(td + sig::kTypeDescNameOffset, raw, sizeof(raw))) return {};
    if (raw[0] != '.') return {};
    return Demangle(raw);
}

std::string NameForObject(u64 object) {
    u64 vt = 0;
    if (!mem::SafeReadPtr(object, &vt)) return {};
    return NameForVtable(vt);
}

}}  // namespace ch::rtti

