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
}

int main()
{
    using namespace cdt::render;
    std::array<uint8_t, SceneBytes> scene{};
    Put(scene, 0xAC0, 6360000.f);
    Put(scene, 0x30, std::array<float, 4>{1920,1080,1.f/1920,1.f/1080});
    Put(scene, 0x80, std::array<float, 4>{0,0,0,0});
    Put(scene, 0x90, std::array<float, 4>{0,0,1,0});
    Put(scene, 0x20, uint32_t{100});
    std::vector<uint8_t> lights(LightBytes, 0x5a);
    std::array<uint8_t, CounterBytes> counters{};
    Put(counters, 4, uint32_t{1});
    Check(OpenBridge(), "Release bridge failed to open");
    const auto name=L"Local\\CrimsonDesertTelemetry.Render."+std::to_wstring(GetCurrentProcessId());
    const auto handle=OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str());
    Check(handle!=nullptr, "Release bridge could not be inspected");
    const auto* mapping=static_cast<const Mapping*>(MapViewOfFile(handle,FILE_MAP_READ,0,0,MappingBytes));
    Check(mapping!=nullptr, "Release mapping unavailable");

    // An old diagnostic setting or a caller cannot activate the excluded feature.
    SetSourceVisibilityEnabled(true);
    PublishSample(scene.data(),lights.data(),counters.data(),GetTickCount64(),1,2,3,0);
    Check(mapping->header.state==Status::Active && !(mapping->header.seqlock&1), "Raw release publication failed");
    Check(std::memcmp(mapping->scene,scene.data(),SceneBytes)==0 &&
        std::memcmp(mapping->lights,lights.data(),LightBytes)==0 &&
        std::memcmp(mapping->counters,counters.data(),CounterBytes)==0,
        "Excluded visibility changed raw camera/light/counter bytes");
    Check(mapping->visibility[0].code==11 && mapping->visibility[RecordCount-1].code==11 &&
        mapping->header.visibilityVolumeSequence==0, "Release activated source visibility");

    std::array<uint8_t,768> gi{};
    Put(gi,0x10,std::array<float,3>{1.f/32,1.f/16,1.f/32});
    cdt::sdf::Publish(std::vector<uint8_t>(size_t{128}*64*1040*2),gi,{0,0,0},1,GetTickCount64(),true);
    const auto status=cdt::sdf::CurrentStatus(GetTickCount64());
    Check(!status.available && status.reason=="not-built-in-this-package", "Real SDF implementation linked into release");
    const std::array<std::array<float,3>,1> targets{{{0,0,5}}};
    const auto batch=cdt::sdf::TraceBatch({0,0,0},targets,GetTickCount64());
    Check(!batch.status.available && batch.traces.empty(), "Release returned a geometric verdict");
    UnmapViewOfFile(mapping); CloseHandle(handle);
    std::cout << "PASS release keeps raw publication intact and cannot activate excluded SDF visibility\n";
}
