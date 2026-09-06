// MSVC RTTI reading.
//
// CrimsonDesert.exe ships complete RTTI - about 15,000 type descriptors with
// the Pearl Abyss `pa` namespace (RTTI_AND_CONSOLE.md). Class names survive
// updates where addresses do not, so this is the most stable anchor available.
//
// Scope note, repeated from the research: RTTI resolves vtables and nothing
// more. It does not identify a live singleton and does not prove a member
// offset, so it does not replace an exact executable-hash gate.
#pragma once

#include "common.h"

namespace ch { namespace rtti {

struct TypeDesc {
    u64         address = 0;   // the TypeDescriptor itself
    std::string name;          // demangled, e.g. "pa::LightComponent"
    std::string raw;           // e.g. ".?AVLightComponent@pa@@"
};

// ".?AVLightComponent@pa@@" -> "pa::LightComponent". Returns the input on
// anything it does not understand, so nothing is silently lost.
std::string Demangle(const char* mangled);

// Every type descriptor in the module's readable non-executable sections whose
// demangled name contains `filter` (case-insensitive; empty matches all).
std::vector<TypeDesc> Scan(u64 moduleBase, const char* filter, int cap);

// Class name for an object, via vtable[-1] -> CompleteObjectLocator.
// Empty string when the object has no usable RTTI.
std::string NameForVtable(u64 vtable);
std::string NameForObject(u64 object);

// TypeDescriptor address for a vtable, 0 when unavailable.
u64 TypeDescForVtable(u64 vtable);

}}  // namespace ch::rtti

