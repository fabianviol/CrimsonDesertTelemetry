#include "render_bridge.h"
#include "sdf_visibility.h"
#include <array>
#include <cstring>
#include <iostream>
#include <vector>

namespace
{
void Check(bool ok, const char* message)
{
    if (!ok) { std::cerr << message << '\n'; ExitProcess(1); }
}
template<size_t N, class T> void Put(std::array<uint8_t, N>& bytes, size_t offset, T value)
{
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
std::array<uint8_t, 768> GiConstants()
{
    std::array<uint8_t, 768> gi{};
    Put(gi, 0x10, std::array<float, 3>{1.f / 32, 1.f / 16, 1.f / 32});
    for (unsigned level = 0; level < 8; ++level)
        Put(gi, 0x14c + level * 16, 4.f / static_cast<float>(1u << level));
    return gi;
}
std::vector<uint8_t> Field(uint16_t half)
{
    std::vector<uint8_t> bytes(size_t{128} * 64 * 1040 * 2);
    for (size_t i = 0; i < bytes.size(); i += 2)
    { bytes[i] = static_cast<uint8_t>(half); bytes[i + 1] = static_cast<uint8_t>(half >> 8); }
    return bytes;
}
void Light(std::vector<uint8_t>& bytes, unsigned index, const std::array<float, 3>& relative)
{
    auto* record = bytes.data() + size_t{index} * cdt::render::RecordStride;
    const float marker = 3.14159265f;
    std::memcpy(record, relative.data(), sizeof(relative));
    std::memcpy(record + 12, &marker, sizeof(marker));
}
}

int main()
{
    using namespace cdt::render;
    std::array<uint8_t, SceneBytes> scene{};
    Put(scene, 0xAC0, 6360000.f);
    Put(scene, 0x30, std::array<float, 4>{1920, 1080, 1.f / 1920, 1.f / 1080});
    Put(scene, 0x80, std::array<float, 4>{0, 0, 0, 0});
    Put(scene, 0x90, std::array<float, 4>{0, 0, 1, 0});
    Put(scene, 0x20, uint32_t{100});
    std::vector<uint8_t> lights(LightBytes);
    Light(lights, 0, {0, 0, 5});
    Light(lights, 1, {0, 0, -5});
    std::array<uint8_t, CounterBytes> counters{};
    Put(counters, 4, uint32_t{2});

    Check(OpenBridge(), "Production bridge failed to open");
    const auto name = L"Local\\CrimsonDesertTelemetry.Render." + std::to_wstring(GetCurrentProcessId());
    const auto handle = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
    Check(handle != nullptr, "Production bridge could not be inspected");
    const auto* mapping = static_cast<const Mapping*>(MapViewOfFile(handle, FILE_MAP_READ, 0, 0, MappingBytes));
    Check(mapping != nullptr, "Production mapping unavailable");
    const auto publish = [&]
    {
        PublishSample(scene.data(), lights.data(), counters.data(), GetTickCount64(), 1, 2, 3, 0);
        Check(std::memcmp(mapping->scene, scene.data(), SceneBytes) == 0 &&
            std::memcmp(mapping->lights, lights.data(), LightBytes) == 0 &&
            std::memcmp(mapping->counters, counters.data(), CounterBytes) == 0,
            "Visibility changed raw camera, light or counter bytes");
    };

    SetSourceVisibilityEnabled(true);
    publish();
    Check(mapping->visibility[0].code == 0 && mapping->visibility[1].code == 0,
        "Enabled production visibility did not wait for its first field");

    const auto gi = GiConstants();
    cdt::sdf::Publish(Field(0x3c00), gi, {0, 0, 0}, 101, GetTickCount64(), true);
    publish();
    Check(mapping->visibility[0].code == 1 && mapping->visibility[1].code == 1 &&
        mapping->visibility[0].closest == 1 && mapping->visibility[1].closest == 1,
        "Production fire path did not classify clear sources in front and behind the camera");

    cdt::sdf::Publish(Field(0xbc00), gi, {0, 0, 0}, 102, GetTickCount64(), true);
    publish();
    Check(mapping->visibility[0].code == 2 && mapping->visibility[1].code == 2 &&
        mapping->visibility[0].closest == -1 && mapping->visibility[1].closest == -1,
        "Production fire path did not classify blocked sources in front and behind the camera");

    SetSourceVisibilityEnabled(false);
    publish();
    Check(mapping->visibility[0].code == 11 && mapping->visibility[RecordCount - 1].code == 11,
        "Disabled production visibility retained a verdict");
    UnmapViewOfFile(mapping);
    CloseHandle(handle);
    std::cout << "PASS narrow production rendered-source visibility preserves raw records\n";
}
