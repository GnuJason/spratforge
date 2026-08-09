#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#include <spratforge/atlas/atlas_core.hpp>

int main() {
    using namespace spratforge;

    const auto config = atlas::parse_atlas_dimensions("2x2");
    assert(config);
    assert(atlas::is_valid(*config));
    assert(!atlas::is_valid({.columns = 0, .rows = 1, .padding = 0}));
    assert(!atlas::parse_atlas_dimensions("2 by 2"));

    const core::Frame red{.width = 2, .height = 2,
                          .rgba = {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255}};
    const core::Frame green{.width = 2, .height = 2,
                            .rgba = {0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255}};
    const atlas::AtlasConfig padded{.columns = 2, .rows = 2, .padding = 1};
    const atlas::AtlasResult result = atlas::build_atlas({red, green}, padded);
    assert(result.width == 5);
    assert(result.height == 5);
    assert(result.rgba.size() == 100U);
    assert(result.rgba[0] == 255U && result.rgba[1] == 0U);
    assert(result.rgba[8] == 0U);
    assert(result.rgba[12] == 0U && result.rgba[13] == 255U);
    assert(result.metadata["columns"] == 2);
    assert(result.metadata["rows"] == 2);
    assert(result.metadata["padding"] == 1);
    assert(result.metadata["frames"].size() == 2U);
    assert(result.metadata["frames"][1]["index"] == 1);
    assert(result.metadata["frames"][1]["x"] == 3);
    assert(result.metadata["frames"][1]["y"] == 0);
    assert(result.metadata["frames"][1]["w"] == 2);
    assert(result.metadata["frames"][1]["h"] == 2);

    bool overflow_threw = false;
    try {
        (void)atlas::build_atlas({red, green}, {.columns = 1, .rows = 1, .padding = 0});
    } catch (const std::runtime_error& error) {
        overflow_threw = std::string(error.what()) == "Atlas grid too small for frame count";
    }
    assert(overflow_threw);

    const std::filesystem::path output_directory = std::filesystem::temp_directory_path() / "spratforge-atlas-test";
    const std::filesystem::path png_path = output_directory / "atlas.png";
    const std::filesystem::path metadata_path = output_directory / "atlas.json";
    atlas::save_atlas_png(png_path.string(), result);
    atlas::save_metadata_json(metadata_path.string(), result.metadata);
    std::ifstream png(png_path, std::ios::binary);
    const std::vector<unsigned char> signature(8U);
    std::vector<unsigned char> actual_signature(8U);
    png.read(reinterpret_cast<char*>(actual_signature.data()), static_cast<std::streamsize>(actual_signature.size()));
    assert(actual_signature == std::vector<unsigned char>({137, 80, 78, 71, 13, 10, 26, 10}));
    std::ifstream metadata_input(metadata_path);
    nlohmann::json metadata;
    metadata_input >> metadata;
    assert(metadata == result.metadata);
    std::filesystem::remove_all(output_directory);
    return 0;
}