#include <iostream>
#include <string>

#include "atlas/atlas_core.hpp"
#include "cli/cli_parser.hpp"
#include "core/renderer_core.hpp"
#include "profiles/profile_loader.hpp"

int main(int argc, char* argv[]) {
    const auto result = spratforge::cli::parse_arguments(argc, argv);
    if (!result.options) {
        std::cerr << result.error << '\n';
        return 2;
    }

    const auto& options = *result.options;
    std::string error;
    bool rendered = false;
    switch (options.mode) {
        case spratforge::cli::Mode::single:
            rendered = spratforge::core::render_single_frame(options.output_path, error);
            break;
        case spratforge::cli::Mode::profile: {
            const auto profile = spratforge::profiles::load_profile(*options.profile_name);
            if (!profile) {
                std::cerr << "Unknown profile: " << *options.profile_name << '\n';
                return 3;
            }
            rendered = spratforge::core::render_profile(*profile, options.output_path, error);
            break;
        }
        case spratforge::cli::Mode::atlas: {
            const auto config = spratforge::atlas::parse_atlas_dimensions(*options.atlas_dimensions);
            if (!config) {
                std::cerr << "Invalid atlas dimensions: " << *options.atlas_dimensions << '\n';
                return 4;
            }
            rendered = spratforge::core::render_atlas(spratforge::atlas::build_atlas(*config), options.output_path, error);
            break;
        }
    }
    if (!rendered) {
        std::cerr << error << '\n';
        return 5;
    }
    return 0;
}