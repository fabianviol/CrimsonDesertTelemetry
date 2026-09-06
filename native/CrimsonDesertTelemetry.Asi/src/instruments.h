#pragma once
#include <windows.h>
#include <cstdint>

namespace cdt::instruments
{
// Called synchronously from the sole DllMain before engine registration. This
// preserves the working debug-owner startup capture; delayed init is too late.
void EarlyAttach(HMODULE module);
// Worker thread only. All D3D/resource work and heavy logging happen here or on
// the already-authorized, exact render callsite; nothing is polled in DllMain.
void Run(HANDLE stopEvent);
bool OwnsCodeAddress(uint64_t address);
}
