cmake_minimum_required(VERSION 3.25)
project(heros_shade CXX)

set(DUSK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../dusklight" CACHE PATH "Path to dusk source root")
if (NOT EXISTS "${DUSK_DIR}/sdk/CMakeLists.txt")
    if (NOT DUSKLIGHT_VERSION)
        set(DUSKLIGHT_VERSION "b245c6bef8b4a370afb2453104a585dd41c97676")
    endif ()
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchDusklight.cmake")
    set(DUSK_DIR "${DUSKLIGHT_DIR}")
endif ()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_subdirectory("${DUSK_DIR}/sdk" dusk-sdk EXCLUDE_FROM_ALL)

add_mod(heros_shade
    FEATURES game fmt
    SOURCES  src/mod.cpp
             src/arc.cpp
             src/shade.cpp
             src/wolf.cpp
             src/voice.cpp
    MOD_JSON mod.json
)

if(MSVC)
    target_compile_options(heros_shade PRIVATE /Zc:__cplusplus)
endif()

option(PUBLIC_BUILD "Build for publishing" OFF)
if(PUBLIC_BUILD AND MSVC)
    target_link_options(heros_shade PRIVATE "/PDBALTPATH:%_PDB%")
endif()
