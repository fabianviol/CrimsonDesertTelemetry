cmake_minimum_required(VERSION 3.25)

project(CrimsonDesertTelemetryAsi VERSION 0.1.0 LANGUAGES CXX)

add_library(CrimsonDesertTelemetryAsi SHARED src/bootstrap.cpp)
target_compile_features(CrimsonDesertTelemetryAsi PRIVATE cxx_std_20)
target_compile_definitions(CrimsonDesertTelemetryAsi PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX)
target_compile_options(CrimsonDesertTelemetryAsi PRIVATE /W4 /WX /permissive-)
target_link_libraries(CrimsonDesertTelemetryAsi PRIVATE ws2_32 bcrypt)
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
