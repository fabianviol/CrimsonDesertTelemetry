#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cdt::overlay
{
using Clock = std::chrono::steady_clock;
struct Vec3 { float x{}, y{}, z{}; };
struct LightSummary
{
    std::string status = "not-reported", unavailableReason;
    std::optional<std::uint32_t> publishedRecords;
    std::optional<double> ageMilliseconds;
};
struct Sample
{
    std::string state = "waiting", build, orientationSource, schemaVersion;
    std::int64_t sequence{};
    double sourceAgeMs{};
    std::optional<Vec3> playerPosition, playerForward, playerUp;
    std::optional<Vec3> cameraPosition, cameraForward, cameraUp, cameraRight;
    std::optional<float> playerHeading, cameraHeading, pitch, fov;
    int consensus{}, validCopies{}, distinctStates{};
    std::int64_t captureUs{};
    bool rediscovered{};
    LightSummary authoredLights, renderedLights;
};
struct View
{
    Sample sample;
    Clock::time_point received{};
    bool connected{}, hasSample{};
    std::string connection = "CONNECTING";
    std::string healthStatus, healthError;
    double rateHz{};
};
struct Config
{
    bool enabled = false, visible = true, details = false, autoScale = true;
    bool notifications = false;
    bool lightsExpected = false, renderedExpected = false;
    int notificationDurationMs = 6000;
    int toggleKey = 0x77, detailsKey = 0x78, corner = 0, staleMs = 1000;
    float scale = 1.0f, opacity = 0.92f;
    unsigned short port = 27311;
};
struct Notice
{
    std::string title, detail;
    bool error = false;
};
class NoticeTracker
{
public:
    std::optional<Notice> Update(const View& view, const Config& config, Clock::time_point now);
private:
    std::string currentKey_, pendingKey_, observedKey_;
    std::optional<Notice> current_;
    Clock::time_point shownAt_{}, pendingAt_{}, observedAt_{};
    bool persistent_{};
};
// Reject malformed/oversize/incompatible data; never interpret missing vectors as zero.
Sample ParseSample(std::string_view json, std::chrono::system_clock::time_point now);
std::optional<float> Heading(Vec3 direction);
double AgeMs(const View& view, Clock::time_point now);
bool IsLive(const View& view, Clock::time_point now, int staleMs);
std::string Status(const View& view, Clock::time_point now, int staleMs);
float HudScale(float width, float height, const Config& config, bool details);
}
