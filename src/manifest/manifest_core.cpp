#include "manifest/manifest_core.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <json/json.hpp>

namespace spratforge::manifest {
Manifest generate_manifest(const anchor::AnchorData& anchor, const std::vector<templates::AnimationTemplate>& templates,
                           const std::string& palette_name) {
    Manifest result{.anchor = anchor, .palette_name = palette_name};
    for (const auto& item : templates) result.animations.push_back({item.name, item.fps, item.frame_count});
    return result;
}
void save_manifest_json(const std::string& path, const Manifest& manifest) {
    nlohmann::json json{{"anchor", {{"size", {manifest.anchor.width, manifest.anchor.height}},
                                    {"bounds", {{"x", manifest.anchor.bounds.x}, {"y", manifest.anchor.bounds.y}, {"width", manifest.anchor.bounds.width}, {"height", manifest.anchor.bounds.height}}},
                                    {"pivot", {{"x", manifest.anchor.pivot.x}, {"y", manifest.anchor.pivot.y}}}}},
                        {"anchor_reference", "anchor.json"}, {"palette", manifest.palette_name}, {"animations", nlohmann::json::array()}};
    for (const auto& item : manifest.animations) json["animations"].push_back({{"name", item.name}, {"fps", item.fps}, {"frames", item.frame_count}});
    const std::filesystem::path output(path); if (output.has_parent_path()) std::filesystem::create_directories(output.parent_path());
    std::ofstream file(path); if (!file) throw std::runtime_error("Unable to write manifest: " + path);
    file << json.dump(2) << '\n';
}
}  // namespace spratforge::manifest
