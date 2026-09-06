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
void NoticeTests()
{
    Config config;
    config.notifications = true;
    config.lightsExpected = true; config.renderedExpected = true;
    config.notificationDurationMs = 6000;
    config.enabled = false;
    const auto start = Clock::time_point{} + std::chrono::seconds(10);
    View view;
    NoticeTracker tracker;
    auto notice = tracker.Update(view, config, start);
    Require(notice && notice->title == "Starting telemetry" && !notice->error,
        "Notifications must work with the full HUD disabled");
    notice = tracker.Update(view, config, start + std::chrono::seconds(9));
    Require(notice && notice->title == "Starting telemetry", "Waiting notice must persist initially");
    notice = tracker.Update(view, config, start + std::chrono::seconds(30));
    Require(notice && notice->error && notice->title == "Telemetry host is not reachable" &&
        notice->detail.find("ASP.NET Core Runtime") != std::string::npos,
        "Host startup timeout must provide an actionable error");
    view.connected = true; view.hasSample = true; view.sample.state = "loading";
    notice = tracker.Update(view, config, start + std::chrono::seconds(31));
    Require(notice && notice->title == "Loading Crimson Desert", "Loading notice missing");
    view.sample.state = "playing";
    view.sample.playerPosition = Vec3{1, 2, 3}; view.sample.cameraPosition = Vec3{4, 5, 6};
    view.sample.authoredLights.status = "available"; view.sample.authoredLights.publishedRecords = 0;
    view.sample.renderedLights.status = "available"; view.sample.renderedLights.publishedRecords = 0;
    const auto fresh = [&](Clock::time_point when)
    {
        view.received = when;
        ++view.sample.sequence;
        return tracker.Update(view, config, when);
    };
    notice = fresh(start + std::chrono::seconds(32));
    Require(notice && notice->title == "Telemetry is ready" && !notice->error,
        "Loading-to-ready success notice missing");
    view.sample.renderedLights.publishedRecords = 400;
    Require(!fresh(start + std::chrono::seconds(39)), "Sequence/count changes retriggered the success notice");
    view.sample.renderedLights.status = "unavailable";
    view.sample.renderedLights.unavailableReason = "legacy-plugin-conflict";
    notice = fresh(start + std::chrono::seconds(40));
    Require(notice && notice->error && notice->detail.find("mod manager") != std::string::npos,
        "Legacy conflict must explain the corrective action");
    notice = fresh(start + std::chrono::seconds(70));
    Require(notice && notice->error, "Actionable error vanished before recovery");
    view.sample.renderedLights.status = "available";
    view.sample.renderedLights.unavailableReason.clear();
    notice = fresh(start + std::chrono::seconds(71));
    Require(notice && notice->title == "Telemetry is ready", "Recovery should emit one new ready notice");
    Require(!fresh(start + std::chrono::seconds(78)), "Recovery ready notice did not expire");
    // A brief interruption must neither replace the screen with a stale notice
    // nor cause another success toast when the next sample arrives.
    Require(!tracker.Update(view, config, start + std::chrono::milliseconds(80001)),
        "Transient stale data was not debounced");
    Require(!fresh(start + std::chrono::milliseconds(80500)), "Brief stale recovery retriggered ready");
    Require(!tracker.Update(view, config, start + std::chrono::seconds(83)), "Stale debounce did not start");
    notice = tracker.Update(view, config, start + std::chrono::seconds(84));
    Require(notice && notice->title == "Waiting for fresh telemetry", "Persistent stale data was hidden");
    notice = fresh(start + std::chrono::seconds(85));
    Require(notice && notice->title == "Telemetry is ready", "Stale recovery was not reported");
    view.sample.state = "loading";
    notice = fresh(start + std::chrono::seconds(86));
    Require(notice && notice->title == "Loading Crimson Desert", "Second load did not restart notices");
    view.sample.state = "playing";
    notice = fresh(start + std::chrono::seconds(87));
    Require(notice && notice->title == "Telemetry is ready", "Second loaded scene did not report ready");
    config.notifications = false;
    Require(!fresh(start + std::chrono::seconds(88)), "Disabled notices must disappear");

    config.notifications = true;
    view.sample.renderedLights.status = "unavailable";
    view.sample.renderedLights.unavailableReason = "bridge-waiting";
    notice = fresh(start + std::chrono::seconds(90));
    Require(notice && !notice->error, "Initial light discovery should wait");
    notice = fresh(start + std::chrono::seconds(101));
    Require(notice && notice->error && notice->title == "Light capture has no fresh sample",
        "A progressing scene with permanently missing light samples needs an error");
    notice = fresh(start + std::chrono::seconds(102));
    Require(notice && notice->error, "Light timeout error oscillated back to waiting");
    config.renderedExpected = false;
    view.sample.renderedLights.unavailableReason = "game-stopped";
    notice = fresh(start + std::chrono::seconds(103));
    Require(notice && notice->title == "Telemetry is ready" && !notice->error,
        "Intentionally disabled ManyLights must not become a capture error");

    Config healthConfig; healthConfig.notifications = true;
    View unhealthy; unhealthy.healthStatus = "unsupported-build";
    unhealthy.healthError = "Install the updated telemetry plugin.";
    NoticeTracker healthTracker;
    notice = healthTracker.Update(unhealthy, healthConfig, start);
    Require(notice && notice->error && notice->detail == unhealthy.healthError,
        "Unsupported build must be visible before any sample exists");
    View oldHost;
    oldHost.connected = true; oldHost.hasSample = true; oldHost.received = start;
    oldHost.sample.state = "playing"; oldHost.sample.schemaVersion = "1.1";
    oldHost.sample.playerPosition = Vec3{1, 2, 3}; oldHost.sample.cameraPosition = Vec3{4, 5, 6};
    Config expected; expected.notifications = true;
    NoticeTracker expectedTracker;
    notice = expectedTracker.Update(oldHost, expected, start);
    Require(notice && notice->title == "Telemetry is ready" && !notice->error,
        "An older host remains compatible when lights are not requested");
    expected.lightsExpected = true; expected.renderedExpected = true;
    notice = expectedTracker.Update(oldHost, expected, start);
    Require(notice && notice->error && notice->title == "Requested light data is missing",
        "An older host must not report ready when requested lights are missing");
    oldHost.sample.schemaVersion = "1.3";
    oldHost.sample.authoredLights.status = "available";
    notice = expectedTracker.Update(oldHost, expected, start);
    Require(notice && notice->error, "Missing requested rendered feed was hidden by authored light availability");
    expected.renderedExpected = false;
    notice = expectedTracker.Update(oldHost, expected, start);
    Require(notice && !notice->error && notice->title == "Telemetry is ready",
        "An intentionally unrequested rendered feed prevented readiness");
    std::cout << "PASS notification lifecycle, error recovery, debounce and HUD independence\n";
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
        Require(!ReadTestConfig(nullptr).notifications && !ReadTestConfig("").notifications,
            "Missing/empty configuration must not silently install notification graphics hooks");
        const auto notices = ReadTestConfig("[Notifications]\nEnabled=1\nDurationMilliseconds=7000\n[Server]\nPort=27329\n[Lights]\nEnabled=1\nManyLights=0\n");
        Require(notices.notifications && !notices.enabled && notices.notificationDurationMs == 7000 &&
            notices.port == 27329 && notices.lightsExpected && !notices.renderedExpected,
            "Notification-only INI must load server/light expectations without enabling the full HUD");
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
        for (const auto* version : {"1.2", "1.3", "1.4"})
        {
            json["schemaVersion"] = version;
            sample = ParseSample(json.dump(), now);
            Require(sample.playerPosition.has_value() && sample.schemaVersion == version,
                "Additive schema compatibility and displayed version");
        }
        json["lights"] = {{"status", "available"}, {"sources", nlohmann::json::array({{{"position", "not decoded by HUD"}}})},
            {"rendered", {{"status", "unavailable"}, {"unavailableReason", "bridge-waiting"}}}};
        sample = ParseSample(json.dump(), now);
        Require(sample.authoredLights.status == "available" && sample.authoredLights.publishedRecords == 1 &&
            sample.renderedLights.status == "unavailable" && sample.renderedLights.unavailableReason == "bridge-waiting" &&
            !sample.renderedLights.publishedRecords, "Optional light status/count summary");
        json["lights"]["rendered"] = {{"status", "available"}, {"sources", nlohmann::json::array()}, {"ageMilliseconds", 125}};
        json["lights"]["status"] = "broken";
        sample = ParseSample(json.dump(), now);
        Require(sample.cameraPosition && sample.authoredLights.status == "invalid" &&
            sample.renderedLights.status == "available" && sample.renderedLights.publishedRecords == 0 &&
            sample.renderedLights.ageMilliseconds == 125,
            "Malformed authored lights must not invalidate camera or rendered light summary");
        json["lights"]["status"] = "available";
        json["lights"]["rendered"]["sources"] = 17;
        sample = ParseSample(json.dump(), now);
        Require(sample.playerPosition && sample.cameraPosition && sample.authoredLights.status == "available" &&
            sample.renderedLights.status == "invalid" && !sample.renderedLights.publishedRecords,
            "Malformed rendered lights must fail independently");
        json.erase("lights");
        sample = ParseSample(json.dump(), now);
        Require(sample.authoredLights.status == "not-reported" && sample.renderedLights.status == "not-reported" &&
            !sample.authoredLights.publishedRecords, "Missing light module must not become an OFF/zero state");
        std::cout << "PASS additive schemas and independently optional light summaries\n";
        json["schemaVersion"] = "1.1";
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
        NoticeTests();
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
