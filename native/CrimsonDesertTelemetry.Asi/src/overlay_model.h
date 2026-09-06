#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cdt::overlay
{
using Clock = std::chrono::steady_clock;
struct Vec3 { float x{}, y{}, z{}; };
struct LightRecord
{
    // Sample-local renderer index, never a persistent physical-light identity.
    int sampleIndex{};
    Vec3 position, colorLinear;
    float luminanceLinear{};
    std::string kind;
    std::optional<Vec3> direction;
    std::optional<float> coneHalfAngleDegrees;
};
struct LightSummary
{
    std::string status = "not-reported", unavailableReason;
    std::optional<std::uint32_t> publishedRecords;
    std::optional<double> ageMilliseconds;
    // Only the current rendered feed owns records. View copies share immutable
    // storage; unavailable/malformed/newly missing feeds never retain it.
    std::shared_ptr<const std::vector<LightRecord>> records;
};
struct Sample
{
    std::string state = "waiting", build, orientationSource, schemaVersion;
    std::int64_t sequence{};
    double sourceAgeMs{};
    std::optional<Vec3> playerPosition, playerForward, playerUp;
    std::optional<Vec3> cameraPosition, cameraForward, cameraUp, cameraRight;
    std::optional<float> playerHeading, cameraHeading, pitch, fov;
    std::optional<float> aspectRatio, nearPlane;
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
    bool lightOverlay = false, lightOverlayVisible = true, radar3D = true;
    int notificationDurationMs = 6000;
    int toggleKey = 0x77, detailsKey = 0x78, corner = 0, staleMs = 1000;
    int lightToggleKey = 0x79, lightMaxMarkers = 512, lightMaxLabels = 6;
    float scale = 1.0f, opacity = 0.92f;
    float lightRadius = 35.0f;
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
struct ScreenPoint { float x{}, y{}, depth{}; };
// World positions are already reconstructed with the capture-paired camera.
// Project with the newest envelope camera; leave viewport clipping to callers.
std::optional<ScreenPoint> ProjectWorld(Vec3 world, const Sample& sample, float width, float height);
double AgeMs(const View& view, Clock::time_point now);
bool IsLive(const View& view, Clock::time_point now, int staleMs);
bool RenderedLightsLive(const View& view, Clock::time_point now, int staleMs);
std::string LightFeedStatus(const View& view, Clock::time_point now, int staleMs);
std::string Status(const View& view, Clock::time_point now, int staleMs);
float HudScale(float width, float height, const Config& config, bool details);
}
