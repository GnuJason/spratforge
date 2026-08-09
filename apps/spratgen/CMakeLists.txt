add_library(spratgen_core STATIC
	${CMAKE_CURRENT_SOURCE_DIR}/src/config.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/generator.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/silhouette.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/skeleton.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/pose_model.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/pose_interp.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/palette.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/renderer.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/export.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/stb_impl.cpp
)

target_include_directories(spratgen_core PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/third_party/jsonnet/third_party
	${CMAKE_CURRENT_SOURCE_DIR}/stb
)

add_library(spratgen::spratgen ALIAS spratgen_core)

set(jsonnet_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/jsonnet" CACHE PATH
	"Path to spratgen's bundled Jsonnet source directory")