#include "overlay.h"
#include <nlohmann/json.hpp>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace cdt::overlay;
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
Config ReadTestConfig(const char* contents)
{
    std::array<wchar_t, MAX_PATH> directory{}, filename{};
    const DWORD length = GetTempPathW(static_cast<DWORD>(directory.size()), directory.data());
    Require(length > 0 && length < directory.size(), "Temporary directory lookup");
    Require(GetTempFileNameW(directory.data(), L"cdt", 0, filename.data()) != 0, "Temporary INI creation");
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{filename.data()};
    if (contents)
    {
        std::ofstream file(cleanup.path, std::ios::trunc);
        file << contents;
        file.close();
        Require(file.good(), "Temporary INI write");
    }
    else Require(std::filesystem::remove(cleanup.path), "Missing-INI fixture setup");
    return LoadConfig(cleanup.path);
}
int main()
{
    try
    {
        Config config;
        Require(!config.enabled, "HUD must default to disabled");
        Require(!ReadTestConfig(nullptr).enabled, "Missing INI must disable HUD");
        Require(!ReadTestConfig("").enabled, "Empty INI must disable HUD");
        Require(!ReadTestConfig("[Server]\nEnabled=1\n").enabled, "Server enable must not enable HUD");
        Require(!ReadTestConfig("[Overlay]\nInitiallyVisible=1\n").enabled, "Visibility must not implicitly enable HUD");
        Require(!ReadTestConfig("[Overlay]\nEnabled=0\nInitiallyVisible=1\n").enabled, "Explicitly disabled HUD");
        const auto enabled = ReadTestConfig("[Server]\nPort=27329\n[Overlay]\nEnabled=1\n");
        Require(enabled.enabled && enabled.visible && enabled.port == 27329, "Explicit opt-in preserves HUD configuration");
        const auto hidden = ReadTestConfig("[Overlay]\nEnabled=1\nInitiallyVisible=0\n");
        Require(hidden.enabled && !hidden.visible && hidden.toggleKey == 119, "Initially hidden HUD must remain toggleable");
        std::cout << "PASS HUD defaults off, missing configuration, explicit opt-in and hidden mode\n";
        Require(std::abs(HudScale(1920, 1080, config, false) - 1.0f) < 0.001f, "1080p HUD scale");
        Require(std::abs(HudScale(3840, 2160, config, false) - 2.0f) < 0.001f, "4K HUD scale");
        Require(std::abs(HudScale(2560, 1440, config, false) - 4.0f / 3.0f) < 0.001f, "1440p HUD scale");
        config.autoScale = false;
        Require(std::abs(HudScale(3840, 2160, config, false) - 1.0f) < 0.001f, "Manual HUD scale");
        config.autoScale = true;
        Require(HudScale(800, 480, config, true) * 640.0f <= 480.1f, "Diagnostics must fit small screens");
        Require(HudScale(0, 0, config, false) == 0, "Minimized surface");
        std::cout << "PASS HUD resolution scaling and panel bounds\n";
        const auto now = std::chrono::system_clock::from_time_t(1788091200); // 2026-08-30T12:00:00Z
        auto json = nlohmann::json::parse(R"({"schemaVersion":"1.1","sequence":1,"capturedAt":"2026-08-30T12:00:00+00:00",
            "game":{"build":"test","state":"playing"},"coordinateSystem":{"unit":"game-unit","handedness":"right","upAxis":"y"},
            "capabilities":["player.position","player.orientation","camera"],
            "player":{"position":{"x":123.0,"y":456.0,"z":789.0},"orientation":{"source":"player-physics-root",
                "forward":{"x":1,"y":0,"z":0},"up":{"x":0,"y":1,"z":0},"headingDegrees":90}},
            "camera":{"position":{"x":1,"y":2,"z":3},"forward":{"x":0,"y":0,"z":1},"up":{"x":0,"y":1,"z":0},
                "right":{"x":1,"y":0,"z":0},"verticalFovDegrees":60},
            "quality":{"consensusCopies":3,"validCopies":4,"distinctStates":1,"rediscovered":false,"captureDurationMicroseconds":120}})");
        auto sample = ParseSample(json.dump(), now);
        Require(sample.playerPosition && sample.playerPosition->x == 123, "Player position");
        Require(sample.playerHeading && std::abs(*sample.playerHeading - 90) < 0.01, "Independent player heading");
        Require(sample.cameraHeading && std::abs(*sample.cameraHeading) < 0.01, "Independent camera heading");
        Require(!Heading({0, 1, 0}), "Vertical direction must not produce a heading");
        std::cout << "PASS overlay camera/player separation and vertical projection\n";
        View view;
        view.sample = sample; view.connected = true; view.hasSample = true; view.received = Clock::now();
        Require(IsLive(view, view.received, 1000), "Fresh data");
        Require(!IsLive(view, view.received + std::chrono::seconds(2), 1000), "Stale data");
        view.connected = false;
        Require(!IsLive(view, view.received, 1000), "Disconnected data");
        std::cout << "PASS overlay freshness and disconnect invalidation\n";
        json["game"]["state"] = "loading";
        sample = ParseSample(json.dump(), now);
        Require(!sample.playerPosition && !sample.cameraForward, "Loading must clear positions");
        json["game"]["state"] = "playing";
        json["player"]["orientation"] = nullptr;
        sample = ParseSample(json.dump(), now);
        Require(sample.playerPosition && !sample.playerHeading, "No invented orientation");
        std::cout << "PASS overlay loading and optional orientation\n";
        const auto reject = [&](nlohmann::json invalid)
        {
            bool rejected = false;
            try { ParseSample(invalid.dump(), now); } catch (...) { rejected = true; }
            Require(rejected, "Invalid sample accepted");
        };
        auto invalid = json; invalid["schemaVersion"] = "2.0"; reject(invalid);
        invalid = json; invalid["coordinateSystem"]["upAxis"] = "z"; reject(invalid);
        invalid = json; invalid["camera"]["forward"]["z"] = 50; reject(invalid);
        invalid = json; invalid["capturedAt"] = "tomorrow"; reject(invalid);
        invalid = json; invalid["capturedAt"] = "2027-08-30T12:00:00Z"; reject(invalid);
        std::cout << "PASS overlay malformed/schema/coordinate/timestamp rejection\n";
        json["capturedAt"] = "2026-08-30T11:59:58.5000000+00:00";
        sample = ParseSample(json.dump(), now);
        Require(std::abs(sample.sourceAgeMs - 1500) < 0.1, "Source timestamp age");
        std::cout << "PASS overlay source age includes time before receipt\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
