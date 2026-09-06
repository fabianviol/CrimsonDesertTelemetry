// CrimsonHue console enabler - shared types, logging, configuration.
//
// Reimplementation of the Nexus mod 803 proxy DLL. See ../ANALYSIS.md for what
// the original does and which parts of it are pinned to a particular build.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ch {

using u8  = uint8_t;
using i32 = int32_t;
using i64 = int64_t;
using u32 = uint32_t;
using u64 = uint64_t;

// ---------------------------------------------------------------- logging --

// Log() writes to CrimsonDesertTelemetry.native.log only.
// Out() writes to that log *and*, when a command is being serviced, to
// inject_result.txt. Command handlers use Out(); everything else uses Log().
void OpenLog(const std::string& gameDir);
void CloseLog();
void Log(const char* fmt, ...);
void Out(const char* fmt, ...);
void SetResultFile(FILE* f);

// Logs `rows` rows of 16 bytes at `addr`, prefixed by `label`. Unreadable rows
// are reported rather than skipped, so a null result is distinguishable from an
// inaccessible one.
void LogHexDump(const void* addr, int rows, const char* label);

// ------------------------------------------------------------ config file --

// Unified CrimsonDesertTelemetry.ini next to the ASI. Console and explorer are
// optional research controls; normal telemetry does not enable them.
struct Config {
    bool  enableConsole      = false;  // discovery, gate patching, ~ toggle
    bool  patchGates         = true;   // NOP the two conditional jumps
    bool  patchDevFlags      = true;   // phase 5: write 1 into dev variables
    bool  earlyBreakpoint    = true;   // the one-shot 0xCC that captures `this`
    bool  hideNotification   = true;   // the build-pinned hide path, signatures.h
    bool  enableExplorer     = false;  // inject_cmd.txt / inject_result.txt
    bool  allowWrite         = true;   // the `write` command
    bool  allowCall          = true;   // the `call` and `vcall` commands
    bool  allowDebugCommands = true;   // debugmode / debugfeature / debugcmd
    bool  allowBreakpoints   = true;   // bpcapture / bpstatus / bpcancel
    bool  allowManyLights    = false;  // targeted GPU readback instrument
    bool  verboseDiscovery   = true;   // phase 4/6 informational dumps
    u32   initDelayMs        = 5000;
    u32   pollIntervalMs     = 100;
    std::string cmdFile      = "inject_cmd.txt";
    std::string resultFile   = "inject_result.txt";
};

extern Config g_cfg;
void LoadConfig(const std::string& gameDir);
// Prints the active configuration. Separate from LoadConfig because the config
// has to be read in DllMain, before the log file exists.
void LogConfig();

// ------------------------------------------------------------- game state --

struct GameState {
    HMODULE   module      = nullptr;   // CrimsonDesert.exe
    u64       moduleBase  = 0;
    u64       moduleSize  = 0;
    HWND      window      = nullptr;

    // Engine strings (phase 1).
    u64 strShow   = 0;
    u64 strHide   = 0;
    u64 strToggle = 0;
    u64 strEch    = 0;

    // Extracted addresses (phase 3).
    u64 showConsoleFunc = 0;
    u64 hideConsoleFunc = 0;
    u64 toggleFunc      = 0;
    u64 gate1           = 0;
    u64 gate2           = 0;
    u64 globalCtxPtr    = 0;   // address OF the pointer, not the pointer
    u64 registerCmdFunc = 0;
    u64 setCallbackFunc = 0;
    u64 parentFuncAddr  = 0;
    u64 echDispatch     = 0;

    // Phase 4.
    u64 cmdObjShow = 0;
    u64 cmdObjHide = 0;

    // Captured by the early breakpoints' VEH. Two independent one-shots:
    // parentThis for the console handlers, debugThis for the 208 commands
    // registered by debugRegFunc. Different functions, different objects -
    // the console stub reads its owner at +0x08, the debug stubs at +0x30/+0x38.
    volatile u64 parentThis = 0;
    volatile u64 debugThis  = 0;

    // The PearlAbyssEngine.Debug.* registration function and the bind call it
    // uses, both resolved by name. See ../DEBUG_COMMANDS_20260905.md.
    u64 debugRegFunc  = 0;
    u64 debugBindFunc = 0;

    bool earlyPatched    = false;
    int  earlyPatchCount = 0;
    bool consoleVisible  = false;
};

extern GameState g_game;

std::string GameDirectory();

}  // namespace ch
