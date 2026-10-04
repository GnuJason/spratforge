#pragma once

#include <optional>
#include <string>
#include <vector>

#include <spratforge/core/output_dir.hpp>
#include <spratforge/profiles/generation_profiles.hpp>

namespace spratforge::cli {

enum class Mode { single, profile, atlas, ai_motion, turnkey, generate, rig_validate, audit, forge };

// Options consumed by the `forge` subcommand (see spratforge/forge/forge_pipeline.hpp).
struct ForgeOptionsCli {
    std::string character = "boxer";
    std::string rig_override;
    int fps = 0;    // 0 keeps each clip's authored frame rate
    int scale = 1;
    int alpha_threshold = 16;
    int margin = 2;
    bool no_sheets = false;
    bool no_debug = false;

    // Phase 4A ergonomics.
    std::vector<std::string> only;      // empty: emit every animation
    bool list_animations = false;       // print the clip library and exit 0
    int supersample = 3;                // 1 == legacy nearest neighbour
    bool soft_edges = false;            // premultiplied soft alpha edges
    bool no_cleanup = false;            // disable pinhole fill + despeckle
    unsigned int seed = 0;              // reserved, recorded in metadata
};

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
    bool quiet = false;
    core::OutputPolicy output_policy = core::OutputPolicy::Refuse;
    ForgeOptionsCli forge;
    profiles::ProfilePaths profiles;
};

struct ParseResult {
    std::optional<Options> options;
    std::string error;
};

ParseResult parse_arguments(int argc, char* argv[]);

// Short overview of every subcommand.
std::string usage();

// Detailed help for one subcommand, with worked examples. An unknown or empty
// name returns the overview.
std::string usage_for(const std::string& subcommand);

// Semantic version of the CLI, printed by `--version`.
std::string version_string();

}  // namespace spratforge::cli