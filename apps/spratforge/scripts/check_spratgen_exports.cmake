function(spratforge_find_header_in_include_dirs header output_variable)
    set(_include_dirs ${ARGN})
    foreach(_include_dir IN LISTS _include_dirs)
        if(EXISTS "${_include_dir}/${header}")
            set(${output_variable} "${_include_dir}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${output_variable} "" PARENT_SCOPE)
endfunction()

function(spratforge_check_spratgen_exports spratgen_target)
    if(NOT DEFINED jsonnet_SOURCE_DIR OR "${jsonnet_SOURCE_DIR}" STREQUAL "")
        message(FATAL_ERROR
            "spratgen must export jsonnet_SOURCE_DIR so spratforge can locate its Jsonnet third-party JSON headers.")
    endif()

    if(NOT EXISTS "${jsonnet_SOURCE_DIR}")
        message(FATAL_ERROR
            "spratgen exported jsonnet_SOURCE_DIR='${jsonnet_SOURCE_DIR}', but that directory does not exist.")
    endif()

    if(NOT TARGET ${spratgen_target})
        message(FATAL_ERROR "Expected spratgen target '${spratgen_target}' is unavailable.")
    endif()

    get_target_property(_spratgen_include_dirs ${spratgen_target} INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT _spratgen_include_dirs)
        message(FATAL_ERROR
            "spratgen target '${spratgen_target}' must export public include directories containing export.hpp.")
    endif()

    spratforge_find_header_in_include_dirs("export.hpp" _spratgen_export_include_dir ${_spratgen_include_dirs})
    if(NOT _spratgen_export_include_dir)
        message(FATAL_ERROR
            "spratgen target '${spratgen_target}' does not export an include directory containing export.hpp. "
            "Export the header through INTERFACE_INCLUDE_DIRECTORIES.")
    endif()

    set(_spratgen_json_include_dir "${jsonnet_SOURCE_DIR}/third_party")
    if(NOT EXISTS "${_spratgen_json_include_dir}/json/json.hpp")
        message(FATAL_ERROR
            "spratgen Jsonnet headers are incomplete: expected json/json.hpp at "
            "'${_spratgen_json_include_dir}/json/json.hpp'.")
    endif()
endfunction()