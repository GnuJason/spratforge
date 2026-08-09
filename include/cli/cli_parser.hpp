#pragma once

#include <optional>
#include <string>

namespace spratforge::cli {

enum class Mode { single, profile, atlas, ai_motion, turnkey };

struct Options {
    Mode mode;
    std::string output_path;
    std::optional<std::string> input_path;
    std::optional<std::string> grid_dimensions;
    std::optional<std::string> profile_name;
    std::optional<std::string> atlas_dimensions;
    std::optional<int> atlas_padding;
    std::optional<std::string> palette_mode;
    std::optional<std::string> motion_vector;
    bool dither = false;
    bool verbose = false;
};

struct ParseResult {
    std::optional<Options> options;
    std::string error;
};

ParseResult parse_arguments(int argc, char* argv[]);
std::string usage();

}  // namespace spratforge::cli