// Logging, configuration and process-wide state.
#include "common.h"
#include "mem.h"

#include <cstdarg>
#include <cctype>
#include <ctime>

namespace ch {

Config    g_cfg;
GameState g_game;

static FILE* g_log    = nullptr;
static FILE* g_result = nullptr;
static CRITICAL_SECTION g_logLock;
static bool g_lockReady = false;

std::string GameDirectory() {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string s(path);
    const size_t slash = s.find_last_of('\\');
    return slash == std::string::npos ? std::string(".") : s.substr(0, slash);
}

void OpenLog(const std::string& gameDir) {
    if (!g_lockReady) { InitializeCriticalSection(&g_logLock); g_lockReady = true; }
    const std::string path = gameDir + "\\CrimsonDesertTelemetry.native.log";
    g_log = fopen(path.c_str(), "w");
    if (!g_log) return;

    time_t now = time(nullptr);
    char stamp[64] = {};
    struct tm tmv{};
    if (localtime_s(&tmv, &now) == 0)
        strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tmv);

    Log("=== CrimsonDesertTelemetry integrated native instruments ===");
    Log("Console research imported from CrimsonHueConsole 0.3.17; originals preserved.");
    Log("Log opened at: %s  (%s)", path.c_str(), stamp);
}

void CloseLog() {
    if (g_log) { fclose(g_log); g_log = nullptr; }
}

void SetResultFile(FILE* f) { g_result = f; }

static void Emit(FILE* f, const char* fmt, va_list ap) {
    if (!f) return;
    vfprintf(f, fmt, ap);
    fputc('\n', f);
    fflush(f);
}

void Log(const char* fmt, ...) {
    if (!g_log) return;
    if (g_lockReady) EnterCriticalSection(&g_logLock);
    va_list ap;
    va_start(ap, fmt);
    Emit(g_log, fmt, ap);
    va_end(ap);
    if (g_lockReady) LeaveCriticalSection(&g_logLock);
}

void Out(const char* fmt, ...) {
    if (g_lockReady) EnterCriticalSection(&g_logLock);
    va_list ap;
    va_start(ap, fmt);
    Emit(g_log, fmt, ap);
    va_end(ap);
    if (g_result) {
        va_start(ap, fmt);
        Emit(g_result, fmt, ap);
        va_end(ap);
    }
    if (g_lockReady) LeaveCriticalSection(&g_logLock);
}

void LogHexDump(const void* addr, int rows, const char* label) {
    const u64 a = reinterpret_cast<u64>(addr);
    Log("%s (0x%llX):", label ? label : "dump", a);
    for (int r = 0; r < rows; ++r) {
        const u64 line = a + static_cast<u64>(r) * 16;
        u8 buf[16] = {};
        if (!mem::SafeRead(reinterpret_cast<const void*>(line), buf, 16)) {
            Log("  0x%llX: <inaccessible>", line);
            continue;
        }
        char hex[16 * 3 + 1] = {};
        char asc[17] = {};
        for (int i = 0; i < 16; ++i) {
            sprintf_s(hex + i * 3, 4, "%02X ", buf[i]);
            asc[i] = (buf[i] >= 0x20 && buf[i] < 0x7F) ? static_cast<char>(buf[i]) : '.';
        }
        Log("  0x%llX: %s | %s", line, hex, asc);
    }
}

// ------------------------------------------------------------------ config --

static std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

static bool ParseBool(const std::string& v, bool def) {
    if (v.empty()) return def;
    const char c = static_cast<char>(tolower(static_cast<unsigned char>(v[0])));
    if (c == '1' || c == 'y' || c == 't') return true;
    if (c == '0' || c == 'n' || c == 'f') return false;
    return def;
}

// Read in DllMain, before the log file exists: EarlyPatch() runs there and has
// to honour PatchGates / EarlyBreakpoint. Nothing here logs; LogConfig() does
// that once the log is open.
static bool g_configFound  = false;
static int  g_configApplied = 0;

void LoadConfig(const std::string& gameDir) {
    const std::string path = gameDir + "\\CrimsonDesertTelemetry.ini";
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return;
    g_configFound = true;
    char line[512];
    int applied = 0;
    while (fgets(line, sizeof(line), f)) {
        std::string s = Trim(line);
        if (s.empty() || s[0] == ';' || s[0] == '#' || s[0] == '[') continue;
        const size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string k = Trim(s.substr(0, eq));
        std::string v = Trim(s.substr(eq + 1));
        for (auto& c : k) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

        if      (k == "enableconsole")    g_cfg.enableConsole    = ParseBool(v, true);
        else if (k == "patchgates")       g_cfg.patchGates       = ParseBool(v, true);
        else if (k == "patchdevflags")    g_cfg.patchDevFlags    = ParseBool(v, true);
        else if (k == "earlybreakpoint")  g_cfg.earlyBreakpoint  = ParseBool(v, true);
        else if (k == "hidenotification") g_cfg.hideNotification = ParseBool(v, true);
        else if (k == "enableexplorer")   g_cfg.enableExplorer   = ParseBool(v, true);
        else if (k == "allowwrite")       g_cfg.allowWrite       = ParseBool(v, true);
        else if (k == "allowcall")        g_cfg.allowCall        = ParseBool(v, true);
        else if (k == "allowdebugcommands") g_cfg.allowDebugCommands = ParseBool(v, true);
        else if (k == "allowbreakpoints")   g_cfg.allowBreakpoints   = ParseBool(v, true);
        else if (k == "allowmanylights")    g_cfg.allowManyLights    = ParseBool(v, false);
        else if (k == "verbosediscovery") g_cfg.verboseDiscovery = ParseBool(v, true);
        else if (k == "initdelayms")      g_cfg.initDelayMs      = strtoul(v.c_str(), nullptr, 10);
        else if (k == "pollintervalms")   g_cfg.pollIntervalMs   = strtoul(v.c_str(), nullptr, 10);
        else if (k == "cmdfile")          g_cfg.cmdFile          = v;
        else if (k == "resultfile")       g_cfg.resultFile       = v;
        else continue;
        ++applied;
    }
    fclose(f);
    g_configApplied = applied;
}

void LogConfig() {
    if (g_configFound)
        Log("Loaded CrimsonDesertTelemetry.ini (%d research setting(s) applied)", g_configApplied);
    else
        Log("No CrimsonDesertTelemetry.ini next to the ASI; console/explorer disabled by default.");
    Log("  console=%d gates=%d devflags=%d earlyBP=%d hideNotify=%d "
        "explorer=%d write=%d call=%d debugcmds=%d bp=%d manylights=%d initDelay=%ums",
        g_cfg.enableConsole, g_cfg.patchGates, g_cfg.patchDevFlags,
        g_cfg.earlyBreakpoint, g_cfg.hideNotification, g_cfg.enableExplorer,
        g_cfg.allowWrite, g_cfg.allowCall, g_cfg.allowDebugCommands, g_cfg.allowBreakpoints,
        g_cfg.allowManyLights, g_cfg.initDelayMs);
}

}  // namespace ch
