cmake_minimum_required(VERSION 3.20)

project(ringqueen LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include(CTest)

# spratforge consumes spratgen targets, so spratgen must be configured first.
add_subdirectory(apps/spratgen)
add_subdirectory(apps/spratforge)
add_subdirectory(libs/rq-core)
add_subdirectory(libs/rq-assets)
add_subdirectory(libs/rq-ai)
include(libs/rq-assets/cmake/assets.cmake)
add_subdirectory(apps/ringqueen-engine)