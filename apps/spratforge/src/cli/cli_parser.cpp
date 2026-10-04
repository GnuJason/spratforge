#include <spratforge/cli/cli_parser.hpp>

#include <charconv>
#include <string_view>
#include <set>

#include <spratforge/ai/ai_motion_core.hpp>
#include <spratforge/atlas/atlas_core.hpp>

namespace spratforge::cli {
namespace {
ParseResult parse_command(int argc, char* argv[]) {
    Options options{};
    const std::string command = argv[1];
    options.mode = command == "generate" ? Mode::generate : command == "rig-validate" ? Mode::rig_validate : Mode::audit;
    std::set<std::string> allowed{"--out", "--rig-profile"};
    if (options.mode == Mode::rig_validate) allowed.insert({"--rig", "--rig-override"});
    else allowed.insert({"--input", "--rig", "--palette-profile", "--export-profile"});
    if (options.mode == Mode::generate) allowed.insert({"--motion-dir", "--rig-override", "--variant"});
    const std::set<std::string> bare_flags{"--force", "--clean", "--quiet", "--verbose"};
    std::set<std::string> seen;
    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        if (bare_flags.count(argument)) {
            if (!seen.insert(argument).second) return {.error = "Duplicate option: " + argument};
            if (argument == "--force") options.output_policy = core::OutputPolicy::Force;
            else if (argument == "--clean") options.output_policy = core::OutputPolicy::Clean;
            else if (argument == "--quiet") options.quiet = true;
            else options.verbose = true;
            continue;
        }
        if (!allowed.count(argument)) {
            return {.error = "Unsupported option for " + command + ": " + argument +
                             "  (see: spratforge_cli " + command + " --help)"};
        }
        if (!seen.insert(argument).second) return {.error = "Duplicate option: " + argument};
        if (++index >= argc || std::string_view(argv[index]).starts_with("--") || std::string_view(argv[index]).empty()) return {.error = "Missing value for " + argument};
        const std::string value = argv[index];
        if (argument == "--input") options.input_path = value;
        else if (argument == "--out") options.output_path = value;
        else if (argument == "--rig") options.profiles.rig_file = value;
        else if (argument == "--rig-override") options.profiles.rig_override = value;
        else if (argument == "--rig-profile") options.profiles.rig = value;
        else if (argument == "--motion-dir") options.profiles.motion_directory = value;
        else if (argument == "--palette-profile") options.profiles.palette = value;
        else if (argument == "--export-profile") options.profiles.export_profile = value;
        else if (argument == "--variant") options.profiles.variant = value;
    }
    if (options.mode == Mode::rig_validate && options.profiles.rig_file.empty()) return {.error = "rig-validate requires --rig"};
    if (options.mode != Mode::rig_validate && !options.input_path) return {.error = command + " requires --input"};
    if (options.mode == Mode::generate && options.output_path.empty()) return {.error = "generate requires --out"};
    return {.options = std::move(options)};
}

std::optional<int> parse_nonnegative_int(std::string_view value) {
    int parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed < 0) return std::nullopt;
    return parsed;
}

// `forge`: single-sprite -> full animation set. Flag driven, no profile files.
ParseResult parse_forge(int argc, char* argv[]) {
    Options options{};
    options.mode = Mode::forge;
    const std::set<std::string> value_flags{"--input",  "--out",   "--character",       "--rig-override",
                                            "--fps",    "--scale", "--alpha-threshold", "--margin",
                                            "--only",   "--supersample", "--seed"};
    const std::set<std::string> bare_flags{"--no-sheets",      "--no-debug", "--force",
                                           "--clean",          "--quiet",    "--verbose",
                                           "--list-animations", "--soft-edges", "--no-cleanup"};
    std::set<std::string> seen;
    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        if (bare_flags.count(argument)) {
            if (!seen.insert(argument).second) return {.error = "Duplicate option: " + argument};
            if (argument == "--no-sheets") options.forge.no_sheets = true;
            else if (argument == "--no-debug") options.forge.no_debug = true;
            else if (argument == "--soft-edges") options.forge.soft_edges = true;
            else if (argument == "--no-cleanup") options.forge.no_cleanup = true;
            else if (argument == "--list-animations") options.forge.list_animations = true;
            else if (argument == "--quiet") options.quiet = true;
            else if (argument == "--verbose") options.verbose = true;
            else if (argument == "--force") options.output_policy = core::OutputPolicy::Force;
            else if (argument == "--clean") options.output_policy = core::OutputPolicy::Clean;
            continue;
        }
        if (!value_flags.count(argument)) return {.error = "Unsupported option for forge: " + argument};
        if (!seen.insert(argument).second) return {.error = "Duplicate option: " + argument};
        if (++index >= argc || std::string_view(argv[index]).starts_with("--") || std::string_view(argv[index]).empty())
            return {.error = "Missing value for " + argument};
        const std::string value = argv[index];
        if (argument == "--input") options.input_path = value;
        else if (argument == "--out") options.output_path = value;
        else if (argument == "--character") options.forge.character = value;
        else if (argument == "--rig-override") options.forge.rig_override = value;
        else if (argument == "--only") {
            // Comma separated, order-insensitive; emission order stays the
            // clip library's own order so --only never reshuffles frames.
            std::string current;
            for (const char c : value + ",") {
                if (c == ',') {
                    if (!current.empty()) options.forge.only.push_back(current);
                    current.clear();
                } else if (c != ' ') {
                    current.push_back(c);
                }
            }
            if (options.forge.only.empty()) return {.error = "--only requires at least one animation name"};
        }
        else {
            const auto parsed = parse_nonnegative_int(value);
            if (!parsed) return {.error = "Invalid integer for " + argument + ": " + value};
            if (argument == "--fps") options.forge.fps = *parsed;
            else if (argument == "--scale") options.forge.scale = *parsed;
            else if (argument == "--alpha-threshold") options.forge.alpha_threshold = *parsed;
            else if (argument == "--margin") options.forge.margin = *parsed;
            else if (argument == "--supersample") {
                if (*parsed < 1 || *parsed > 8) return {.error = "--supersample must be between 1 and 8"};
                options.forge.supersample = *parsed;
            }
            else options.forge.seed = static_cast<unsigned int>(*parsed);
        }
    }
    if (options.quiet && options.verbose) return {.error = "--quiet and --verbose are mutually exclusive"};
    // --list-animations is informational: it needs neither input nor output.
    if (options.forge.list_animations) return {.options = std::move(options)};
    if (!options.input_path) {
        return {.error = "forge requires --input <neutral-sprite.png>  (see: spratforge_cli forge --help)"};
    }
    if (options.output_path.empty()) {
        return {.error = "forge requires --out <directory>  (see: spratforge_cli forge --help)"};
    }
    return {.options = std::move(options)};
}

std::optional<Mode> parse_mode(std::string_view value) {
    if (value == "single") return Mode::single;
    if (value == "profile") return Mode::profile;
    if (value == "atlas") return Mode::atlas;
    if (value == "ai-motion") return Mode::ai_motion;
    return std::nullopt;
}

}  // namespace

ParseResult parse_arguments(int argc, char* argv[]) {
    if (argc > 1 && std::string_view(argv[1]) == "forge") return parse_forge(argc, argv);
    if (argc > 1 && (std::string_view(argv[1]) == "generate" || std::string_view(argv[1]) == "rig-validate" || std::string_view(argv[1]) == "audit")) return parse_command(argc, argv);
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

namespace {

const char* kForgeHelp =
    "spratforge forge - turn ONE neutral sprite into a complete animation set.\n"
    "\n"
    "Usage:\n"
    "  spratforge_cli forge --input <sprite.png> --out <directory> [options]\n"
    "  spratforge_cli forge --list-animations\n"
    "\n"
    "Required:\n"
    "  --input <png>            Neutral, front-facing source sprite (32..2048 px per\n"
    "                           side, non-square is fine).\n"
    "  --out <directory>        Output directory. Refuses to write into a non-empty\n"
    "                           directory unless --force or --clean is given.\n"
    "\n"
    "Output policy:\n"
    "  --force                  Write over the directory; never deletes anything.\n"
    "  --clean                  Delete only the files recorded in this directory's\n"
    "                           .spratforge-manifest.json from a previous run, then\n"
    "                           write. Files spratforge did not create are never\n"
    "                           touched, and the run still refuses if any remain.\n"
    "\n"
    "Selection:\n"
    "  --list-animations        Print the clip library (name, frames, fps, flags).\n"
    "  --only <a,b,c>           Emit only these animations. Emission order always\n"
    "                           follows the library order.\n"
    "\n"
    "Appearance:\n"
    "  --character <name>       Character id written into metadata (default: boxer).\n"
    "  --rig-override <json>    Hand-authored joint positions layered over the\n"
    "                           estimated rig.\n"
    "  --scale <1-16>           Integer nearest-neighbour magnification (default: 1).\n"
    "  --alpha-threshold <1-255> Silhouette cut-off (default: 16).\n"
    "  --margin <0-256>         Extra canvas padding in source pixels (default: 2).\n"
    "  --fps <1-240>            Override every clip's frame rate.\n"
    "  --supersample <1-8>      Sub-samples per destination axis (default: 3).\n"
    "                           1 reproduces the legacy nearest-neighbour look.\n"
    "  --soft-edges             Premultiplied-alpha soft edges instead of the\n"
    "                           palette-preserving hard edges (adds new colours).\n"
    "  --no-cleanup             Disable pinhole filling and despeckling.\n"
    "  --no-sheets              Skip the per-animation strip sheets.\n"
    "  --no-debug               Skip rig_debug.png.\n"
    "  --seed <n>               Reserved. The pipeline has no RNG, so this cannot\n"
    "                           change the output; it is recorded in metadata.json.\n"
    "\n"
    "Reporting:\n"
    "  --quiet                  Suppress the JSON run summary (errors still print).\n"
    "  --verbose                Also list every written file in the summary.\n"
    "  --help, -h               This help.\n"
    "\n"
    "Examples:\n"
    "  # Full set from the RingQueen master sprite\n"
    "  spratforge_cli forge --input assets/master_boxer.png --out build/boxer\n"
    "\n"
    "  # Re-run into the same directory, removing only what the last run wrote\n"
    "  spratforge_cli forge --input assets/master_boxer.png --out build/boxer --clean\n"
    "\n"
    "  # Just the four directional walks, 2x magnified, quietly\n"
    "  spratforge_cli forge --input assets/master_boxer.png --out build/walks \\\n"
    "      --only walk_left,walk_right,walk_up,walk_down --scale 2 --quiet\n"
    "\n"
    "Exit codes: 0 ok, 2 bad arguments, 5 run failed.\n";

const char* kGenerateHelp =
    "spratforge generate - profile-driven pipeline (legacy, profile JSON required).\n"
    "\n"
    "Usage:\n"
    "  spratforge_cli generate --input <png> --out <directory> [options]\n"
    "\n"
    "Options:\n"
    "  --input <png>            Source sprite.\n"
    "  --out <directory>        Output directory (same policy as forge).\n"
    "  --force | --clean        Non-empty output directory policy; see forge --help.\n"
    "  --rig <json>             Rig definition.\n"
    "  --rig-profile <json>     Rig limits profile.\n"
    "  --rig-override <json>    Rig overrides.\n"
    "  --motion-dir <directory> Motion template directory.\n"
    "  --palette-profile <json> Palette profile.\n"
    "  --variant <name>         Palette variant.\n"
    "  --export-profile <json>  Export profile.\n"
    "  --quiet | --verbose      Reporting level.\n"
    "\n"
    "Examples:\n"
    "  spratforge_cli generate --input art/boxer.png --out build/boxer \\\n"
    "      --rig profiles/rig/boxer_default.json --clean\n"
    "\n"
    "Exit codes: 0 ok, 2 bad arguments, 5 run failed.\n";

const char* kRigValidateHelp =
    "spratforge rig-validate - validate a rig JSON against a rig profile.\n"
    "\n"
    "Usage:\n"
    "  spratforge_cli rig-validate --rig <json> [--rig-profile <json>]\n"
    "                              [--rig-override <json>] [--out <report.json>]\n"
    "\n"
    "Examples:\n"
    "  spratforge_cli rig-validate --rig profiles/rig/boxer_default.json\n"
    "\n"
    "Exit codes: 0 valid, 2 bad arguments, 5 invalid rig.\n";

const char* kAuditHelp =
    "spratforge audit - audit a sprite or a generated output directory.\n"
    "\n"
    "Usage:\n"
    "  spratforge_cli audit --input <png-or-directory> [--rig <json>]\n"
    "                       [--rig-profile <json>] [--palette-profile <json>]\n"
    "                       [--export-profile <json>] [--out <report.json>]\n"
    "\n"
    "Examples:\n"
    "  spratforge_cli audit --input build/boxer\n"
    "\n"
    "Exit codes: 0 clean, 2 bad arguments, 5 findings reported.\n";

const char* kLegacyHelp =
    "spratforge legacy render modes.\n"
    "\n"
    "Usage:\n"
    "  spratforge_cli --turnkey <input.png> --out <directory>\n"
    "  spratforge_cli --mode <single|profile|atlas|ai-motion> --out <path>\n"
    "                 [--input <png>] [--grid <w>x<h>] [--profile <name>]\n"
    "                 [--atlas <cols>x<rows>] [--padding <px>]\n"
    "                 [--palette <nes|gb|strict>] [--dither] [--motion <x,y>]\n"
    "                 [--verbose]\n"
    "\n"
    "Prefer `forge` for new work; these modes are kept for existing scripts.\n";

}  // namespace

std::string version_string() {
    return std::string("spratforge ") + SPRATFORGE_VERSION;
}

std::string usage_for(const std::string& subcommand) {
    if (subcommand == "forge") return kForgeHelp;
    if (subcommand == "generate") return kGenerateHelp;
    if (subcommand == "rig-validate") return kRigValidateHelp;
    if (subcommand == "audit") return kAuditHelp;
    if (subcommand == "legacy") return kLegacyHelp;
    return usage();
}

std::string usage() {
    return std::string(
        "spratforge - deterministic sprite animation forge.\n"
        "\n"
        "Usage: spratforge_cli <subcommand> [options]\n"
        "\n"
        "Subcommands:\n"
        "  forge         One neutral sprite -> a full, game-ready animation set.\n"
        "  generate      Profile-driven pipeline (legacy).\n"
        "  rig-validate  Validate a rig JSON against a rig profile.\n"
        "  audit         Audit a sprite or a generated output directory.\n"
        "  legacy        Single/profile/atlas/ai-motion render modes.\n"
        "\n"
        "Global:\n"
        "  --help, -h        This overview. `<subcommand> --help` for details.\n"
        "  --version, -V     Print the version and exit.\n"
        "\n"
        "Quick start:\n"
        "  spratforge_cli forge --input sprite.png --out out/ --clean\n"
        "  spratforge_cli forge --help\n"
        "\n"
        "Exit codes: 0 success, 2 bad arguments, 3-4 legacy mode errors, 5 run failed.\n");
}

}  // namespace spratforge::cli