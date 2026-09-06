#pragma once
#include "common.h"

namespace ch { namespace discovery {

// Runs inside DllMain, before the game's own code has executed. Two jobs:
// NOP every console gate found by full-pattern scan, and arm a one-shot
// breakpoint on the function that references PearlAbyssEngine.ShowDebugConsole
// so its `this` pointer can be captured the first time the game calls it.
void EarlyPatch();

// Phases 1-6. Returns false when the ShowDebugConsole string is not found,
// which is the one failure that makes everything downstream meaningless.
bool Discover();

// Calls the game's register-command function for a set of known command names
// and writes ConsoleCommands.txt. Informational.
void DumpRegisteredCommands();

// Removes the vectored handler and restores the breakpoint byte if it is still
// armed. Safe to call more than once.
void DisarmEarlyBreakpoint();

}}  // namespace ch::discovery

