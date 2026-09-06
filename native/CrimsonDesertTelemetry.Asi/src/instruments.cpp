#include "instruments.h"
#include "build_guard.h"
#include "render_capture.h"
#include "render_bridge.h"
#include "native_contract.generated.h"
#include "overlay.h"
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

void RunImpl(HANDLE stopEvent)
{
    if (duplicate) return;
    ch::OpenLog(moduleDirectory);
    ch::LogConfig();
    ch::bp::ReportStartup();
    if (!render::OpenBridge())
    {
        ch::Log("Native bridge already owned or unavailable; instrumentation skipped.");
        overlay::SetLocalFault("native-capture", "Native telemetry bridge unavailable",
            "A duplicate telemetry producer or a shared-memory failure prevents capture. Check the installed ASIs and native log, then restart the game.");
        return;
    }
    if (legacy || LegacyPresent())
    {
        ch::Log("Native instruments refused: legacy CrimsonHueConsole is still installed/loaded. Archive the old ASI and restart.");
        render::PublishStatus(render::Status::LegacyConflict, ERROR_ALREADY_EXISTS);
        overlay::SetLocalFault("native-capture", "Conflicting legacy console plugin",
            "CrimsonHueConsole is still installed or loaded. Archive the old ASI and restart with only the integrated CrimsonDesertTelemetry plugin.");
        return;
    }
    if (earlyFailed)
    {
        render::PublishStatus(render::Status::Fault, ERROR_DLL_INIT_FAILED);
        overlay::SetLocalFault("native-capture", "Native configuration or startup failed",
            "The plugin could not initialize its configuration or module context. Check CrimsonDesertTelemetry.ini and the native log, then restart.");
        return;
    }
    const bool captureEnabled = GetPrivateProfileIntW(L"Lights", L"Enabled", 0, iniPath.c_str()) != 0 &&
        GetPrivateProfileIntW(L"Lights", L"ManyLights", 1, iniPath.c_str()) != 0;
    if (!requested && !captureEnabled)
    {
        render::PublishStatus(render::Status::Stopped);
        overlay::ClearLocalFault("native-capture");
        return;
    }
    if (!hashChecked) { hashChecked = true; verified = VerifyExecutable(); }
    if (!verified)
    {
        ch::Log("Native game instrumentation disabled: executable SHA256 differs from validated build %s.", native_contract::BuildId.data());
        render::PublishStatus(render::Status::Incompatible, ERROR_REVISION_MISMATCH);
        overlay::SetLocalFault("native-capture", "This game build is not validated for native lights",
            "Native hooks remain disabled. Install a telemetry version validated for this game build; check-update can inspect compatibility without enabling hooks.");
        return;
    }
    PinModule();
    if (!ch::mem::GetModuleRange(nullptr, &ch::g_game.moduleBase, &ch::g_game.moduleSize))
    {
        render::PublishStatus(render::Status::Fault, ERROR_MOD_NOT_FOUND);
        overlay::SetLocalFault("native-capture", "Game module unavailable",
            "The plugin could not resolve the game image. Check the native log and restart the game; no capture hook was installed.");
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
            const auto preflight = render::CheckCapturePreflight(ch::g_game.moduleBase);
            overlay::SetLocalFault("native-capture", "Native light capture refused initialization",
                !preflight ? std::string("Build context check failed: ") + render::PreflightFailureName(preflight.failure) +
                    ". No new capture hook was installed. Check for conflicting mods or use a telemetry release validated for this game build." :
                    "The validated capture hook could not initialize. Check the native log for MinHook or graphics errors and restart the game.");
        }
        else overlay::ClearLocalFault("native-capture");
    }
    else overlay::ClearLocalFault("native-capture");
    uint32_t reportedCaptureError = 0;
    while (WaitForSingleObject(stopEvent, 5) == WAIT_TIMEOUT)
        if (capturing)
        {
            render::PollCapture();
            const auto failure = render::CaptureFailureCode();
            if (failure && failure != reportedCaptureError)
            {
                reportedCaptureError = failure;
                overlay::SetLocalFault("native-capture", "Native light capture stopped safely",
                    "Capture error " + std::to_string(failure) +
                    ". Check the native log for device, queue or readback failure, then restart. Old samples are not current light data.");
            }
        }
    if (capturing) render::StopCapture();
    else render::PublishStatus(render::Status::Stopped);
    // No blocking cleanup under loader lock. Stop() is on this worker.
    if (ch::g_cfg.enableExplorer) ch::explorer::Stop();
    ch::console::UnhookWndProc();
    ch::discovery::DisarmEarlyBreakpoint();
}
void Run(HANDLE stopEvent)
{
    try { RunImpl(stopEvent); }
    catch (...)
    {
        ch::Log("Native instrumentation worker failed unexpectedly; capture cannot continue.");
        render::PublishStatus(render::Status::Fault, ERROR_UNHANDLED_EXCEPTION);
        overlay::SetLocalFault("native-capture", "Native telemetry worker failed",
            "Native telemetry could not continue. Check the native log and restart the game; stale samples must not be treated as current lights.");
    }
}
bool OwnsCodeAddress(uint64_t address) { return render::OwnsCodeAddress(address); }
}
