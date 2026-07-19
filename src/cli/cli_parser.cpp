#include "cli/cli_parser.hpp"

#include <string_view>

namespace spratforge::cli {
namespace {

std::optional<Mode> parse_mode(std::string_view value) {
    if (value == "single") return Mode::single;
    if (value == "profile") return Mode::profile;
    if (value == "atlas") return Mode::atlas;
    return std::nullopt;
}

}  // namespace

ParseResult parse_arguments(int argc, char* argv[]) {
    Options options{};
    bool has_mode = false;
    bool has_output = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--mode" || argument == "--out" || argument == "--profile" || argument == "--atlas") {
            if (++index >= argc) return {.error = "Missing value for " + std::string(argument)};
            const std::string value = argv[index];
            if (argument == "--mode") {
                const auto mode = parse_mode(value);
                if (!mode) return {.error = "Invalid mode: " + value};
                options.mode = *mode;
                has_mode = true;
            } else if (argument == "--out") {
                options.output_path = value;
                has_output = true;
            } else if (argument == "--profile") {
                options.profile_name = value;
            } else {
                options.atlas_dimensions = value;
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
    return {.options = std::move(options)};
}

std::string usage() {
    return "Usage: spratforge_cli --mode <single|profile|atlas> --out <path> "
           "[--profile <name>] [--atlas <columns>x<rows>]";
}

}  // namespace spratforge::cli