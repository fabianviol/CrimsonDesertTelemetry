cmake_minimum_required(VERSION 3.25)

project(CrimsonDesertTelemetryAsi VERSION 1.0.0 LANGUAGES C CXX ASM_MASM)

include(FetchContent)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
get_filename_component(CDT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include(cmake/GenerateNativeContract.cmake)
include_directories("${CMAKE_CURRENT_BINARY_DIR}/generated")
function(cdt_dependency name archive url hash)
    set(location "${url}")
    # Optional offline archive cache; hashes are checked identically to downloads.
    if(EXISTS "${CDT_ROOT}/build/dependency-downloads/${archive}")
        set(location "${CDT_ROOT}/build/dependency-downloads/${archive}")
    endif()
    FetchContent_Declare(${name} URL "${location}" URL_HASH "SHA256=${hash}" DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endfunction()
cdt_dependency(imgui imgui-1.91.9b.zip
    https://codeload.github.com/ocornut/imgui/zip/refs/tags/v1.91.9b
    FD37507C8476A6D14CC7C4B352401F31BCBD0F0D995D35390811E968C466F46E)
cdt_dependency(minhook minhook-1.3.4.zip
    https://codeload.github.com/TsudaKageyu/minhook/zip/refs/tags/v1.3.4
    172708123DAA0C98D20D3A980B16A50BE14AF243DC95DEE6F79C24193AD010E4)
cdt_dependency(json json-3.12.0.zip
    https://codeload.github.com/nlohmann/json/zip/refs/tags/v3.12.0
    34660B5E9A407195D55E8DA705ED26CC6D175CE5A6B1FB957E701FB4D5B04022)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(imgui minhook json)

add_library(cdt_sdf STATIC src/sdf_visibility.cpp)
target_compile_features(cdt_sdf PUBLIC cxx_std_20)
target_compile_options(cdt_sdf PRIVATE /W4 /WX /permissive-)
target_include_directories(cdt_sdf PUBLIC src)

add_library(cdt_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp)
target_include_directories(cdt_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(cdt_imgui PUBLIC d3d12 dxgi d3dcompiler)

add_library(cdt_overlay STATIC src/overlay_model.cpp src/overlay_client.cpp src/overlay_hud.cpp src/overlay_graphics.cpp src/overlay_hdr.cpp)
target_compile_features(cdt_overlay PUBLIC cxx_std_20)
target_compile_definitions(cdt_overlay PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(cdt_overlay PRIVATE /W4 /WX /permissive-)
target_include_directories(cdt_overlay PUBLIC src)
target_link_libraries(cdt_overlay PUBLIC cdt_imgui cdt_sdf minhook nlohmann_json::nlohmann_json winhttp)

file(READ "${imgui_SOURCE_DIR}/LICENSE.txt" imgui_license)
file(READ "${minhook_SOURCE_DIR}/LICENSE.txt" minhook_license)
file(READ "${json_SOURCE_DIR}/LICENSE.MIT" json_license)
string(REPLACE "Copyright (c) 2014-2025 Omar Cornut" "Copyright (c) 2004, 2005 Tristan Grimmer" font_license "${imgui_license}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/THIRD-PARTY-NOTICES.txt"
    "Dear ImGui 1.91.9b - https://github.com/ocornut/imgui\n${imgui_license}\n\n"
    "MinHook 1.3.4 - https://github.com/TsudaKageyu/minhook\n${minhook_license}\n\n"
    "JSON for Modern C++ 3.12.0 - https://github.com/nlohmann/json\n${json_license}\n\n"
    "ProggyClean font, embedded in Dear ImGui: Copyright (c) 2004, 2005 Tristan Grimmer. MIT License.\n"
    "${font_license}\n")
foreach(header imstb_rectpack.h imstb_textedit.h imstb_truetype.h)
    file(READ "${imgui_SOURCE_DIR}/${header}" stb_source)
    string(FIND "${stb_source}" "ALTERNATIVE A - MIT License" license_start REVERSE)
    if(license_start LESS 0)
        message(FATAL_ERROR "Missing embedded stb license in ${header}")
    endif()
    string(SUBSTRING "${stb_source}" ${license_start} -1 stb_license)
    file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/THIRD-PARTY-NOTICES.txt" "\n${header} (embedded in Dear ImGui)\n${stb_license}\n")
endforeach()

set(CDT_CONSOLE_SOURCES
    src/console/breakpoint.cpp src/console/console.cpp src/console/debugcmd.cpp
    src/console/discovery.cpp src/console/explorer.cpp src/console/manylights.cpp
    src/console/mem.cpp src/console/rtti.cpp src/console/util.cpp)
# The preserved research sources predate /WX; keep their warnings visible without
# relaxing warnings for newly integrated production code.
set_source_files_properties(${CDT_CONSOLE_SOURCES} PROPERTIES COMPILE_OPTIONS "/WX-;/wd4996")
add_library(cdt_instruments STATIC src/instruments.cpp src/build_guard.cpp
    src/render_bridge.cpp src/render_capture.cpp src/filter_thunk.asm src/ambient_thunk.asm
    src/spatial_probe.cpp src/spatial_readback.cpp src/spatial_sample.cpp
    src/spatial_thunk.asm ${CDT_CONSOLE_SOURCES})
target_compile_features(cdt_instruments PUBLIC cxx_std_20)
target_compile_definitions(cdt_instruments PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(cdt_instruments PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/permissive->")
target_include_directories(cdt_instruments PUBLIC src)
target_link_libraries(cdt_instruments PUBLIC cdt_sdf minhook d3d12 dxgi bcrypt psapi user32)
target_link_libraries(cdt_instruments PRIVATE nlohmann_json::nlohmann_json)

add_library(CrimsonDesertTelemetryAsi SHARED src/bootstrap.cpp)
target_compile_features(CrimsonDesertTelemetryAsi PRIVATE cxx_std_20)
target_compile_definitions(CrimsonDesertTelemetryAsi PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX)
target_compile_options(CrimsonDesertTelemetryAsi PRIVATE /W4 /WX /permissive-)
target_link_libraries(CrimsonDesertTelemetryAsi PRIVATE ws2_32 bcrypt cdt_overlay cdt_instruments)
set_target_properties(CrimsonDesertTelemetryAsi PROPERTIES
    PREFIX ""
    OUTPUT_NAME "CrimsonDesertTelemetry"
    SUFFIX ".asi")

add_executable(CrimsonDesertTelemetryBootstrapSmoke tests/bootstrap_smoke.cpp)
target_compile_features(CrimsonDesertTelemetryBootstrapSmoke PRIVATE cxx_std_20)
target_compile_definitions(CrimsonDesertTelemetryBootstrapSmoke PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX)
target_compile_options(CrimsonDesertTelemetryBootstrapSmoke PRIVATE /W4 /WX /permissive-)
target_link_libraries(CrimsonDesertTelemetryBootstrapSmoke PRIVATE ws2_32)

enable_testing()
add_executable(CrimsonDesertTelemetrySpatialProbeTests tests/spatial_probe_tests.cpp tests/spatial_barrier_abi.cpp
    src/spatial_thunk.asm src/spatial_readback.cpp src/spatial_sample.cpp
    src/console/mem.cpp src/console/util.cpp)
target_compile_features(CrimsonDesertTelemetrySpatialProbeTests PRIVATE cxx_std_20)
target_include_directories(CrimsonDesertTelemetrySpatialProbeTests PRIVATE src)
target_compile_definitions(CrimsonDesertTelemetrySpatialProbeTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(CrimsonDesertTelemetrySpatialProbeTests PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/permissive->")
target_link_libraries(CrimsonDesertTelemetrySpatialProbeTests PRIVATE cdt_sdf minhook nlohmann_json::nlohmann_json d3d12 dxgi psapi user32)
add_test(NAME spatial-passive-probe COMMAND CrimsonDesertTelemetrySpatialProbeTests)
set_tests_properties(spatial-passive-probe PROPERTIES TIMEOUT 20)
add_executable(CrimsonDesertTelemetrySpatialReadbackTests tests/spatial_readback_tests.cpp src/spatial_readback.cpp)
target_compile_features(CrimsonDesertTelemetrySpatialReadbackTests PRIVATE cxx_std_20)
target_include_directories(CrimsonDesertTelemetrySpatialReadbackTests PRIVATE src)
target_compile_definitions(CrimsonDesertTelemetrySpatialReadbackTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(CrimsonDesertTelemetrySpatialReadbackTests PRIVATE /W4 /WX /permissive-)
target_link_libraries(CrimsonDesertTelemetrySpatialReadbackTests PRIVATE d3d12 dxgi)
add_test(NAME spatial-texture-readback COMMAND CrimsonDesertTelemetrySpatialReadbackTests)
set_tests_properties(spatial-texture-readback PROPERTIES TIMEOUT 30)
add_executable(CrimsonDesertTelemetrySpatialPairTests tests/spatial_pair_tests.cpp src/spatial_readback.cpp
    src/spatial_sample.cpp
    src/spatial_thunk.asm src/console/mem.cpp src/console/util.cpp)
target_compile_features(CrimsonDesertTelemetrySpatialPairTests PRIVATE cxx_std_20)
target_include_directories(CrimsonDesertTelemetrySpatialPairTests PRIVATE src)
target_compile_definitions(CrimsonDesertTelemetrySpatialPairTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(CrimsonDesertTelemetrySpatialPairTests PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/permissive->")
target_link_libraries(CrimsonDesertTelemetrySpatialPairTests PRIVATE cdt_sdf minhook nlohmann_json::nlohmann_json d3d12 dxgi d3dcompiler psapi user32)
add_test(NAME spatial-paired-readback COMMAND CrimsonDesertTelemetrySpatialPairTests)
set_tests_properties(spatial-paired-readback PROPERTIES TIMEOUT 30)
add_executable(CrimsonDesertTelemetrySpatialSampleTests tests/spatial_sample_tests.cpp src/spatial_sample.cpp)
target_compile_features(CrimsonDesertTelemetrySpatialSampleTests PRIVATE cxx_std_20)
add_test(NAME spatial-native-sampler COMMAND CrimsonDesertTelemetrySpatialSampleTests)

add_executable(CrimsonDesertTelemetrySdfVisibilityTests tests/sdf_visibility_tests.cpp)
target_link_libraries(CrimsonDesertTelemetrySdfVisibilityTests PRIVATE cdt_sdf)
target_compile_options(CrimsonDesertTelemetrySdfVisibilityTests PRIVATE /W4 /WX /permissive-)
add_test(NAME sdf-variant-a-hud COMMAND CrimsonDesertTelemetrySdfVisibilityTests)

add_executable(CrimsonDesertTelemetrySpatialThunkTests tests/spatial_thunk_tests.cpp tests/spatial_thunk_fixture.asm src/spatial_thunk.asm)
target_compile_features(CrimsonDesertTelemetrySpatialThunkTests PRIVATE cxx_std_20)
target_compile_options(CrimsonDesertTelemetrySpatialThunkTests PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/permissive->")
add_test(NAME spatial-dispatch-abi COMMAND CrimsonDesertTelemetrySpatialThunkTests)
set_tests_properties(spatial-dispatch-abi PROPERTIES TIMEOUT 20)
add_executable(CrimsonDesertTelemetryExposureProbeTests tests/exposure_probe_tests.cpp)
target_compile_features(CrimsonDesertTelemetryExposureProbeTests PRIVATE cxx_std_20)
target_include_directories(CrimsonDesertTelemetryExposureProbeTests PRIVATE src)
target_compile_options(CrimsonDesertTelemetryExposureProbeTests PRIVATE /W4 /WX /permissive-)
add_test(NAME exposure-cache-probe COMMAND CrimsonDesertTelemetryExposureProbeTests)
add_executable(CrimsonDesertTelemetryBuildGuardTests tests/build_guard_tests.cpp src/build_guard.cpp)
target_compile_features(CrimsonDesertTelemetryBuildGuardTests PRIVATE cxx_std_20)
target_include_directories(CrimsonDesertTelemetryBuildGuardTests PRIVATE src)
target_compile_definitions(CrimsonDesertTelemetryBuildGuardTests PRIVATE CDT_BUILD_GUARD_TEST
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(CrimsonDesertTelemetryBuildGuardTests PRIVATE /W4 /WX /permissive-)
add_test(NAME native-build-guard COMMAND CrimsonDesertTelemetryBuildGuardTests)
add_executable(CrimsonDesertTelemetryRenderBridgeTests tests/render_bridge_tests.cpp src/render_bridge.cpp)
target_compile_features(CrimsonDesertTelemetryRenderBridgeTests PRIVATE cxx_std_20)
target_include_directories(CrimsonDesertTelemetryRenderBridgeTests PRIVATE src)
target_compile_definitions(CrimsonDesertTelemetryRenderBridgeTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(CrimsonDesertTelemetryRenderBridgeTests PRIVATE /W4 /WX /permissive-)
add_test(NAME render-bridge COMMAND CrimsonDesertTelemetryRenderBridgeTests)
add_executable(CrimsonDesertTelemetryRenderCaptureSmoke tests/render_capture_smoke.cpp
    src/render_capture.cpp src/render_bridge.cpp src/filter_thunk.asm src/ambient_thunk.asm src/console/mem.cpp src/console/util.cpp)
target_compile_features(CrimsonDesertTelemetryRenderCaptureSmoke PRIVATE cxx_std_20)
target_include_directories(CrimsonDesertTelemetryRenderCaptureSmoke PRIVATE src)
target_compile_definitions(CrimsonDesertTelemetryRenderCaptureSmoke PRIVATE CDT_RENDER_CAPTURE_TEST
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(CrimsonDesertTelemetryRenderCaptureSmoke PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/permissive->")
target_link_libraries(CrimsonDesertTelemetryRenderCaptureSmoke PRIVATE minhook d3d12 dxgi psapi user32)
add_test(NAME render-capture-d3d12 COMMAND CrimsonDesertTelemetryRenderCaptureSmoke)
set_tests_properties(render-capture-d3d12 PROPERTIES TIMEOUT 20)
add_test(NAME ambient-capture-d3d12 COMMAND CrimsonDesertTelemetryRenderCaptureSmoke --ambient)
set_tests_properties(ambient-capture-d3d12 PROPERTIES TIMEOUT 20)
add_test(NAME sky-shared-capture-d3d12 COMMAND CrimsonDesertTelemetryRenderCaptureSmoke --sky-shared)
add_test(NAME sky-first-capture-d3d12 COMMAND CrimsonDesertTelemetryRenderCaptureSmoke --sky-first)
set_tests_properties(sky-shared-capture-d3d12 sky-first-capture-d3d12 PROPERTIES TIMEOUT 25)
add_executable(CrimsonDesertTelemetryRenderContractTests tests/render_contract_tests.cpp
    src/render_capture.cpp src/render_bridge.cpp src/filter_thunk.asm src/ambient_thunk.asm src/console/mem.cpp src/console/util.cpp)
target_compile_features(CrimsonDesertTelemetryRenderContractTests PRIVATE cxx_std_20)
target_include_directories(CrimsonDesertTelemetryRenderContractTests PRIVATE src)
target_compile_definitions(CrimsonDesertTelemetryRenderContractTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(CrimsonDesertTelemetryRenderContractTests PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/permissive->")
target_link_libraries(CrimsonDesertTelemetryRenderContractTests PRIVATE minhook d3d12 dxgi psapi user32)
add_test(NAME render-contract-preflight COMMAND CrimsonDesertTelemetryRenderContractTests)
set_tests_properties(render-contract-preflight PROPERTIES TIMEOUT 15)
add_executable(CrimsonDesertTelemetryFilterThunkTests tests/filter_thunk_tests.cpp
    tests/thunk_fixture.asm src/filter_thunk.asm)
target_compile_features(CrimsonDesertTelemetryFilterThunkTests PRIVATE cxx_std_20)
target_compile_definitions(CrimsonDesertTelemetryFilterThunkTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(CrimsonDesertTelemetryFilterThunkTests PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/permissive->")
target_link_libraries(CrimsonDesertTelemetryFilterThunkTests PRIVATE minhook)
add_test(NAME render-filter-thunk COMMAND CrimsonDesertTelemetryFilterThunkTests)
set_tests_properties(render-filter-thunk PROPERTIES TIMEOUT 15)
add_executable(CrimsonDesertTelemetryAmbientThunkTests tests/ambient_thunk_tests.cpp
    tests/thunk_fixture.asm src/ambient_thunk.asm)
target_compile_features(CrimsonDesertTelemetryAmbientThunkTests PRIVATE cxx_std_20)
target_compile_definitions(CrimsonDesertTelemetryAmbientThunkTests PRIVATE CDT_AMBIENT_THUNK_TEST UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(CrimsonDesertTelemetryAmbientThunkTests PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/permissive->")
target_link_libraries(CrimsonDesertTelemetryAmbientThunkTests PRIVATE minhook)
add_test(NAME ambient-thunks COMMAND CrimsonDesertTelemetryAmbientThunkTests)
set_tests_properties(ambient-thunks PROPERTIES TIMEOUT 15)
add_executable(CrimsonDesertTelemetryOverlayTests tests/overlay_tests.cpp)
target_link_libraries(CrimsonDesertTelemetryOverlayTests PRIVATE cdt_overlay)
target_compile_options(CrimsonDesertTelemetryOverlayTests PRIVATE /W4 /WX /permissive-)
add_test(NAME overlay-model COMMAND CrimsonDesertTelemetryOverlayTests)

add_executable(CrimsonDesertTelemetryGraphicsSmoke tests/graphics_smoke.cpp)
target_link_libraries(CrimsonDesertTelemetryGraphicsSmoke PRIVATE cdt_overlay)
target_compile_options(CrimsonDesertTelemetryGraphicsSmoke PRIVATE /W4 /WX /permissive-)
add_test(NAME overlay-d3d12 COMMAND CrimsonDesertTelemetryGraphicsSmoke)
set_tests_properties(overlay-d3d12 PROPERTIES TIMEOUT 45)
add_test(NAME notifications-d3d12 COMMAND CrimsonDesertTelemetryGraphicsSmoke --notices)
set_tests_properties(notifications-d3d12 PROPERTIES TIMEOUT 45)
add_test(NAME light-overlay-d3d12 COMMAND CrimsonDesertTelemetryGraphicsSmoke --lights)
set_tests_properties(light-overlay-d3d12 PROPERTIES TIMEOUT 45)
add_test(NAME light-overlay-only-d3d12 COMMAND CrimsonDesertTelemetryGraphicsSmoke --lights-only)
set_tests_properties(light-overlay-only-d3d12 PROPERTIES TIMEOUT 45)

add_executable(CrimsonDesertTelemetryHdrTests tests/overlay_hdr_tests.cpp src/overlay_hdr.cpp)
target_compile_features(CrimsonDesertTelemetryHdrTests PRIVATE cxx_std_20)
target_include_directories(CrimsonDesertTelemetryHdrTests PRIVATE src)
target_compile_definitions(CrimsonDesertTelemetryHdrTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(CrimsonDesertTelemetryHdrTests PRIVATE /W4 /WX /permissive-)
target_link_libraries(CrimsonDesertTelemetryHdrTests PRIVATE d3d12 dxgi d3dcompiler)
add_test(NAME overlay-hdr-composite COMMAND CrimsonDesertTelemetryHdrTests)
set_tests_properties(overlay-hdr-composite PROPERTIES TIMEOUT 45)
add_test(NAME overlay-scrgb-d3d12 COMMAND CrimsonDesertTelemetryGraphicsSmoke --scrgb)
set_tests_properties(overlay-scrgb-d3d12 PROPERTIES TIMEOUT 45)
add_test(NAME notifications-scrgb-d3d12 COMMAND CrimsonDesertTelemetryGraphicsSmoke --scrgb-notices)
set_tests_properties(notifications-scrgb-d3d12 PROPERTIES TIMEOUT 45)

add_executable(CrimsonDesertTelemetryClientSmoke tests/client_smoke.cpp)
target_link_libraries(CrimsonDesertTelemetryClientSmoke PRIVATE cdt_overlay ws2_32 bcrypt crypt32)
target_compile_options(CrimsonDesertTelemetryClientSmoke PRIVATE /W4 /WX /permissive-)
add_test(NAME overlay-websocket COMMAND CrimsonDesertTelemetryClientSmoke)
set_tests_properties(overlay-websocket PROPERTIES TIMEOUT 30)
