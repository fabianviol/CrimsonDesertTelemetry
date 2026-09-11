#pragma once
#include <cstdint>

namespace cdt::spatial
{
// Private passive observation by default. The separate readback opt-in permits
// guarded texture copies; a series can be repeated without restarting the game.
// A named event starts one bounded run after the game has loaded.
// transactions: RETAINED diagnostic snapshots, bounded by memory.
// publishSeconds: how long the live camera-visibility value keeps refreshing.
// They are separate on purpose; one key meaning both already misfired once.
bool Start(uint64_t moduleBase, const wchar_t* directory,bool enableReadback=false,
    unsigned transactions=1,unsigned intervalMilliseconds=1000,unsigned publishSeconds=0,
    bool enableDistanceReadback=false,bool persistDistanceEvidence=false,
    bool startImmediately=false);
void Poll();
void Stop();
bool OwnsCodeAddress(uint64_t address);
}
