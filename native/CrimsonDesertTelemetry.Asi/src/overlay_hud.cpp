#include "overlay.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace cdt::overlay
{
namespace
{
constexpr ImU32 White = IM_COL32(228, 239, 244, 255);
constexpr ImU32 Muted = IM_COL32(174, 194, 207, 255);
constexpr ImU32 Cyan = IM_COL32(49, 221, 208, 255);
constexpr ImU32 Amber = IM_COL32(251, 188, 91, 255);
std::string Angle(const std::optional<float>& angle)
{
    return angle ? std::format("{:.1f} deg", *angle) : "--";
}
std::string VectorText(const std::optional<Vec3>& vector, int precision = 2)
{
    return vector ? std::format("{:.{}f}   {:.{}f}   {:.{}f}", vector->x, precision,
        vector->y, precision, vector->z, precision) : "--   --   --";
}
}

void DrawHud(const View& view, const Config& config, const bool details)
{
    auto& io = ImGui::GetIO();
    const float naturalHeight = details ? 600.0f : 344.0f;
    const float scale = HudScale(io.DisplaySize.x, io.DisplaySize.y, config, details);
    if (scale < 0.3f) return;
    const float width = 510 * scale, height = naturalHeight * scale;
    const float margin = 20 * scale;
    const ImVec2 origin((config.corner & 1) ? io.DisplaySize.x - width - margin : margin,
        (config.corner & 2) ? io.DisplaySize.y - height - margin : margin);
    auto* draw = ImGui::GetForegroundDrawList();
    const auto point = [&](float x, float y) { return ImVec2(origin.x + x * scale, origin.y + y * scale); };
    const auto text = [&](float x, float y, ImU32 color, const std::string& value, float size = 16)
    {
        draw->AddText(ImGui::GetFont(), size * scale, point(x, y), color, value.c_str());
    };
    draw->AddRectFilled(origin, point(510, naturalHeight),
        IM_COL32(9, 20, 30, static_cast<int>(255 * config.opacity)), 12 * scale);
    draw->AddRect(origin, point(510, naturalHeight), IM_COL32(47, 74, 90, 235), 12 * scale);
    draw->AddRectFilled(point(0, 16), point(3, 55), Cyan);
    text(20, 15, Muted, "CRIMSON DESERT", 13);
    text(20, 34, White, "TELEMETRY", 22);
    const auto now = Clock::now();
    const bool live = IsLive(view, now, config.staleMs);
    text(272, 24, live ? Cyan : Amber, Status(view, now, config.staleMs), 14);
    draw->AddLine(point(20, 70), point(490, 70), IM_COL32(40, 61, 76, 255));

    const Sample empty{};
    const auto& sample = live ? view.sample : empty;
    const ImVec2 center = point(121, 169);
    draw->AddCircle(center, 65 * scale, IM_COL32(60, 86, 102, 255), 64, scale);
    draw->AddCircle(center, 43 * scale, IM_COL32(28, 47, 61, 255), 64, scale);
    draw->AddLine(point(121, 104), point(121, 234), IM_COL32(35, 56, 71, 255));
    draw->AddLine(point(56, 169), point(186, 169), IM_COL32(35, 56, 71, 255));
    text(110, 83, Muted, "+Z", 13);
    text(196, 160, Muted, "+X", 13);
    text(110, 240, Muted, "-Z", 13);
    text(27, 160, Muted, "-X", 13);
    const auto arrow = [&](std::optional<float> heading, ImU32 color, bool filled, float length)
    {
        if (!heading) return;
        const float radians = *heading * std::numbers::pi_v<float> / 180.0f;
        const float dx = std::sin(radians), dy = -std::cos(radians);
        const auto p = [&](float forward, float sideways)
        {
            return ImVec2(center.x + (dx * forward - dy * sideways) * scale,
                center.y + (dy * forward + dx * sideways) * scale);
        };
        const ImVec2 tip = p(length, 0), left = p(-10, -10), right = p(-10, 10);
        if (filled) draw->AddTriangleFilled(tip, left, right, color);
        else draw->AddTriangle(tip, left, right, color, 2 * scale);
    };
    arrow(sample.cameraHeading, Amber, false, 59);
    arrow(sample.playerHeading, Cyan, true, 46);
    draw->AddCircleFilled(center, 3 * scale, White);
    text(250, 87, Cyan, "PLAYER ROOT", 13);
    text(250, 106, White, Angle(sample.playerHeading), 24);
    text(250, 144, Amber, "CAMERA", 13);
    text(250, 163, White, Angle(sample.cameraHeading), 24);
    text(250, 204, Muted, "PITCH", 12);
    text(370, 204, Muted, "VERTICAL FOV", 12);
    text(250, 223, White, Angle(sample.pitch), 17);
    text(370, 223, White, Angle(sample.fov), 17);
    draw->AddLine(point(20, 265), point(490, 265), IM_COL32(40, 61, 76, 255));
    text(20, 279, Muted, "PLAYER POSITION  X / Y / Z  [game units]", 12);
    text(20, 298, White, VectorText(sample.playerPosition), 19);
    text(20, 325, Muted, "World axes, not compass north. Root orientation, not body pose.", 12);

    if (!details) return;
    draw->AddLine(point(20, 354), point(490, 354), IM_COL32(40, 61, 76, 255));
    text(20, 368, White, "DIAGNOSTICS", 14);
    text(20, 395, Muted, std::format("Schema 1.1  |  sequence {}  |  {:.1f} received Hz",
        view.sample.sequence, live ? view.rateHz : 0.0), 13);
    text(20, 416, Muted, view.hasSample ? std::format("Sample age {:.0f} ms  |  capture {} us (not latency)",
        AgeMs(view, now), view.sample.captureUs) : "Sample age --  |  capture --", 13);
    text(20, 437, Muted, std::format("Camera sources {}/{}  |  states {}  |  rebound {}",
        sample.consensus, sample.validCopies, sample.distinctStates, sample.rediscovered ? "yes" : "no"), 13);
    text(20, 458, Muted, "Player forward  " + VectorText(sample.playerForward, 3), 13);
    text(20, 479, Muted, "Camera forward  " + VectorText(sample.cameraForward, 3), 13);
    text(20, 500, Muted, "Player up       " + VectorText(sample.playerUp, 3), 13);
    text(20, 521, Muted, "Build " + view.sample.build, 13);
    text(20, 542, Muted, "D3D12 / SDR  |  passive HUD  |  no mouse capture", 13);
    text(20, 570, Cyan, "Player + camera telemetry. Light-source discovery is not included.", 12);
}
}
