#include "spatial_sample.h"
#include <cmath>
#include <cstring>

namespace cdt::spatial
{
namespace
{
// The offline model rounds intermediate results to float32 exactly here; the
// trilinear accumulation stays in double. Both must be reproduced to match it.
double f32(double value) { return static_cast<double>(static_cast<float>(value)); }

double FromBits(uint64_t bits)
{
    double value{};
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// DXIL prints this z scale as an exact double; do not round it to a literal.
const double ZScale = FromBits(0x3F6F07C200000000ull);

double Lane(const uint8_t* constants, size_t offset, unsigned index)
{
    float value{};
    std::memcpy(&value, constants + offset + index * sizeof(float), sizeof(value));
    return value;
}

bool Finite(double value) { return std::isfinite(value); }

SpatialSample Trilinear(SpatialSample result, const uint8_t* volume, size_t rowPitch)
{
    constexpr long long dims[3]{VolumeWidth, VolumeHeight, VolumeDepth};
    double frac[3]{};
    long long low[3]{};
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        // Python's % is floored, so a negative coordinate wraps into [0,1).
        double wrapped = std::fmod(result.coordinates[axis], 1.0);
        if (wrapped < 0) wrapped += 1.0;
        const double texel = wrapped * static_cast<double>(dims[axis]) - 0.5;
        low[axis] = static_cast<long long>(std::floor(texel));
        frac[axis] = texel - static_cast<double>(low[axis]);
    }
    // Accumulation order matches the offline model so the sums round identically.
    double value = 0;
    for (unsigned bz = 0; bz < 2; ++bz)
        for (unsigned by = 0; by < 2; ++by)
            for (unsigned bx = 0; bx < 2; ++bx)
            {
                const unsigned bits[3]{bx, by, bz};
                long long xyz[3]{};
                double weight = 1.0;
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    long long index = (low[axis] + static_cast<long long>(bits[axis])) % dims[axis];
                    if (index < 0) index += dims[axis];
                    xyz[axis] = index;
                    weight *= bits[axis] ? frac[axis] : 1.0 - frac[axis];
                }
                const uint8_t raw = volume[(static_cast<size_t>(xyz[2]) * VolumeHeight +
                    static_cast<size_t>(xyz[1])) * rowPitch + static_cast<size_t>(xyz[0])];
                value += weight * raw / 255.0;
            }
    result.sampled = value;
    result.skyVisibility = value < 0 ? 1.0 : (value > 1 ? 0.0 : 1.0 - value);
    return result;
}

SpatialSample Coordinates(SpatialSample result, const uint8_t* constants,
    const uint8_t* volume, const double uv[3], size_t rowPitch)
{
    const double scale = 1.0 / (1 << result.clipmap);
    const double z = f32(uv[2] * scale);
    double fraction = f32(z - std::floor(z));
    if (fraction < 0) fraction = f32(1.0 + fraction);
    result.coordinates[0] = f32(uv[0] * scale);
    result.coordinates[1] = f32(uv[1] * scale);
    result.coordinates[2] = f32(f32(static_cast<double>(result.clipmap * 66 + 1) +
        f32(fraction * 64.0)) * ZScale);
    if (!volume || rowPitch < VolumeWidth || rowPitch > SIZE_MAX / (VolumeHeight * VolumeDepth))
    {
        result.status = SampleStatus::NoVolume;
        return result;
    }
    (void)constants;
    return Trilinear(result, volume, rowPitch);
}
}

SpatialSample DecodeReference(const uint8_t* constants)
{
    SpatialSample result{};
    if (!constants) return result;
    double inverse[3]{}, wrapped[3]{}, uv[3]{};
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        inverse[axis] = Lane(constants, 0x10, axis);
        wrapped[axis] = Lane(constants, 0x130, axis);
        uv[axis] = Lane(constants, 0x2E0, axis);
        if (!(inverse[axis] > 0.0 && inverse[axis] <= 1.0) || !Finite(wrapped[axis]) ||
            !Finite(uv[axis]) || std::fabs(uv[axis]) >= 1e8)
            return result;
    }
    for (unsigned axis = 0; axis < 3; ++axis) result.world[axis] = uv[axis] / inverse[axis];
    constexpr double radius[3]{63, 31, 63};
    // Clipmap 0 is deliberately not tested by this shader.
    for (unsigned level = 1; level < 8 && result.clipmap < 0; ++level)
    {
        const double scale = Lane(constants, 0x140 + level * 16, 3);
        if (!(scale > 0.0 && scale <= 1e4))
        {
            result.status = SampleStatus::InvalidClipmap;
            return result;
        }
        bool inside = true;
        for (unsigned axis = 0; axis < 3 && inside; ++axis)
        {
            const double origin = Lane(constants, 0x140 + level * 16, axis);
            const double relative = Lane(constants, 0x240 + level * 16, axis);
            if (!Finite(origin) || !Finite(relative)) return result;
            const long long low = static_cast<long long>(f32(origin - radius[axis]));
            const long long high = static_cast<long long>(f32(origin + radius[axis]));
            const double cell = std::floor(f32(f32(wrapped[axis] * scale) + relative));
            inside = cell >= static_cast<double>(low) && cell < static_cast<double>(high);
        }
        if (inside) result.clipmap = static_cast<int>(level);
    }
    if (result.clipmap < 0 || result.clipmap > 3)
    {
        // The shader's fallback yields 1 WITHOUT texture coverage. That is not a
        // measurement of open sky and must never be presented as one.
        result.status = SampleStatus::Fallback;
        result.skyVisibility = 1.0;
        return result;
    }
    result.status = SampleStatus::Ok;
    return result;
}

SpatialSample SampleAtReference(const uint8_t* constants, const uint8_t* volume, size_t rowPitch)
{
    SpatialSample result = DecodeReference(constants);
    if (result.status != SampleStatus::Ok) return result;
    const double uv[3]{Lane(constants, 0x2E0, 0), Lane(constants, 0x2E0, 1), Lane(constants, 0x2E0, 2)};
    return Coordinates(result, constants, volume, uv, rowPitch);
}

SpatialSample SampleAtWorld(const uint8_t* constants, const uint8_t* volume, const double world[3], size_t rowPitch)
{
    SpatialSample result = DecodeReference(constants);
    if (result.status != SampleStatus::Ok || !world) return result;
    double uv[3]{};
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        const double inverse = Lane(constants, 0x10, axis);
        uv[axis] = world[axis] * inverse;
        if (!Finite(uv[axis]))
        {
            result.status = SampleStatus::InvalidConstants;
            return result;
        }
    }
    return Coordinates(result, constants, volume, uv, rowPitch);
}
}
