include(FetchContent)
if(POLICY CMP0135)
	cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(valijson
	URL https://codeload.github.com/tristanpenman/valijson/tar.gz/refs/tags/v1.0.2
	URL_HASH SHA256=35d86e54fc727f1265226434dc996e33000a570f833537a25c8b702b0b824431
	SOURCE_SUBDIR rq-headers-only
)
FetchContent_MakeAvailable(valijson)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/schema/sprite.schema.json" RQ_SPRITE_SCHEMA)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/schema/sprite.schema.json")
configure_file(src/sprite_schema.hpp.in generated/sprite_schema.hpp @ONLY)

add_library(rq_assets src/sprite_asset.cpp)
add_library(rq::assets ALIAS rq_assets)
target_include_directories(rq_assets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(rq_assets PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_include_directories(rq_assets SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/apps/spratgen/stb")
target_include_directories(rq_assets SYSTEM PRIVATE "${valijson_SOURCE_DIR}/include" "${jsonnet_SOURCE_DIR}/third_party" "${jsonnet_SOURCE_DIR}/third_party/json")

add_executable(rq_asset_validate src/validate_main.cpp)
target_link_libraries(rq_asset_validate PRIVATE rq::assets)

if(BUILD_TESTING)
	add_executable(test_sprite_schema tests/test_sprite_schema.cpp)
	target_link_libraries(test_sprite_schema PRIVATE rq::assets)
	target_include_directories(test_sprite_schema SYSTEM PRIVATE "${jsonnet_SOURCE_DIR}/third_party")
	add_test(NAME test_sprite_schema COMMAND test_sprite_schema)
endif()