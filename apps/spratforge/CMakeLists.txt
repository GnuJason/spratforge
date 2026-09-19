# spratforge itself does not provide export.hpp or json/json.hpp. export.hpp must be
# exported by spratgen's public include directories. JSON is supplied either by an
# installed jsoncpp package or by spratgen's exported Jsonnet third-party directory.
# spratgen is configured by the monorepo root from apps/spratgen before this file.
if(NOT DEFINED spratgen_SOURCE_DIR)
    set(spratgen_SOURCE_DIR "${PROJECT_SOURCE_DIR}/apps/spratgen")
endif()

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
    message(FATAL_ERROR "apps/spratgen did not export a known library target. Expected one of: spratgen_core, spratgen_static, spratcore, spratgen")
endif()

if(NOT TARGET spratgen::spratgen)
    add_library(spratgen::spratgen ALIAS ${_spratgen_target})
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_spratgen_exports.cmake")
spratforge_check_spratgen_exports(spratgen::spratgen)

find_package(jsoncpp CONFIG QUIET)
set(_spratforge_json_target "")
foreach(_candidate IN ITEMS JsonCpp::JsonCpp jsoncpp_lib jsoncpp_static)
    if(TARGET ${_candidate})
        set(_spratforge_json_target ${_candidate})
        break()
    endif()
endforeach()

if(NOT "${_spratforge_json_target}" STREQUAL "")
    get_target_property(_spratforge_json_include_dirs ${_spratforge_json_target} INTERFACE_INCLUDE_DIRECTORIES)
    spratforge_find_header_in_include_dirs("json/json.hpp" _spratforge_json_header_dir ${_spratforge_json_include_dirs})
    if(NOT _spratforge_json_header_dir)
        message(FATAL_ERROR
            "jsoncpp target '${_spratforge_json_target}' was found but does not export an include directory containing json/json.hpp.")
    endif()
else()
    get_target_property(_spratforge_spratgen_include_dirs spratgen::spratgen INTERFACE_INCLUDE_DIRECTORIES)
    spratforge_find_header_in_include_dirs("json/json.hpp" _spratforge_json_header_dir ${_spratforge_spratgen_include_dirs})
    set(_spratforge_json_fallback_dir "${jsonnet_SOURCE_DIR}/third_party")
    if(_spratforge_json_header_dir)
        set(_spratforge_json_include_dirs "${_spratforge_json_header_dir}")
    elseif(EXISTS "${_spratforge_json_fallback_dir}/json/json.hpp")
        set(_spratforge_json_include_dirs "${_spratforge_json_fallback_dir}")
    else()
        message(FATAL_ERROR
            "Unable to locate json/json.hpp. Install a jsoncpp CMake package or ensure spratgen exports "
            "an include path containing json/json.hpp or jsonnet_SOURCE_DIR with "
            "'${_spratforge_json_fallback_dir}/json/json.hpp'.")
    endif()
endif()

add_library(spratforge_core
    src/ai/ai_motion_core.cpp
    src/audit/audit_core.cpp
    src/anchor/anchor_core.cpp
    src/atlas/atlas_core.cpp
    src/core/palette_core.cpp
    src/core/pixel_normalizer.cpp
    src/core/renderer_core.cpp
    src/manifest/manifest_core.cpp
    src/motion/pose_renderer.cpp
    src/pipeline/pipeline_core.cpp
    src/profiles/profile_loader.cpp
    src/profiles/generation_profiles.cpp
    src/rig/pixel_rig.cpp
    src/rig/segmentation.cpp
    src/rig/rig_validator.cpp
    src/templates/template_engine.cpp
)
target_include_directories(spratforge_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(spratforge_core SYSTEM PRIVATE "${spratgen_SOURCE_DIR}/stb")
target_include_directories(spratforge_core SYSTEM PUBLIC ${_spratforge_json_include_dirs})
target_compile_definitions(spratforge_core PRIVATE
    SPRATFORGE_PALETTE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/palettes"
    SPRATFORGE_PROFILE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/profiles"
    SPRATFORGE_TEMPLATE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/profiles/templates"
)
target_link_libraries(spratforge_core PUBLIC spratgen::spratgen)
if(NOT "${_spratforge_json_target}" STREQUAL "")
    target_link_libraries(spratforge_core PUBLIC ${_spratforge_json_target})
endif()

add_executable(spratforge_cli src/main.cpp src/cli/cli_parser.cpp)
target_link_libraries(spratforge_cli PRIVATE spratforge_core spratgen::spratgen)

if(BUILD_TESTING)
    function(add_spratforge_test test_name)
        add_executable(${test_name} tests/${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE spratforge_core)
        add_test(NAME ${test_name} COMMAND ${test_name})
        set_tests_properties(${test_name} PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endfunction()

    add_spratforge_test(test_golden_frame)
    add_spratforge_test(test_profiles)
    add_spratforge_test(test_palette)
    add_spratforge_test(test_atlas)
    add_spratforge_test(test_motion)
    add_spratforge_test(test_anchor)
    add_spratforge_test(test_templates)
    add_spratforge_test(test_pixel_normalizer)
    add_spratforge_test(test_manifest)
    add_spratforge_test(test_pipeline)
    add_spratforge_test(test_rig)
    add_spratforge_test(test_pose_renderer)
    add_spratforge_test(test_cli_generate)
    add_spratforge_test(test_cli_rig_validate)
    add_spratforge_test(test_cli_audit)
    add_spratforge_test(test_ringqueen_export)
    target_link_libraries(test_ringqueen_export PRIVATE rq::assets)
    target_compile_definitions(test_ringqueen_export PRIVATE RQ_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    foreach(_cli_test IN ITEMS test_cli_generate test_cli_rig_validate test_cli_audit test_ringqueen_export)
        target_compile_definitions(${_cli_test} PRIVATE SPRATFORGE_CLI_PATH="$<TARGET_FILE:spratforge_cli>")
        add_dependencies(${_cli_test} spratforge_cli)
    endforeach()
endif()