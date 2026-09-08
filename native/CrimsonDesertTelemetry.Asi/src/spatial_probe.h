#pragma once
#include <cstdint>

namespace cdt::spatial
{
// Private passive observation by default. The separate readback opt-in permits
// ONE guarded texture copy per process; neither mode publishes API data.
// A named event starts one bounded run after the game has loaded.
bool Start(uint64_t moduleBase, const wchar_t* directory,bool enableReadback=false);
void Poll();
void Stop();
bool OwnsCodeAddress(uint64_t address);
}
