// Invoking the engine's own PearlAbyssEngine.Debug.* commands.
//
// These are a second command system, separate from the chat console: they are
// registered into the registry at globalCtxPtr through registerCmdFunc, and the
// chat handler never looks there, so they cannot be typed in game. See
// ../DEBUG_COMMANDS_20260905.md for how that was established.
//
// The mechanism here is the one the console toggle already uses: resolve the
// per-command handler stub from the command's name string, and call it with the
// owner captured from its registration function. Everything is name-anchored;
// no stub address is hard-coded.
#pragma once

#include "common.h"

namespace ch { namespace dbg {

// What a resolved command actually does, decided by reading its stub, not by
// trusting its name. The invoker refuses anything it has not classified.
enum class Behaviour {
    ToggleKnown,   // stub carries the action - safe to invoke
    NoOp,          // stub body is `ret` - would report nothing and mean nothing
    SharedStub,    // callback comes from a register, not an inline lea
    Unknown,       // resolved, but the stub shape is not recognised
    Unresolved,    // name, xref or stub not found
};

struct Resolved {
    std::string name;              // the full PearlAbyssEngine.* name
    u64         nameString = 0;
    u64         xref       = 0;    // the `lea rdx` that references the name
    u64         stub       = 0;
    int         featureId  = -1;   // read out of the stub, -1 when not a feature
    Behaviour   behaviour  = Behaviour::Unresolved;
    const char* note       = "";
};

// Resolves without invoking. Safe to call at any time; touches no game state.
Resolved Resolve(const char* name);

// Reads the debug-mode gate byte at [[owner+0x38]+0x49]. -1 when it cannot be
// read. Every ToggleRenderingFeature stub returns immediately when this is 0.
int ReadGate();

// Invokes a command by name. `name` may be given in full
// ("PearlAbyssEngine.Debug.ToggleRenderingFeature.PointLight"), or shortened to
// the last dotted component, in which case the known prefixes are tried.
//
// Refuses NoOp, SharedStub and Unknown. Logs the gate before and after, without
// claiming that an unchanged gate says anything about whether the call did
// something: for everything except ToggleDebugMode the gate is a precondition,
// not a result.
void Invoke(const char* name);

// Invokes a ToggleRenderingFeature by numeric id or by short name. The id is
// looked up in the generated table to get a name, the stub is resolved from that
// name, and the id is then read back out of the stub and compared. A mismatch
// refuses rather than fires.
void InvokeFeature(const char* idOrName);

// Prints the generated feature table, marking the no-ops. `filter` is an
// optional case-insensitive substring.
void List(const char* filter);

// One-line status: owner, gate, and whether the registration function was seen.
void Status();

}}  // namespace ch::dbg

