#pragma once

#include <optional>
#include <string>

namespace spratforge::cli {

enum class Mode { single, profile, atlas };

struct Options {
    Mode mode;
    std::string output_path;
    std::optional<std::string> profile_name;
    std::optional<std::string> atlas_dimensions;
};

struct ParseResult {
    std::optional<Options> options;
    std::string error;
};

ParseResult parse_arguments(int argc, char* argv[]);
std::string usage();

}  // namespace spratforge::cli