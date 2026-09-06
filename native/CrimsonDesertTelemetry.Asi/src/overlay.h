#pragma once
#include <windows.h>
#include <filesystem>
#include "overlay_model.h"

namespace cdt::overlay
{
// HUD and separate startup notices each opt in. Both off means no UI hooks/client.
Config LoadConfig(const std::filesystem::path& ini);
// Called only from a bootstrap worker, never from DllMain itself.
void Start(HMODULE module, HANDLE stopEvent, const std::filesystem::path& directory) noexcept;
bool TryRead(View& view);
void Publish(View view);
// Blocking receive loop for a dedicated worker; the ASI keeps it for process lifetime.
void RunClient(Config config, HANDLE stopEvent);
void DrawHud(const View& view, const Config& config, bool details);
void DrawNotice(const Notice& notice, const Config& config, bool hudVisible, bool details);

// The graphics runtime is process-lifetime. No hot-unload is supported.
bool InstallGraphics(const Config& config) noexcept;
void MaintainGraphics() noexcept;
const char* GraphicsStatus() noexcept;
unsigned long long RenderedFrames() noexcept;
void SetVisibleForTest(bool visible) noexcept;
}
