#pragma once
#include <cstdint>

namespace cdt::spatial
{
// Private, opt-in passive observation. Calls no GPU copy/barrier and publishes
// no API data. A named event starts one bounded run after the game has loaded.
bool Start(uint64_t moduleBase, const wchar_t* directory);
void Poll();
void Stop();
bool OwnsCodeAddress(uint64_t address);
}
