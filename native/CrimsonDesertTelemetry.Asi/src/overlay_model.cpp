#include "overlay_model.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <numbers>
#include <stdexcept>

namespace cdt::overlay
{
namespace
{
using Json = nlohmann::json;
constexpr size_t MaximumTelemetryMessageBytes = 4 * 1024 * 1024;
LightSummary ReadLights(const Json& value, std::uint32_t maximum)
{
    LightSummary result;
    try
    {
        if (!value.is_object()) throw std::runtime_error("Invalid optional light object");
        result.status = value.at("status").get<std::string>();
        if (result.status == "available")
        {
            const auto& sources = value.at("sources");
            if (!sources.is_array() || sources.size() > maximum)
                throw std::runtime_error("Invalid optional light count");
            result.publishedRecords = static_cast<std::uint32_t>(sources.size());
            if (value.contains("ageMilliseconds"))
            {
                const auto& age = value.at("ageMilliseconds");
                if (!age.is_number()) throw std::runtime_error("Invalid optional light age");
                result.ageMilliseconds = age.get<double>();
                if (!std::isfinite(*result.ageMilliseconds) || *result.ageMilliseconds < 0)
                    throw std::runtime_error("Invalid optional light age");
            }
        }
        else if (result.status == "unavailable")
        {
            result.unavailableReason = value.at("unavailableReason").get<std::string>();
            if (result.unavailableReason.empty() || result.unavailableReason.size() > 128)
                throw std::runtime_error("Invalid optional light reason");
            // No source count is reported for unavailable data; absent data is
            // not a zero count and says nothing about the lamps' ON/OFF state.
        }
        else throw std::runtime_error("Unknown optional light status");
    }
    catch (const std::exception&)
    {
        result = {};
        result.status = "invalid";
        result.unavailableReason = "invalid-light-summary";
    }
    return result;
}
float Number(const Json& value)
{
    const float result = value.get<float>();
    if (!std::isfinite(result)) throw std::runtime_error("Non-finite telemetry");
    return result;
}
Vec3 Vector(const Json& value)
{
    return {Number(value.at("x")), Number(value.at("y")), Number(value.at("z"))};
}
Vec3 Direction(const Json& value)
{
    const auto result = Vector(value);
    const float length = std::sqrt(result.x * result.x + result.y * result.y + result.z * result.z);
    if (length < 0.9f || length > 1.1f) throw std::runtime_error("Invalid direction");
    return result;
}
double SourceAge(const std::string& text, const std::chrono::system_clock::time_point now)
{
    // The v1 contract emits UTC ISO 8601, with optional fractional seconds.
    std::tm tm{};
    int consumed{};
    if (sscanf_s(text.c_str(), "%d-%d-%dT%d:%d:%d%n", &tm.tm_year, &tm.tm_mon,
        &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &consumed) != 6)
        throw std::runtime_error("Invalid capture timestamp");
    if (tm.tm_year < 2020 || tm.tm_mon < 1 || tm.tm_mon > 12 || tm.tm_mday < 1 ||
        tm.tm_mday > 31 || tm.tm_hour > 23 || tm.tm_min > 59 || tm.tm_sec > 59)
        throw std::runtime_error("Invalid capture timestamp range");
    size_t offset = static_cast<size_t>(consumed);
    double fraction{}, place = 0.1;
    if (offset < text.size() && text[offset] == '.')
    {
        ++offset;
        const size_t first = offset;
        while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9')
        {
            fraction += static_cast<double>(text[offset++] - '0') * place;
            place *= 0.1;
        }
        if (offset == first) throw std::runtime_error("Invalid timestamp fraction");
    }
    const auto zone = text.substr(offset);
    if (zone != "Z" && zone != "+00:00") throw std::runtime_error("Non-UTC timestamp");
    tm.tm_year -= 1900;
    --tm.tm_mon;
    const auto seconds = _mkgmtime64(&tm);
    if (seconds < 0) throw std::runtime_error("Invalid capture time");
    const double age = std::chrono::duration<double, std::milli>(now.time_since_epoch()).count()
        - (static_cast<double>(seconds) + fraction) * 1000.0;
    if (age < -2000.0) throw std::runtime_error("Capture timestamp is in the future");
    return std::max(0.0, age);
}
}

std::optional<float> Heading(const Vec3 direction)
{
    if (std::hypot(direction.x, direction.z) < 0.1f) return std::nullopt;
    const float angle = std::atan2(direction.x, direction.z) * 180.0f / std::numbers::pi_v<float>;
    return std::fmod(angle + 360.0f, 360.0f);
}

float HudScale(float width, float height, const Config& config, bool details)
{
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) return 0;
    const float resolution = config.autoScale ? std::max(1.0f, height / 1080.0f) : 1.0f;
    // Scale in physical render-target pixels: a 4K screen needs twice the 1080p size.
    // Keep lower-resolution displays readable; only shrink if the panel would clip.
    const float naturalHeight = details ? 600.0f : 344.0f;
    return std::min({config.scale * resolution, width / 550.0f, height / (naturalHeight + 40.0f)});
}

Sample ParseSample(const std::string_view text, const std::chrono::system_clock::time_point now)
{
    if (text.empty() || text.size() > MaximumTelemetryMessageBytes) throw std::runtime_error("Invalid message size");
    const auto root = Json::parse(text, [](int depth, Json::parse_event_t, Json&)
    {
        if (depth > 24) throw std::runtime_error("Telemetry nesting limit exceeded");
        return true;
    });
    const auto schema = root.at("schemaVersion").get<std::string>();
    if (schema != "1.1" && schema != "1.2" && schema != "1.3" && schema != "1.4")
        throw std::runtime_error("Unsupported telemetry schema");
    const auto& axes = root.at("coordinateSystem");
    if (axes.at("upAxis") != "y" || axes.at("handedness") != "right" || axes.at("unit") != "game-unit")
        throw std::runtime_error("Unsupported coordinate system");
    Sample sample;
    sample.schemaVersion = schema;
    sample.sequence = root.at("sequence").get<std::int64_t>();
    sample.state = root.at("game").at("state").get<std::string>();
    sample.build = root.at("game").at("build").get<std::string>();
    if (sample.state.size() > 64 || sample.build.size() > 128 || sample.sequence < 0)
        throw std::runtime_error("Invalid sample metadata");
    sample.sourceAgeMs = SourceAge(root.at("capturedAt").get<std::string>(), now);
    if (root.contains("lights"))
    {
        const auto& lights = root.at("lights");
        sample.authoredLights = ReadLights(lights, 8192);
        if (lights.is_object() && lights.contains("rendered"))
            sample.renderedLights = ReadLights(lights.at("rendered"), 32768);
    }
    // Non-playing samples must not accidentally show coordinates from an older state.
    if (sample.state != "playing") return sample;
    const auto& player = root.at("player");
    if (!player.is_null())
    {
        sample.playerPosition = Vector(player.at("position"));
        if (player.contains("orientation") && !player.at("orientation").is_null())
        {
            const auto& orientation = player.at("orientation");
            sample.orientationSource = orientation.at("source").get<std::string>();
            if (sample.orientationSource != "player-physics-root")
                throw std::runtime_error("Unknown orientation source");
            sample.playerForward = Direction(orientation.at("forward"));
            sample.playerUp = Direction(orientation.at("up"));
            sample.playerHeading = Heading(*sample.playerForward);
        }
    }
    const auto& camera = root.at("camera");
    if (!camera.is_null())
    {
        sample.cameraPosition = Vector(camera.at("position"));
        sample.cameraForward = Direction(camera.at("forward"));
        sample.cameraUp = Direction(camera.at("up"));
        sample.cameraRight = Direction(camera.at("right"));
        sample.cameraHeading = Heading(*sample.cameraForward);
        sample.pitch = std::asin(std::clamp(sample.cameraForward->y, -1.0f, 1.0f)) * 180.0f / std::numbers::pi_v<float>;
        sample.fov = Number(camera.at("verticalFovDegrees"));
        if (*sample.fov <= 0 || *sample.fov >= 180) throw std::runtime_error("Invalid FOV");
    }
    if (root.contains("quality") && !root.at("quality").is_null())
    {
        const auto& q = root.at("quality");
        sample.consensus = q.at("consensusCopies").get<int>();
        sample.validCopies = q.at("validCopies").get<int>();
        sample.distinctStates = q.at("distinctStates").get<int>();
        sample.captureUs = q.at("captureDurationMicroseconds").get<std::int64_t>();
        sample.rediscovered = q.at("rediscovered").get<bool>();
    }
    return sample;
}
double AgeMs(const View& view, const Clock::time_point now)
{
    return view.sample.sourceAgeMs + std::max(0.0, std::chrono::duration<double, std::milli>(now - view.received).count());
}
bool IsLive(const View& view, const Clock::time_point now, const int staleMs)
{
    return view.connected && view.hasSample && AgeMs(view, now) <= staleMs && view.sample.state == "playing";
}
std::string Status(const View& view, const Clock::time_point now, const int staleMs)
{
    if (!view.connected) return view.connection;
    if (!view.hasSample) return "WAITING FOR DATA";
    if (AgeMs(view, now) > staleMs) return "STALE DATA";
    if (view.sample.state == "playing") return "LIVE";
    if (view.sample.state == "unsupported") return "UNSUPPORTED BUILD";
    if (view.sample.state == "loading") return "LOADING";
    return "WAITING / DISCOVERING";
}

namespace
{
struct NoticeState
{
    std::string key;
    Notice notice;
    bool persistent = true, debounce = false;
};
NoticeState LightNotice(const LightSummary& light)
{
    const auto& reason = light.unavailableReason;
    if (light.status == "invalid")
        return {"lights-invalid", {"Light data is incompatible",
            "Install matching ASI and telemetry host versions, then restart Crimson Desert.", true}};
    if (reason == "unsupported-build")
        return {reason, {"Light capture needs an update",
            "Install a telemetry version compatible with this Crimson Desert build.", true}};
    if (reason == "legacy-plugin-conflict")
        return {reason, {"Two light plugins detected",
            "Disable the old CrimsonHueConsole package in your mod manager and restart.", true}};
    if (reason == "native-fault")
        return {reason, {"Light capture stopped",
            "Restart Crimson Desert; check the telemetry logs if this repeats.", true}};
    if (reason == "bridge-invalid")
        return {reason, {"Light bridge is incompatible",
            "Install matching ASI and telemetry host versions, then restart Crimson Desert.", true}};
    if (reason == "bridge-missing")
        return {reason, {"Light capture is not connected",
            "Check that CrimsonDesertTelemetry.asi is loaded and light capture is enabled.", true}, true, true};
    if (reason == "bridge-waiting" || reason == "bridge-stale" || reason == "bridge-changing" ||
        reason == "walk-unavailable" || reason == "player-unavailable" ||
        reason == "required-telemetry-unavailable" || reason == "game-stopped")
        return {"lights-waiting", {"Preparing light data",
            "Waiting for a fresh light sample from the current scene."}, true, true};
    return {"lights-unavailable", {"Light data is unavailable",
        "Check the telemetry logs for the light reader's failure reason.", true}, true, true};
}
NoticeState NextNotice(const View& view, const Config& config, Clock::time_point now)
{
    const bool live = IsLive(view, now, config.staleMs);
    if (!live && (view.healthStatus == "unsupported-build" || view.healthStatus == "error"))
        return {"health-" + view.healthStatus, {
            view.healthStatus == "unsupported-build" ? "This game build needs an update" : "Telemetry could not start",
            view.healthError.empty() ? "Check the telemetry logs and restart with matching plugin files."
                : view.healthError.substr(0, 512), true}};
    if (!view.connected)
    {
        if (view.connection == "INVALID / INCOMPATIBLE DATA")
            return {"connection-invalid", {"Telemetry data is incompatible",
                "Install matching ASI and telemetry host versions, then restart Crimson Desert.", true}};
        return {"connection-waiting", {"Starting telemetry",
            "Waiting for the local telemetry connection."}, true, true};
    }
    if (!view.hasSample)
        return {"sample-waiting", {"Waiting for Crimson Desert",
            "Telemetry will become available after a scene is loaded."}, true, true};
    if (view.sample.state != "playing")
        return {"scene-" + view.sample.state, {view.sample.state == "loading" ? "Loading Crimson Desert" : "Waiting for Crimson Desert",
            "Telemetry resumes when the scene is ready."}};
    if (AgeMs(view, now) > config.staleMs)
        return {"telemetry-stale", {"Waiting for fresh telemetry",
            "The last sample is out of date; current values are temporarily unavailable."}, true, true};
    if (!view.sample.cameraPosition || !view.sample.playerPosition)
        return {"pose-waiting", {"Preparing player and camera data",
            "Waiting for the current scene's telemetry."}, true, true};

    std::optional<NoticeState> waiting;
    for (const auto* light : {config.lightsExpected ? &view.sample.authoredLights : nullptr,
        config.lightsExpected && config.renderedExpected ? &view.sample.renderedLights : nullptr})
    {
        if (!light) continue;
        if (light->status == "available") continue;
        if (light->status == "not-reported")
            return {"lights-missing", {"Requested light data is missing",
                "Install matching ASI and telemetry host files; check [Lights] settings and restart.", true}};
        auto notice = LightNotice(*light);
        if (notice.notice.error) return notice;
        if (!waiting) waiting = std::move(notice);
    }
    if (waiting) return *waiting;
    if (config.lightsExpected && config.renderedExpected && view.sample.renderedLights.ageMilliseconds &&
        *view.sample.renderedLights.ageMilliseconds + AgeMs(view, now) > 500)
        return {"lights-waiting", {"Preparing light data",
            "Waiting for a fresh light sample from the current scene."}, true, true};
    const bool lights = config.lightsExpected && (view.sample.authoredLights.status == "available" ||
        (config.renderedExpected && view.sample.renderedLights.status == "available"));
    return {"ready", {"Telemetry is ready", lights ? "Player, camera and available light data are ready."
        : "Player and camera data are ready."}, false};
}
}

std::optional<Notice> NoticeTracker::Update(const View& view, const Config& config, Clock::time_point now)
{
    if (!config.notifications)
    {
        current_.reset(); currentKey_.clear(); pendingKey_.clear(); observedKey_.clear();
        return std::nullopt;
    }
    const auto visible = [&]() -> std::optional<Notice>
    {
        return current_ && (persistent_ || now - shownAt_ < std::chrono::milliseconds(
            std::clamp(config.notificationDurationMs, 1000, 60000))) ? current_ : std::nullopt;
    };
    auto next = NextNotice(view, config, now);
    if (next.key != observedKey_) { observedKey_ = next.key; observedAt_ = now; }
    if (now - observedAt_ >= std::chrono::seconds(10))
    {
        if (next.key == "connection-waiting")
            next = {"connection-timeout", {"Telemetry host is not reachable",
                "Check the .NET 8 ASP.NET Core Runtime (x64), [Server] Enabled=1, and telemetry host logs.", true}};
        else if (next.key == "lights-waiting")
            next = {"lights-timeout", {"Light capture has no fresh sample",
                "Restart Crimson Desert; check CrimsonDesertTelemetry.native.log if this continues.", true}};
    }
    if (next.key == currentKey_)
    {
        pendingKey_.clear();
        return visible();
    }
    if (next.debounce && !currentKey_.empty())
    {
        if (next.key != pendingKey_) { pendingKey_ = next.key; pendingAt_ = now; }
        if (now - pendingAt_ < std::chrono::seconds(1)) return visible();
    }
    pendingKey_.clear();
    currentKey_ = std::move(next.key);
    current_ = std::move(next.notice);
    persistent_ = next.persistent;
    shownAt_ = now;
    return visible();
}
}
