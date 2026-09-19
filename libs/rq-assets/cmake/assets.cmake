set(RQ_NEUTRAL_BOXER "${PROJECT_SOURCE_DIR}/apps/spratforge/tests/fixtures/neutral_boxer.png" CACHE FILEPATH "Neutral boxer source for RingQueen assets")
set(RQ_ASSET_PROFILE_DIR "${PROJECT_SOURCE_DIR}/apps/spratforge/profiles" CACHE PATH "RingQueen generation profile directory")
set(RQ_GENERATED_ASSET_DIR "${PROJECT_BINARY_DIR}/libs/rq-assets/generated/neutral_boxer")
set(_rq_export_profile "${RQ_ASSET_PROFILE_DIR}/export/ringqueen.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_rq_export_profile}")
file(READ "${_rq_export_profile}" _rq_export)
foreach(_key atlas_png atlas_json manifest_json)
    string(JSON _rq_${_key} GET "${_rq_export}" "${_key}")
    if(NOT _rq_${_key} MATCHES "^[A-Za-z0-9_-]+\\.(png|json)$")
        message(FATAL_ERROR "Unsafe RingQueen export filename: ${_rq_${_key}}")
    endif()
endforeach()
set(RQ_GENERATED_MANIFEST "${RQ_GENERATED_ASSET_DIR}/${_rq_manifest_json}")
set(_rq_generate_script "${PROJECT_SOURCE_DIR}/libs/rq-assets/cmake/generate_assets.cmake")
set(_rq_asset_arguments
    "-DCLI=$<TARGET_FILE:spratforge_cli>"
    "-DVALIDATOR=$<TARGET_FILE:rq_asset_validate>"
    "-DINPUT=${RQ_NEUTRAL_BOXER}"
    "-DPROFILES=${RQ_ASSET_PROFILE_DIR}"
    "-DDESTINATION=${RQ_GENERATED_ASSET_DIR}"
)
file(GLOB_RECURSE _rq_profiles CONFIGURE_DEPENDS "${RQ_ASSET_PROFILE_DIR}/*.json")
set(_rq_assets
    "${RQ_GENERATED_MANIFEST}"
    "${RQ_GENERATED_ASSET_DIR}/${_rq_atlas_json}"
    "${RQ_GENERATED_ASSET_DIR}/${_rq_atlas_png}"
    "${RQ_GENERATED_ASSET_DIR}/rig.json"
    "${RQ_GENERATED_ASSET_DIR}/anchor.json"
)
add_custom_command(OUTPUT ${_rq_assets}
    COMMAND "${CMAKE_COMMAND}" ${_rq_asset_arguments} -P "${_rq_generate_script}"
    DEPENDS spratforge_cli rq_asset_validate "${RQ_NEUTRAL_BOXER}" ${_rq_profiles}
        "${_rq_generate_script}" "${PROJECT_SOURCE_DIR}/libs/rq-assets/schema/sprite.schema.json"
    COMMENT "Generating RingQueen sprite assets"
    VERBATIM
)
add_custom_target(spratforge_generate_assets DEPENDS ${_rq_assets})
add_custom_target(rq_validate_assets
    COMMAND "${CMAKE_COMMAND}" ${_rq_asset_arguments} -DVALIDATE_ONLY=ON -P "${_rq_generate_script}"
    DEPENDS spratforge_generate_assets
    COMMENT "Validating RingQueen sprite assets"
    VERBATIM
)

if(BUILD_TESTING)
    add_test(NAME test_ringqueen_build
        COMMAND "${CMAKE_COMMAND}" "-DBUILD_DIR=${PROJECT_BINARY_DIR}" "-DASSET_DIR=${RQ_GENERATED_ASSET_DIR}"
            ${_rq_asset_arguments} "-DGENERATE_SCRIPT=${_rq_generate_script}"
            "-DATLAS_PNG=${_rq_atlas_png}" "-DMANIFEST_JSON=${_rq_manifest_json}"
            "-DCONFIG=$<CONFIG>" -P "${PROJECT_SOURCE_DIR}/libs/rq-assets/tests/test_asset_build.cmake")
    set_tests_properties(test_ringqueen_build PROPERTIES RUN_SERIAL TRUE)
endif()