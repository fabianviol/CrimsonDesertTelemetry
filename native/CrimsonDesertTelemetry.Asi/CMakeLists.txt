cmake_minimum_required(VERSION 3.25)

project(CrimsonDesertTelemetryAsi VERSION 1.0.0 LANGUAGES C CXX)

include(FetchContent)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
get_filename_component(CDT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
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

add_library(cdt_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp)
target_include_directories(cdt_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(cdt_imgui PUBLIC d3d12 dxgi d3dcompiler)

add_library(cdt_overlay STATIC src/overlay_model.cpp src/overlay_client.cpp src/overlay_hud.cpp src/overlay_graphics.cpp)
target_compile_features(cdt_overlay PUBLIC cxx_std_20)
target_compile_definitions(cdt_overlay PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(cdt_overlay PRIVATE /W4 /WX /permissive-)
target_include_directories(cdt_overlay PUBLIC src)
target_link_libraries(cdt_overlay PUBLIC cdt_imgui minhook nlohmann_json::nlohmann_json winhttp)

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

add_library(CrimsonDesertTelemetryAsi SHARED src/bootstrap.cpp)
target_compile_features(CrimsonDesertTelemetryAsi PRIVATE cxx_std_20)
target_compile_definitions(CrimsonDesertTelemetryAsi PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX)
target_compile_options(CrimsonDesertTelemetryAsi PRIVATE /W4 /WX /permissive-)
target_link_libraries(CrimsonDesertTelemetryAsi PRIVATE ws2_32 bcrypt cdt_overlay)
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
add_executable(CrimsonDesertTelemetryOverlayTests tests/overlay_tests.cpp)
target_link_libraries(CrimsonDesertTelemetryOverlayTests PRIVATE cdt_overlay)
target_compile_options(CrimsonDesertTelemetryOverlayTests PRIVATE /W4 /WX /permissive-)
add_test(NAME overlay-model COMMAND CrimsonDesertTelemetryOverlayTests)

add_executable(CrimsonDesertTelemetryGraphicsSmoke tests/graphics_smoke.cpp)
target_link_libraries(CrimsonDesertTelemetryGraphicsSmoke PRIVATE cdt_overlay)
target_compile_options(CrimsonDesertTelemetryGraphicsSmoke PRIVATE /W4 /WX /permissive-)
add_test(NAME overlay-d3d12 COMMAND CrimsonDesertTelemetryGraphicsSmoke)
set_tests_properties(overlay-d3d12 PROPERTIES TIMEOUT 45)

add_executable(CrimsonDesertTelemetryClientSmoke tests/client_smoke.cpp)
target_link_libraries(CrimsonDesertTelemetryClientSmoke PRIVATE cdt_overlay ws2_32 bcrypt crypt32)
target_compile_options(CrimsonDesertTelemetryClientSmoke PRIVATE /W4 /WX /permissive-)
add_test(NAME overlay-websocket COMMAND CrimsonDesertTelemetryClientSmoke)
set_tests_properties(overlay-websocket PROPERTIES TIMEOUT 30)
