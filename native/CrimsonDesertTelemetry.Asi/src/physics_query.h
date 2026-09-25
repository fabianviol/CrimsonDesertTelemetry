#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace cdt::physics
{
using Vec3 = std::array<float, 3>;
inline bool Finite(const Vec3& v)
{
    for (float x : v) if (!std::isfinite(x) || std::abs(x) > 1000000.f) return false;
    return true;
}
// Same player-near selection as WB. Truncate toward zero, NOT floor, for negative
// world coordinates. This only selects an observation; it is not a visibility test.
inline bool NearPlayer(const Vec3& queryStart, const Vec3& player)
{
    if (!Finite(queryStart) || !Finite(player) || (std::abs(player[0]) < 1 && std::abs(player[2]) < 1)) return false;
    const float x = player[0] - static_cast<float>(static_cast<int>(player[0] * .001f)) * 1000.f;
    const float z = player[2] - static_cast<float>(static_cast<int>(player[2] * .001f)) * 1000.f;
    const float dx = queryStart[0] - x, dz = queryStart[2] - z;
    return dx * dx + dz * dz <= 9.f && std::abs(queryStart[1] - player[1]) <= 3.f;
}
template<class T, std::size_t N> T Read(const std::array<std::uint8_t, N>& b, std::size_t offset)
{
    T value{};
    if (offset <= N && sizeof(T) <= N - offset) std::memcpy(&value, b.data() + offset, sizeof value);
    return value;
}
struct alignas(16) QueryCopy
{
    alignas(16) std::array<std::uint8_t, 0x200> query{};
    alignas(16) std::array<std::uint8_t, 0x100> transform{};
    alignas(16) std::array<std::uint8_t, 0x200> collector{};
    alignas(16) std::array<std::uint8_t, 0x200> shape{};
};
}
