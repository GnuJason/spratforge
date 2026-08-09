#include <spratforge/cli/cli_parser.hpp>

#include <charconv>
#include <string_view>

#include <spratforge/ai/ai_motion_core.hpp>
#include <spratforge/atlas/atlas_core.hpp>

namespace spratforge::cli {
namespace {

std::optional<Mode> parse_mode(std::string_view value) {
    if (value == "single") return Mode::single;
    if (value == "profile") return Mode::profile;
    if (value == "atlas") return Mode::atlas;
    if (value == "ai-motion") return Mode::ai_motion;
    return std::nullopt;
}

std::optional<int> parse_nonnegative_int(std::string_view value) {
    int parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed < 0) return std::nullopt;
    return parsed;
}

}  // namespace

ParseResult parse_arguments(int argc, char* argv[]) {
    Options options{};
    bool has_mode = false;
    bool has_output = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--dither") {
            options.dither = true;
            continue;
        }
        if (argument == "--verbose") {
            options.verbose = true;
            continue;
        }
        if (argument == "--mode" || argument == "--out" || argument == "--input" || argument == "--grid" || argument == "--turnkey" ||
            argument == "--profile" || argument == "--atlas" || argument == "--padding" || argument == "--palette" ||
            argument == "--motion") {
            if (++index >= argc) return {.error = "Missing value for " + std::string(argument)};
            const std::string value = argv[index];
            if (argument == "--mode") {
                const auto mode = parse_mode(value);
                if (!mode) return {.error = "Invalid mode: " + value};
                options.mode = *mode;
                has_mode = true;
            } else if (argument == "--turnkey") {
                options.mode = Mode::turnkey;
                options.input_path = value;
                has_mode = true;
            } else if (argument == "--out") {
                options.output_path = value;
                has_output = true;
            } else if (argument == "--input") {
                options.input_path = value;
            } else if (argument == "--grid") {
                options.grid_dimensions = value;
            } else if (argument == "--profile") {
                options.profile_name = value;
            } else if (argument == "--atlas") {
                if (!atlas::parse_atlas_dimensions(value)) return {.error = "Invalid atlas dimensions: " + value};
                options.atlas_dimensions = value;
            } else if (argument == "--padding") {
                const auto padding = parse_nonnegative_int(value);
                if (!padding) return {.error = "Invalid atlas padding: " + value};
                options.atlas_padding = *padding;
            } else if (argument == "--palette") {
                options.palette_mode = value;
            } else {
                options.motion_vector = value;
            }
        } else if (argument == "--help" || argument == "-h") {
            return {.error = usage()};
        } else {
            return {.error = "Unknown argument: " + std::string(argument)};
        }
    }

    if (!has_mode || !has_output) return {.error = "--mode and --out are required"};
    if (options.mode == Mode::profile && !options.profile_name) {
        return {.error = "--profile is required when --mode profile is selected"};
    }
    if (options.mode == Mode::atlas && !options.atlas_dimensions) {
        return {.error = "--atlas is required when --mode atlas is selected"};
    }
    if (options.mode == Mode::atlas && !options.profile_name) {
        return {.error = "--profile is required when --mode atlas is selected"};
    }
    if (!options.input_path) {
        return {.error = "--input is required for rendering modes"};
    }
    if (options.grid_dimensions && !atlas::parse_atlas_dimensions(*options.grid_dimensions)) {
        return {.error = "Invalid grid dimensions: " + *options.grid_dimensions};
    }
    if (options.mode == Mode::ai_motion && !options.motion_vector) {
        return {.error = "--motion X,Y is required when --mode ai-motion is selected"};
    }
    if (options.mode == Mode::ai_motion) {
        try {
            (void)ai::parse_motion(*options.motion_vector);
        } catch (const std::invalid_argument& error) {
            return {.error = error.what()};
        }
    }
    return {.options = std::move(options)};
}

std::string usage() {
        return "Usage: spratforge_cli --turnkey <input.png> --out <output-directory> | --mode <single|profile|atlas|ai-motion> --out <path> "
            "[--input <png>] [--grid <width>x<height>] [--profile <name>] [--atlas <columns>x<rows>] [--padding <pixels>] "
            "[--palette <nes|gb|strict>] [--dither] [--motion <x,y>] [--verbose]";
}

}  // namespace spratforge::cli