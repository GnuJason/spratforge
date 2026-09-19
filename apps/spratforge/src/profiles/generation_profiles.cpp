#include <spratforge/profiles/generation_profiles.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>

namespace spratforge::profiles {
namespace {
void version(const Json& document) {
    if (bounded_integer(document.at("profile_version"), 1, 1) != 1) throw std::invalid_argument("Unsupported profile version");
}
int optional_int(const Json& document, const std::string& name, int fallback, int low, int high) {
    return document.count(name) ? bounded_integer(document.at(name), low, high) : fallback;
}
bool boolean(const Json& document, const std::string& name, bool fallback) {
    if (!document.count(name)) return fallback;
    if (!document.at(name).is_boolean()) throw std::invalid_argument(name + " must be boolean");
    return document.at(name).get<bool>();
}
std::vector<core::Color> colors(const Json& values) {
    if (!values.is_array() || values.size() > 256) throw std::invalid_argument("Colors must be an array of at most 256 RGB colors");
    std::vector<core::Color> result;
    for (const auto& value : values) {
        if (!value.is_array() || value.size() != 3) throw std::invalid_argument("Expected RGB triple");
        result.push_back({static_cast<std::uint8_t>(bounded_integer(value.at(0), 0, 255)),
            static_cast<std::uint8_t>(bounded_integer(value.at(1), 0, 255)), static_cast<std::uint8_t>(bounded_integer(value.at(2), 0, 255))});
    }
    return result;
}
std::vector<std::string> identifiers(const Json& values) {
    if (!values.is_array()) throw std::invalid_argument("Expected identifier array");
    std::vector<std::string> result;
    for (const auto& value : values) {
        const auto name = identifier(value);
        if (std::find(result.begin(), result.end(), name) != result.end()) throw std::invalid_argument("Duplicate identifier: " + name);
        result.push_back(name);
    }
    return result;
}
std::string output_name(const Json& value, const std::string& suffix) {
    const std::string name = value.get<std::string>();
    if (name.size() <= suffix.size() || name.substr(name.size() - suffix.size()) != suffix) throw std::invalid_argument("Invalid export filename");
    identifier(name.substr(0, name.size() - suffix.size()));
    return name;
}
motion::Interpolation interpolation(const Json& value) {
    const std::string name = value.get<std::string>();
    if (name == "linear") return motion::Interpolation::linear;
    if (name == "ease_in") return motion::Interpolation::ease_in;
    if (name == "ease_out") return motion::Interpolation::ease_out;
    if (name == "ease_in_out") return motion::Interpolation::ease_in_out;
    if (name == "smoothstep") return motion::Interpolation::smoothstep;
    throw std::invalid_argument("Unknown interpolation: " + name);
}
}
Json read_json(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Unable to read JSON: " + path);
    Json document;
    input >> document;
    return document;
}
int bounded_integer(const Json& value, int minimum, int maximum) {
    if (!value.is_number_integer() || value < minimum || value > maximum) throw std::invalid_argument("Expected integer in " + std::to_string(minimum) + ".." + std::to_string(maximum));
    return value.get<int>();
}
void fields(const Json& value, const std::vector<std::string>& allowed) {
    if (!value.is_object()) throw std::invalid_argument("Expected JSON object");
    for (auto entry = value.begin(); entry != value.end(); ++entry) {
        if (std::find(allowed.begin(), allowed.end(), entry.key()) == allowed.end()) throw std::invalid_argument("Unknown field: " + entry.key());
    }
}
std::string identifier(const Json& value) {
    const auto name = value.get<std::string>();
    if (name.empty() || name.size() > 64 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos) throw std::invalid_argument("Invalid identifier: " + name);
    return name;
}
std::string default_profile_path(const std::string& relative) { return (std::filesystem::path(SPRATFORGE_PROFILE_DIR) / relative).string(); }
RigProfile load_rig_profile(const std::string& path) {
    const auto document = read_json(path);
    fields(document, {"profile_version", "max_width", "max_height", "max_colors", "min_confidence", "require_transparency", "required_regions", "overrides"});
    version(document);
    RigProfile result;
    result.max_width = optional_int(document, "max_width", 256, 1, 4096);
    result.max_height = optional_int(document, "max_height", 256, 1, 4096);
    result.max_colors = optional_int(document, "max_colors", 256, 1, 65536);
    result.min_confidence = optional_int(document, "min_confidence", 0, 0, 1000);
    result.require_transparency = boolean(document, "require_transparency", true);
    result.required_regions = identifiers(document.value("required_regions", Json::array()));
    result.overrides = document.value("overrides", Json::object());
    fields(result.overrides, {"joints", "bones", "regions", "pivot"});
    return result;
}
MotionProfile load_motion_profile(const std::string& path) {
    const auto document = read_json(path);
    fields(document, {"profile_version", "name", "frames", "fps", "loop", "durations_ms", "keyframes", "events", "hitboxes"});
    version(document);
    MotionProfile result;
    result.clip.name = identifier(document.at("name"));
    result.clip.frame_count = bounded_integer(document.at("frames"), 1, 1000);
    result.fps = bounded_integer(document.at("fps"), 1, 1000);
    result.clip.loop = boolean(document, "loop", false);
    if (!document.at("keyframes").is_array()) throw std::invalid_argument("Keyframes must be an array");
    for (const auto& key : document.at("keyframes")) {
        fields(key, {"time", "interpolation", "transforms"});
        motion::Keyframe frame{bounded_integer(key.at("time"), 0, 1000), {}, interpolation(key.value("interpolation", Json("linear")))};
        if (!key.at("transforms").is_array()) throw std::invalid_argument("Transforms must be an array");
        for (const auto& transform : key.at("transforms")) {
            fields(transform, {"joint", "dx", "dy", "rotation", "scale_x", "scale_y"});
            frame.pose.transforms.push_back({identifier(transform.at("joint")), optional_int(transform, "dx", 0, -4096, 4096),
                optional_int(transform, "dy", 0, -4096, 4096), optional_int(transform, "rotation", 0, -360, 360),
                optional_int(transform, "scale_x", 100, 50, 200), optional_int(transform, "scale_y", 100, 50, 200)});
        }
        result.clip.keyframes.push_back(std::move(frame));
    }
    (void)motion::sample_pose(result.clip, 0);
    if (document.count("durations_ms")) {
        const auto& values = document.at("durations_ms");
        if (!values.is_array() || values.size() != static_cast<std::size_t>(result.clip.frame_count)) throw std::invalid_argument("Duration count must equal frame count");
        for (const auto& value : values) result.durations.push_back(bounded_integer(value, 1, 60000));
    } else {
        for (int index = 0; index < result.clip.frame_count; ++index) result.durations.push_back((index + 1) * 1000 / result.fps - index * 1000 / result.fps);
    }
    result.events = document.value("events", Json::array());
    result.hitboxes = document.value("hitboxes", Json::array());
    if (!result.events.is_array() || !result.hitboxes.is_array()) throw std::invalid_argument("Events and hitboxes must be arrays");
    for (const auto& event : result.events) {
        fields(event, {"frame", "name"});
        bounded_integer(event.at("frame"), 0, result.clip.frame_count - 1); identifier(event.at("name"));
    }
    for (const auto& box : result.hitboxes) {
        fields(box, {"frame", "x", "y", "w", "h", "kind"});
        bounded_integer(box.at("frame"), 0, result.clip.frame_count - 1);
        bounded_integer(box.at("x"), -4096, 4096); bounded_integer(box.at("y"), -4096, 4096);
        bounded_integer(box.at("w"), 1, 4096); bounded_integer(box.at("h"), 1, 4096); identifier(box.at("kind"));
    }
    return result;
}
PaletteProfile load_palette_profile(const std::string& path) {
    const auto document = read_json(path);
    fields(document, {"profile_version", "name", "allowed_colors", "locks", "roles", "ramps", "variants", "variants_file", "animation_styles"});
    version(document);
    PaletteProfile result;
    result.name = identifier(document.at("name"));
    result.allowed_colors = colors(document.value("allowed_colors", Json::array()));
    result.locks = colors(document.value("locks", Json::array()));
    std::set<std::string> regions;
    const auto roles = document.value("roles", Json::object());
    if (!roles.is_object()) throw std::invalid_argument("Palette roles must be an object");
    for (auto role = roles.begin(); role != roles.end(); ++role) {
        identifier(role.key());
        result.roles[role.key()] = identifiers(role.value());
        for (const auto& name : result.roles.at(role.key())) if (!regions.insert(name).second) throw std::invalid_argument("Region has multiple palette roles");
    }
    const auto ramps = document.value("ramps", Json::object());
    if (!ramps.is_object()) throw std::invalid_argument("Ramps must be an object");
    for (auto ramp = ramps.begin(); ramp != ramps.end(); ++ramp) {
        if (!result.roles.count(ramp.key())) throw std::invalid_argument("Ramp references unknown role");
        result.ramps[ramp.key()] = colors(ramp.value());
        if (result.ramps.at(ramp.key()).empty()) throw std::invalid_argument("Ramp cannot be empty");
    }
    Json variants = document.value("variants", Json::object());
    if (document.count("variants_file")) {
        if (document.count("variants")) throw std::invalid_argument("Choose variants or variants_file");
        const auto filename = output_name(document.at("variants_file"), ".json");
        const auto variant_document = read_json((std::filesystem::path(path).parent_path() / filename).string());
        fields(variant_document, {"profile_version", "variants"}); version(variant_document);
        variants = variant_document.at("variants");
    }
    if (!variants.is_object()) throw std::invalid_argument("Variants must be an object");
    for (auto variant = variants.begin(); variant != variants.end(); ++variant) {
        identifier(variant.key());
        if (variant.key() == "default" || !variant.value().is_object()) throw std::invalid_argument("Invalid palette variant");
        result.variants[variant.key()] = {};
        for (auto role = variant.value().begin(); role != variant.value().end(); ++role) {
            const auto mapped = colors(role.value());
            if (!result.ramps.count(role.key()) || mapped.size() != result.ramps.at(role.key()).size()) throw std::invalid_argument("Variant must preserve role ramp length");
            result.variants[variant.key()][role.key()] = mapped;
        }
    }
    result.animation_styles = document.value("animation_styles", Json::object());
    if (!result.animation_styles.is_object()) throw std::invalid_argument("Animation styles must be an object");
    for (auto style = result.animation_styles.begin(); style != result.animation_styles.end(); ++style) {
        identifier(style.key()); fields(style.value(), {"desaturate", "flash"});
        (void)boolean(style.value(), "desaturate", false);
        if (style.value().count("flash")) (void)colors(Json::array({style.value().at("flash")}));
    }
    return result;
}
ExportProfile load_export_profile(const std::string& path) {
    const auto document = read_json(path);
    fields(document, {"profile_version", "schema_version", "format", "layout", "columns", "padding", "atlas_png", "atlas_json", "manifest_json", "write_frames", "write_sheets"});
    version(document);
    if (document.at("format") != "ringqueen" || document.at("layout") != "global") throw std::invalid_argument("Only ringqueen global exports are supported");
    ExportProfile result;
    result.schema_version = bounded_integer(document.at("schema_version"), 1, 1);
    result.columns = bounded_integer(document.at("columns"), 1, 256);
    result.padding = bounded_integer(document.at("padding"), 0, 64);
    result.atlas_png = output_name(document.at("atlas_png"), ".png");
    result.atlas_json = output_name(document.at("atlas_json"), ".json");
    result.manifest_json = output_name(document.at("manifest_json"), ".json");
    if (result.atlas_json == result.manifest_json || result.atlas_json == "rig.json" || result.atlas_json == "anchor.json" ||
        result.manifest_json == "rig.json" || result.manifest_json == "anchor.json") throw std::invalid_argument("Export filenames collide");
    result.write_frames = boolean(document, "write_frames", true);
    result.write_sheets = boolean(document, "write_sheets", true);
    return result;
}
GenerationProfiles load_generation_profiles(const ProfilePaths& paths) {
    GenerationProfiles result;
    result.rig = load_rig_profile(paths.rig.empty() ? default_profile_path("rig/boxer_default.json") : paths.rig);
    result.palette = load_palette_profile(paths.palette.empty() ? default_profile_path("palette/boxer_default.json") : paths.palette);
    result.output = load_export_profile(paths.export_profile.empty() ? default_profile_path("export/ringqueen.json") : paths.export_profile);
    if (paths.variant != "default" && !result.palette.variants.count(paths.variant)) throw std::invalid_argument("Unknown variant: " + paths.variant);
    const auto directory = paths.motion_directory.empty() ? default_profile_path("motion") : paths.motion_directory;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) if (entry.is_regular_file() && entry.path().extension() == ".json") files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    std::set<std::string> names;
    for (const auto& file : files) {
        auto profile = load_motion_profile(file.string());
        if (!names.insert(profile.clip.name).second || profile.clip.name == "atlas" || profile.clip.name == "manifest" || profile.clip.name == "rig" || profile.clip.name == "anchor" ||
            profile.clip.name + ".png" == result.output.atlas_png || profile.clip.name + ".json" == result.output.atlas_json || profile.clip.name + ".json" == result.output.manifest_json) throw std::invalid_argument("Duplicate or reserved animation name");
        result.motions.push_back(std::move(profile));
    }
    if (result.motions.empty()) throw std::invalid_argument("No motion profiles found");
    std::sort(result.motions.begin(), result.motions.end(), [](const auto& left, const auto& right) { return left.clip.name < right.clip.name; });
    return result;
}
core::Frame apply_style(const core::Frame& source, const rig::RigDefinition& rig, const PaletteProfile& profile,
                        const std::string& variant, const std::string& animation) {
    if (!rig::validate_rig(rig).empty() || source.width != rig.width || source.height != rig.height || source.rgba.size() != rig.silhouette.size() * 4) throw std::invalid_argument("Palette source and rig mismatch");
    if (variant != "default" && !profile.variants.count(variant)) throw std::invalid_argument("Unknown palette variant");
    for (const auto& role : profile.roles) for (const auto& name : role.second) {
        if (std::none_of(rig.regions.begin(), rig.regions.end(), [&](const auto& region) { return region.name == name; })) throw std::invalid_argument("Unknown palette region: " + name);
    }
    core::Frame result = source;
    for (const auto& region : rig.regions) {
        std::string role_name;
        for (const auto& role : profile.roles) if (std::find(role.second.begin(), role.second.end(), region.name) != role.second.end()) role_name = role.first;
        for (std::size_t pixel = 0; pixel < region.pixels.size(); ++pixel) {
            if (!region.pixels[pixel]) continue;
            const auto offset = pixel * 4;
            core::Color color{source.rgba[offset], source.rgba[offset + 1], source.rgba[offset + 2]};
            if (std::find(profile.locks.begin(), profile.locks.end(), color) != profile.locks.end()) continue;
            if (variant != "default" && profile.variants.at(variant).count(role_name)) {
                const auto& ramp = profile.ramps.at(role_name);
                const auto nearest = core::nearest_color(color, {role_name, ramp});
                color = profile.variants.at(variant).at(role_name).at(static_cast<std::size_t>(std::find(ramp.begin(), ramp.end(), nearest) - ramp.begin()));
            }
            if (profile.animation_styles.count(animation)) {
                const auto& style = profile.animation_styles.at(animation);
                if (style.value("desaturate", false)) {
                    const auto gray = static_cast<std::uint8_t>((77 * color.r + 150 * color.g + 29 * color.b) / 256);
                    color = {gray, gray, gray};
                }
                if (style.count("flash")) color = colors(Json::array({style.at("flash")})).front();
            }
            result.rgba[offset] = color.r; result.rgba[offset + 1] = color.g; result.rgba[offset + 2] = color.b;
        }
    }
    return result;
}
}  // namespace spratforge::profiles