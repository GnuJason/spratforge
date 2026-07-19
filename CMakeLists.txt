cmake_minimum_required(VERSION 3.20)

project(spratforge VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SPRATGEN_GIT_REPOSITORY "https://github.com/GnuJason/sprat-cli.git" CACHE STRING
    "Git repository URL for spratgen")
set(SPRATGEN_GIT_TAG "main" CACHE STRING "Git tag/branch/commit for spratgen")
set(SPRAT_DOWNLOAD_STB ON CACHE BOOL "Allow spratgen to download required stb headers")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build libraries as shared" FORCE)

FetchContent_Declare(spratgen
    GIT_REPOSITORY ${SPRATGEN_GIT_REPOSITORY}
    GIT_TAG ${SPRATGEN_GIT_TAG}
    GIT_SUBMODULES ""
)
FetchContent_MakeAvailable(spratgen)
FetchContent_GetProperties(spratgen)

if(NOT DEFINED jsonnet_SOURCE_DIR)
    message(FATAL_ERROR "spratgen did not expose its Jsonnet source directory required for profile parsing")
endif()

set(_spratgen_target "")
foreach(_candidate IN ITEMS spratgen_core spratgen_static spratcore spratgen)
    if(TARGET ${_candidate})
        set(_spratgen_target ${_candidate})
        break()
    endif()
endforeach()

if("${_spratgen_target}" STREQUAL "")
    message(FATAL_ERROR "spratgen was fetched but no known library target was exported. Expected one of: spratgen_core, spratgen_static, spratcore, spratgen")
endif()

if(NOT TARGET spratgen::spratgen)
    add_library(spratgen::spratgen ALIAS ${_spratgen_target})
endif()

add_library(spratforge_core
    src/ai/ai_motion_core.cpp
    src/atlas/atlas_core.cpp
    src/core/palette_core.cpp
    src/core/renderer_core.cpp
    src/profiles/profile_loader.cpp
)
target_include_directories(spratforge_core PUBLIC include)
target_include_directories(spratforge_core SYSTEM PRIVATE "${spratgen_SOURCE_DIR}/stb")
target_include_directories(spratforge_core SYSTEM PUBLIC "${jsonnet_SOURCE_DIR}/third_party")
target_compile_definitions(spratforge_core PRIVATE
    SPRATFORGE_PALETTE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/palettes"
    SPRATFORGE_PROFILE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/profiles"
)
target_link_libraries(spratforge_core PUBLIC spratgen::spratgen)

add_executable(spratforge_cli src/main.cpp src/cli/cli_parser.cpp)
target_link_libraries(spratforge_cli PRIVATE spratforge_core spratgen::spratgen)

include(CTest)
if(BUILD_TESTING)
    function(add_spratforge_test test_name)
        add_executable(${test_name} tests/${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE spratforge_core)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endfunction()

    add_spratforge_test(test_golden_frame)
    add_spratforge_test(test_profiles)
    add_spratforge_test(test_palette)
    add_spratforge_test(test_atlas)
    add_spratforge_test(test_motion)
endif()