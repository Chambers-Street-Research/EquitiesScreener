cmake_minimum_required(VERSION 3.20)
project(EquitiesScreener)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CURL REQUIRED)

include(FetchContent)

FetchContent_Declare(
        cpr
        GIT_REPOSITORY https://github.com/libcpr/cpr.git
        GIT_TAG 1.12.0
)
set(CPR_USE_SYSTEM_CURL ON CACHE BOOL "" FORCE)
set(CPR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CPR_BUILD_TESTS_SSL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(cpr)

# ============================================================
#  equities_core - all business logic, no entry point
# ============================================================

add_library(equities_core STATIC
        data/Equity.cpp
        data/Equity.h
        engine/Engine.cpp
        engine/Engine.h
        io/MarketData.cpp
        io/Csv.cpp
        io/Csv.h
        io/Json.cpp
        io/Json.h
        io/FmpClient.cpp
        io/FmpClient.h
        config/Config.cpp
        config/Config.h
        app/Screener.cpp
        app/Screener.h)

target_link_libraries(equities_core PUBLIC CURL::libcurl cpr::cpr)

# PUBLIC: headers are included as "engine/Engine.h" etc., relative to the
# project root, so anything linking equities_core inherits this search path.
target_include_directories(equities_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Consumers need C++23 too (<print>, std::expected in the public headers).
target_compile_features(equities_core PUBLIC cxx_std_23)

# ============================================================
#  equities_cli - command-line front-end, no logic of its own
# ============================================================

add_executable(equities_cli main.cpp)
target_link_libraries(equities_cli PRIVATE equities_core)

# The target is named for what it is (one of possibly several front-ends over
# equities_core); the binary keeps the name users already invoke - the README,
# the Docker image and its entrypoint all call it EquitiesScreener.
set_target_properties(equities_cli PROPERTIES OUTPUT_NAME EquitiesScreener)

# ============================================================
#  Compiler warnings: -Wall -Wextra (GCC/Clang), /W4 (MSVC)
# ============================================================

# PRIVATE on the library covers its own sources; executables need their
# own copy because PRIVATE options do not propagate across the link.
function(enable_warnings target)
    target_compile_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
            $<$<CXX_COMPILER_ID:MSVC>:/W4>)
endfunction()

enable_warnings(equities_core)
enable_warnings(equities_cli)

# ============================================================
#  Tests - GoogleTest via vcpkg (see vcpkg.json)
# ============================================================

include(CTest)
if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_executable(equities_tests tests/config_test.cpp tests/json_test.cpp tests/market_data_test.cpp)
    target_link_libraries(equities_tests PRIVATE equities_core GTest::gtest_main)
    enable_warnings(equities_tests)
    add_test(NAME equities_tests COMMAND equities_tests)
    add_executable(history_probe tests/history_probe.cpp)
    target_link_libraries(history_probe PRIVATE equities_core)
    enable_warnings(history_probe)
    add_test(NAME live_integration COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/live_integration.py $<TARGET_FILE:equities_cli> $<TARGET_FILE:history_probe>)
    set_tests_properties(live_integration PROPERTIES TIMEOUT 60)
endif()
