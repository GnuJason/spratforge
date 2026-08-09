#include <iostream>
#include <filesystem>
#include <string>

#include <spratforge/ai/ai_motion_core.hpp>
#include <spratforge/atlas/atlas_core.hpp>
#include <spratforge/cli/cli_parser.hpp>
#include <spratforge/core/renderer_core.hpp>
#include <spratforge/profiles/profile_loader.hpp>
#include <spratforge/pipeline/pipeline_core.hpp>

int main(int argc, char* argv[]) {
    const auto result = spratforge::cli::parse_arguments(argc, argv);
    if (!result.options) {
        std::cerr << result.error << '\n';
        return 2;
    }

    const auto& options = *result.options;
    spratforge::core::RenderOptions render_options{.input_path = options.input_path.value_or("")};
    if (options.grid_dimensions) {
        const auto grid = spratforge::atlas::parse_atlas_dimensions(*options.grid_dimensions);
        if (!grid) {
            std::cerr << "Invalid grid dimensions: " << *options.grid_dimensions << '\n';
            return 4;
        }
        render_options.grid_width = grid->columns;
        render_options.grid_height = grid->rows;
    }
    render_options.palette_mode = options.dither ? std::optional<std::string>{"dither"} : options.palette_mode;

    std::string error;
    bool rendered = false;
    switch (options.mode) {
        case spratforge::cli::Mode::single:
            rendered = spratforge::core::render_single_frame(render_options, options.output_path, error);
            break;
        case spratforge::cli::Mode::profile: {
            try {
                const auto profile = spratforge::profiles::load_profile(*options.profile_name);
                rendered = spratforge::core::render_profile(profile, render_options, options.output_path, error);
            } catch (const std::exception& profile_error) {
                std::cerr << profile_error.what() << '\n';
                return 3;
            }
            break;
        }
        case spratforge::cli::Mode::atlas: {
            const auto config = spratforge::atlas::parse_atlas_dimensions(*options.atlas_dimensions);
            if (!config) {
                std::cerr << "Invalid atlas dimensions: " << *options.atlas_dimensions << '\n';
                return 4;
            }
            try {
                auto atlas_config = *config;
                atlas_config.padding = options.atlas_padding.value_or(0);
                const auto profile = spratforge::profiles::load_profile(*options.profile_name);
                const auto frames = spratforge::core::render_profile_frames(profile, render_options, error);
                if (frames.empty()) break;
                const auto atlas = spratforge::atlas::build_atlas(frames, atlas_config);
                spratforge::atlas::save_atlas_png(options.output_path, atlas);
                const std::filesystem::path output_path(options.output_path);
                spratforge::atlas::save_metadata_json(
                    ((output_path.has_parent_path() ? output_path.parent_path() : std::filesystem::path{"."}) / "atlas.json").string(),
                    atlas.metadata);
                rendered = true;
            } catch (const std::exception& atlas_error) {
                error = atlas_error.what();
            }
            break;
        }
        case spratforge::cli::Mode::ai_motion: {
            try {
                const auto motion = spratforge::ai::parse_motion(*options.motion_vector);
                rendered = spratforge::core::render_ai_motion(render_options, spratforge::ai::quantize(motion), 1U,
                                                              options.output_path, error);
            } catch (const std::invalid_argument& motion_error) {
                std::cerr << motion_error.what() << '\n';
                return 4;
            }
            break;
        }
        case spratforge::cli::Mode::turnkey:
            rendered = spratforge::pipeline::Pipeline{}.run(*options.input_path, options.output_path, error);
            break;
    }
    if (!rendered) {
        std::cerr << error << '\n';
        return 5;
    }
    return 0;
}