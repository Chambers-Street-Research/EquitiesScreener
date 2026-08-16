cmake_minimum_required(VERSION 3.20)
project(EquitiesScreener)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
#  equities_core - all business logic, no entry point
# ============================================================

add_library(equities_core STATIC
        data/Equity.cpp
        data/Equity.h
        engine/Engine.cpp
        engine/Engine.h
        io/Csv.cpp
        io/Csv.h
        config/Config.cpp
        config/Config.h
        app/Screener.cpp
        app/Screener.h)

# PUBLIC: headers are included as "engine/Engine.h" etc., relative to the
# project root, so anything linking equities_core inherits this search path.
target_include_directories(equities_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Consumers need C++23 too (<print>, std::expected in the public headers).
target_compile_features(equities_core PUBLIC cxx_std_23)

# ============================================================
#  Executables
# ============================================================

add_executable(EquitiesScreener main.cpp)
target_link_libraries(EquitiesScreener PRIVATE equities_core)

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
enable_warnings(EquitiesScreener)

# ============================================================
#  Tests - GoogleTest via vcpkg (see vcpkg.json)
# ============================================================

include(CTest)
if(BUILD_TESTING)
    # NOTE: this fail-soft guard is only correct while vcpkg provides test
    # dependencies (gtest) alone. The moment vcpkg is needed for anything
    # else (e.g. Boost in equities_core), the toolchain becomes a HARD build
    # requirement: move the check to the top of the file as a FATAL_ERROR
    # and delete the else-branch below (tests would then build whenever the
    # app builds, and the CI test stage stays fail-closed).
    if(CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
        find_package(GTest CONFIG REQUIRED)

        add_executable(equities_tests tests/config_test.cpp)
        target_link_libraries(equities_tests PRIVATE equities_core GTest::gtest_main)
        enable_warnings(equities_tests)

        add_test(NAME equities_tests COMMAND equities_tests)
    else()
        # GoogleTest comes from vcpkg. A plain `cmake -B build` (no toolchain)
        # must still configure - we just skip the test suite.
        message(WARNING
            "BUILD_TESTING is ON but no vcpkg toolchain was set - skipping "
            "tests. To enable them, configure with "
            "-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake "
            "(see README 'Tests'), or pass -DBUILD_TESTING=OFF to silence "
            "this warning.")
    endif()
endif()
