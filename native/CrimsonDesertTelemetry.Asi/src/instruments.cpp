#include "instruments.h"
#include "build_guard.h"
#include "render_capture.h"
#include "render_bridge.h"
#include "console/common.h"
#include "console/breakpoint.h"
#include "console/console.h"
#include "console/discovery.h"
#include "console/explorer.h"
#include "console/mem.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <string>

namespace cdt::instruments
{
namespace
{
HMODULE self{};
HANDLE singleton{};
std::wstring iniPath;
std::string moduleDirectory;
bool legacy{}, duplicate{}, verified{}, hashChecked{}, requested{}, earlyFailed{};
bool LegacyPresent()
{
    if (GetModuleHandleW(L"CrimsonHueConsole.asi") || GetModuleHandleW(L"CrimsonHueConsole.dll")) return true;
    // Also refuse a legacy ASI that the loader has not loaded yet. This makes
    // mixed installations safe independent of alphabetical loading order.
    const auto path = ch::GameDirectory() + "\\CrimsonHueConsole.asi";
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesA((moduleDirectory + "\\CrimsonHueConsole.asi").c_str()) != INVALID_FILE_ATTRIBUTES;
}
void PinModule()
{
    HMODULE pinned{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(self), &pinned);
}
}

void EarlyAttach(HMODULE module)
{
    self = module;
    try
    {
        std::array<wchar_t, 32768> path{};
        if (!GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()))) { earlyFailed = true; return; }
        const auto directory = std::filesystem::path(path.data()).parent_path();
        moduleDirectory = directory.string();
        iniPath = (directory / L"CrimsonDesertTelemetry.ini").wstring();
        ch::LoadConfig(moduleDirectory);
        const auto name = L"Local\\CrimsonDesertTelemetry.Native." + std::to_wstring(GetCurrentProcessId());
        singleton = CreateMutexW(nullptr, FALSE, name.c_str());
        duplicate = !singleton || GetLastError() == ERROR_ALREADY_EXISTS;
        legacy = LegacyPresent();
        if (duplicate || legacy) return;
        requested = ch::g_cfg.enableConsole || ch::g_cfg.enableExplorer;
        if (requested)
        {
            // The optional research startup capture must run before the game
            // executes registration. Hashing on this same thread preserves that
            // ordering; moving the arm to a delayed worker loses debugThis.
            hashChecked = true;
            verified = VerifyExecutable();
            if (!verified) return;
            PinModule();
            ch::discovery::EarlyPatch();
        }
    }
    catch (...) { earlyFailed = true; }
}

void Run(HANDLE stopEvent)
{
    if (duplicate) return;
    ch::OpenLog(moduleDirectory);
    ch::LogConfig();
    ch::bp::ReportStartup();
    if (!render::OpenBridge()) { ch::Log("Native bridge already owned or unavailable; instrumentation skipped."); return; }
    if (legacy || LegacyPresent())
    {
        ch::Log("Native instruments refused: legacy CrimsonHueConsole is still installed/loaded. Archive the old ASI and restart.");
        render::PublishStatus(render::Status::LegacyConflict, ERROR_ALREADY_EXISTS);
        return;
    }
    if (earlyFailed)
    {
        render::PublishStatus(render::Status::Fault, ERROR_DLL_INIT_FAILED);
        return;
    }
    const bool captureEnabled = GetPrivateProfileIntW(L"Lights", L"Enabled", 0, iniPath.c_str()) != 0 &&
        GetPrivateProfileIntW(L"Lights", L"ManyLights", 1, iniPath.c_str()) != 0;
    if (!requested && !captureEnabled) { render::PublishStatus(render::Status::Stopped); return; }
    if (!hashChecked) { hashChecked = true; verified = VerifyExecutable(); }
    if (!verified)
    {
        ch::Log("Native game instrumentation disabled: executable SHA256 differs from validated build 25116796.");
        render::PublishStatus(render::Status::Incompatible, ERROR_REVISION_MISMATCH);
        return;
    }
    PinModule();
    if (!ch::mem::GetModuleRange(nullptr, &ch::g_game.moduleBase, &ch::g_game.moduleSize))
    {
        render::PublishStatus(render::Status::Fault, ERROR_MOD_NOT_FOUND);
        return;
    }
    if (requested)
    {
        const DWORD delay = std::clamp(ch::g_cfg.initDelayMs, 1u, 30000u);
        if (WaitForSingleObject(stopEvent, delay) != WAIT_TIMEOUT) return;
        if (ch::g_cfg.enableConsole || (ch::g_cfg.enableExplorer && ch::g_cfg.allowDebugCommands))
        {
            for (unsigned i = 0; i < 300 && !ch::g_game.window; ++i)
            {
                ch::g_game.window = ch::console::FindGameWindow();
                if (!ch::g_game.window && WaitForSingleObject(stopEvent, 100) != WAIT_TIMEOUT) return;
            }
            const bool discovered = ch::discovery::Discover();
            ch::Log("Integrated research discovery completed: %s", discovered ? "ready" : "partial/unavailable");
            if (ch::g_cfg.enableConsole && ch::g_game.window)
            {
                ch::discovery::DumpRegisteredCommands();
                ch::console::HookWndProc(ch::g_game.window);
            }
        }
        else ch::discovery::DisarmEarlyBreakpoint();
        if (ch::g_cfg.enableExplorer) ch::explorer::Start();
    }
    bool capturing = false;
    if (captureEnabled)
    {
        const unsigned rate = GetPrivateProfileIntW(L"Lights", L"ManyLightsSampleRateHz", 20, iniPath.c_str());
        capturing = render::StartCapture(ch::g_game.moduleBase, rate);
        if (!capturing)
        {
            ch::Log("Automatic ManyLights instrumentation failed closed; authored telemetry remains independent.");
        }
    }
    while (WaitForSingleObject(stopEvent, 5) == WAIT_TIMEOUT)
        if (capturing) render::PollCapture();
    if (capturing) render::StopCapture();
    else render::PublishStatus(render::Status::Stopped);
    // No blocking cleanup under loader lock. Stop() is on this worker.
    if (ch::g_cfg.enableExplorer) ch::explorer::Stop();
    ch::console::UnhookWndProc();
    ch::discovery::DisarmEarlyBreakpoint();
}
bool OwnsCodeAddress(uint64_t address) { return render::OwnsCodeAddress(address); }
}
