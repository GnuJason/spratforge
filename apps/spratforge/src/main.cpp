#include <iostream>
#include <filesystem>
#include <string>

#include <spratforge/ai/ai_motion_core.hpp>
#include <spratforge/audit/audit_core.hpp>
#include <spratforge/atlas/atlas_core.hpp>
#include <spratforge/cli/cli_parser.hpp>
#include <spratforge/core/renderer_core.hpp>
#include <spratforge/forge/forge_pipeline.hpp>
#include <spratforge/profiles/profile_loader.hpp>
#include <spratforge/pipeline/pipeline_core.hpp>

namespace {

// `forge` prints a machine-readable run summary so CI and the sprat wrapper can
// assert on it without re-reading metadata.json.
int run_forge_command(const spratforge::cli::Options& options) {
    spratforge::forge::ForgeOptions forge_options;
    forge_options.input_path = options.input_path.value_or("");
    forge_options.output_dir = options.output_path;
    forge_options.character = options.forge.character;
    forge_options.rig_override_path = options.forge.rig_override;
    forge_options.fps_override = options.forge.fps;
    forge_options.scale = options.forge.scale < 1 ? 1 : options.forge.scale;
    forge_options.alpha_threshold = options.forge.alpha_threshold;
    forge_options.margin = options.forge.margin;
    forge_options.emit_sheets = !options.forge.no_sheets;
    forge_options.emit_debug = !options.forge.no_debug;
    forge_options.only = options.forge.only;
    forge_options.output_policy = options.output_policy;
    forge_options.seed = options.forge.seed;
    forge_options.quality.supersample = options.forge.supersample;
    forge_options.quality.soft_edges = options.forge.soft_edges;
    forge_options.quality.fill_pinholes = !options.forge.no_cleanup;
    forge_options.quality.despeckle = !options.forge.no_cleanup;

    try {
        const auto result = spratforge::forge::run_forge(forge_options);
        nlohmann::json animations = nlohmann::json::array();
        for (const auto& animation : result.metadata.at("animations")) {
            animations.push_back({{"name", animation.at("name")},
                                  {"frame_count", animation.at("frame_count")},
                                  {"fps", animation.at("fps")},
                                  {"loop", animation.at("loop")}});
        }
        nlohmann::json report{{"valid", true},
                              {"diagnostics", nlohmann::json::array()},
                              {"character", forge_options.character},
                              {"output_dir", forge_options.output_dir},
                              {"metadata", "metadata.json"},
                              {"frame", result.metadata.at("frame")},
                              {"anchor", result.metadata.at("anchor")},
                              {"total_frames", result.total_frames},
                              {"files_written", static_cast<int>(result.written_files.size())},
                              {"animations", animations}};
        if (options.verbose) {
            report["written_files"] = result.written_files;
        }
        if (!options.quiet) {
            std::cout << report.dump(2) << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        // Errors always surface, even under --quiet, and always on stderr as
        // well as in the machine-readable report so a shell `set -e` script
        // and a CI JSON consumer both see them.
        const auto report = spratforge::rig::diagnostics_json({{"forge_failed", "forge", error.what()}});
        if (!options.quiet) {
            std::cout << report.dump(2) << '\n';
        }
        const std::string message = error.what();
        const bool already_prefixed = message.rfind("spratforge", 0) == 0 || message.rfind("forge:", 0) == 0;
        std::cerr << (already_prefixed ? "" : "spratforge forge: ") << message << '\n';
        return 5;
    }
}

// `forge --list-animations`: the clip library, machine readable.
int run_list_animations() {
    nlohmann::json animations = nlohmann::json::array();
    for (const auto& clip : spratforge::forge::default_boxer_clips()) {
        animations.push_back({{"name", clip.name},
                              {"category", clip.category},
                              {"direction", clip.direction.empty() ? nlohmann::json(nullptr)
                                                                   : nlohmann::json(clip.direction)},
                              {"alias_of", clip.alias_of.empty() ? nlohmann::json(nullptr)
                                                                 : nlohmann::json(clip.alias_of)},
                              {"frame_count", clip.frame_count},
                              {"fps", clip.fps},
                              {"loop", clip.loop},
                              {"hold_last_frame", clip.hold_last_frame},
                              {"is_attack", clip.impact_frame >= 0},
                              {"active_from", clip.active_from},
                              {"active_to", clip.active_to},
                              {"impact_frame", clip.impact_frame}});
    }
    std::cout << nlohmann::json{{"animations", animations},
                                {"count", static_cast<int>(animations.size())}}
                     .dump(2)
              << '\n';
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::string first = argc > 1 ? argv[1] : "";
    const bool forge_command = first == "forge";
    const bool modern = first == "generate" || first == "rig-validate" || first == "audit" || forge_command;

    if (argc == 1 || first == "--help" || first == "-h" || first == "help") {
        std::cout << spratforge::cli::usage();
        return 0;
    }
    if (first == "--version" || first == "-V" || first == "version") {
        std::cout << spratforge::cli::version_string() << '\n';
        return 0;
    }
    // Per-subcommand help: accepted anywhere in the argument list so
    // `forge --input x --help` works like every other UNIX tool.
    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            std::cout << spratforge::cli::usage_for(modern ? first : "legacy");
            return 0;
        }
        if (argument == "--version" || argument == "-V") {
            std::cout << spratforge::cli::version_string() << '\n';
            return 0;
        }
    }
    const auto result = spratforge::cli::parse_arguments(argc, argv);
    if (!result.options) {
        if (modern) std::cout << spratforge::rig::diagnostics_json({{"arguments", "cli", result.error}}).dump(2) << '\n';
        else std::cerr << result.error << '\n';
        return 2;
    }

    const auto& options = *result.options;
    if (forge_command) {
        if (options.forge.list_animations) return run_list_animations();
        return run_forge_command(options);
    }
    if (modern) {
        auto report = spratforge::rig::diagnostics_json({});
        try {
            if (options.mode == spratforge::cli::Mode::generate) {
                std::string error;
                if (!spratforge::pipeline::generate(*options.input_path, options.output_path, options.profiles, error, options.output_policy)) {
                    report = spratforge::rig::diagnostics_json({{"generate_failed", "generate", error}});
                }
            } else {
                report = options.mode == spratforge::cli::Mode::rig_validate
                    ? spratforge::audit::validate_rig_file(options.profiles.rig_file, options.profiles)
                    : spratforge::audit::audit_path(*options.input_path, options.profiles);
                if (!options.output_path.empty()) {
                    const auto destination = std::filesystem::weakly_canonical(options.output_path);
                    for (const auto& input : {options.input_path.value_or(""), options.profiles.rig_file, options.profiles.rig,
                        options.profiles.rig_override, options.profiles.palette, options.profiles.export_profile}) {
                        if (!input.empty() && destination == std::filesystem::weakly_canonical(input)) throw std::invalid_argument("Diagnostic output cannot overwrite an input");
                    }
                    if (std::filesystem::exists(destination)) throw std::invalid_argument("Diagnostic output already exists");
                    spratforge::atlas::save_metadata_json(options.output_path, report);
                }
            }
        } catch (const std::exception& error) {
            report = spratforge::rig::diagnostics_json({{"command_failed", "cli", error.what()}});
        }
        std::cout << report.dump(2) << '\n';
        return report.at("valid").get<bool>() ? 0 : 5;
    }
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
        case spratforge::cli::Mode::generate:
        case spratforge::cli::Mode::rig_validate:
        case spratforge::cli::Mode::audit:
        case spratforge::cli::Mode::forge:
            return 2;
    }
    if (!rendered) {
        std::cerr << error << '\n';
        return 5;
    }
    return 0;
}