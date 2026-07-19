#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/renderer_core.hpp"
#include <export.hpp>

int main() {
    const auto temporary_directory = std::filesystem::temp_directory_path() / "spratforge_renderer_test";
    std::filesystem::create_directories(temporary_directory);
    const auto input_path = temporary_directory / "input.png";
    const auto output_path = temporary_directory / "frame_000.png";

    const spratgen::RenderedFrame input{
        .width = 2,
        .height = 2,
        .rgba = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255},
    };
    assert(spratgen::FrameExporter{}.writeFrame(input, input_path.string()));

    const spratforge::core::RenderOptions options{
        .input_path = input_path.string(), .grid_width = 1, .grid_height = 1};
    std::string error;
    assert(spratforge::core::render_single_frame(options, output_path.string(), error));
    assert(std::filesystem::file_size(output_path) > 8U);

    std::ifstream output(output_path, std::ios::binary);
    char signature[8] = {};
    output.read(signature, sizeof(signature));
    assert(std::string(signature, sizeof(signature)) == "\x89PNG\r\n\x1a\n");
    std::filesystem::remove_all(temporary_directory);
    return 0;
}