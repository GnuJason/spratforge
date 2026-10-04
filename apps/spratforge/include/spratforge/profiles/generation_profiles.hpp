#pragma once

#include <map>
#include <string>
#include <vector>
#include <spratforge/motion/pose.hpp>

namespace spratforge::profiles {
using Json = nlohmann::json;
struct ProfilePaths {
    std::string rig;
    std::string motion_directory;
    std::string palette;
    std::string export_profile;
    std::string rig_file;
    std::string rig_override;
    std::string variant = "default";
};
struct RigProfile {
    int max_width = 4096;
    int max_height = 4096;
    int max_colors = 65536;
    int min_confidence = 0;
    bool require_transparency = true;
    std::vector<std::string> required_regions;
    Json overrides = Json::object();
};
struct MotionProfile {
    motion::MotionClip clip;
    int fps = 8;
    std::vector<int> durations;
    Json events = Json::array();
    Json hitboxes = Json::array();
};
struct PaletteProfile {
    std::string name;
    std::vector<core::Color> allowed_colors;
    std::vector<core::Color> locks;
    std::map<std::string, std::vector<std::string>> roles;
    std::map<std::string, std::vector<core::Color>> ramps;
    std::map<std::string, std::map<std::string, std::vector<core::Color>>> variants;
    Json animation_styles = Json::object();
};
struct ExportProfile {
    int schema_version = 1;
    int columns = 8;
    int padding = 1;
    std::string atlas_png = "atlas.png";
    std::string atlas_json = "atlas.json";
    std::string manifest_json = "manifest.json";
    bool write_frames = true;
    bool write_sheets = true;
};
struct GenerationProfiles {
    RigProfile rig;
    std::vector<MotionProfile> motions;
    PaletteProfile palette;
    ExportProfile output;
};

Json read_json(const std::string& path);
int bounded_integer(const Json& value, int minimum, int maximum);
void fields(const Json& value, const std::vector<std::string>& allowed);
std::string identifier(const Json& value);
std::string default_profile_path(const std::string& relative);
RigProfile load_rig_profile(const std::string& path);
MotionProfile load_motion_profile(const std::string& path);
PaletteProfile load_palette_profile(const std::string& path);
ExportProfile load_export_profile(const std::string& path);
GenerationProfiles load_generation_profiles(const ProfilePaths& paths);
core::Frame apply_style(const core::Frame& source, const rig::RigDefinition& rig,
                        const PaletteProfile& profile, const std::string& variant, const std::string& animation);
}  // namespace spratforge::profiles