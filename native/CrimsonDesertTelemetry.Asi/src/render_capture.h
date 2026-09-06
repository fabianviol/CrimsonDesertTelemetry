#pragma once
#include <windows.h>
#include <cstdint>
namespace cdt::render
{
bool StartCapture(uint64_t moduleBase, unsigned sampleRateHz);
void PollCapture();
void StopCapture();
bool OwnsCodeAddress(uint64_t address);
}
