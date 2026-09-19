#pragma once

#include <string>
#include <vector>
#include <json/json.hpp>

#include <spratforge/anchor/anchor_core.hpp>
#include <spratforge/templates/template_engine.hpp>

namespace spratforge::manifest {

struct AnimationMetadata {
    std::string name;
    int fps = 0;
    int frame_count = 0;
};
struct Manifest {
    anchor::AnchorData anchor;
    std::vector<AnimationMetadata> animations;
    std::string palette_name;
};

Manifest generate_manifest(const anchor::AnchorData& anchor, const std::vector<templates::AnimationTemplate>& templates,
                           const std::string& palette_name);
void save_manifest_json(const std::string& path, const Manifest& manifest);
nlohmann::json generate_manifest(const nlohmann::json& atlas_metadata, const std::string& atlas_reference);

}  // namespace spratforge::manifest
