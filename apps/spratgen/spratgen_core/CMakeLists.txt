add_library(spratgen_core STATIC
    ../src/spratgen/config.cpp
    ../src/spratgen/generator.cpp
    ../src/spratgen/silhouette.cpp
    ../src/spratgen/skeleton.cpp
    ../src/spratgen/pose_model.cpp
    ../src/spratgen/pose_interp.cpp
    ../src/spratgen/palette.cpp
    ../src/spratgen/renderer.cpp
    ../src/spratgen/export.cpp
)

target_include_directories(spratgen_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/spratgen
)
target_include_directories(spratgen_core SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../stb
)
