#pragma once
#include <cstdint>

namespace cdt::physics
{
// Private diagnostic: exact-build guard is also required by instruments::Run.
bool Start(std::uint64_t moduleBase, const wchar_t* directory, bool continuousVisibility = false);
void Poll();
void Stop();
bool OwnsCodeAddress(std::uint64_t address);
}
