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
        config/Config.h)

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

# PRIVATE on the library covers its own sources; the executable needs its
# own copy because PRIVATE options do not propagate across the link.
target_compile_options(equities_core PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>)

target_compile_options(EquitiesScreener PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>)
