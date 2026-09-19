#pragma once

#include <string>
#include <vector>

#include <json/json.hpp>
#include <spratforge/anchor/anchor_core.hpp>

namespace spratforge::rig {

struct Joint {
    std::string name;
    int x = 0;
    int y = 0;
    int confidence = 0;
};
struct Bone { std::string parent; std::string child; };
struct RegionMask {
    std::string name;
    std::string joint;
    int z_order = 0;
    std::vector<std::uint8_t> pixels;
};
struct RigDefinition {
    int width = 0;
    int height = 0;
    anchor::Pivot pivot;
    std::vector<std::uint8_t> silhouette;
    std::vector<Joint> joints;
    std::vector<Bone> bones;
    std::vector<RegionMask> regions;
};
using PixelRig = RigDefinition;

struct Diagnostic { std::string code; std::string path; std::string message; };
std::vector<Diagnostic> validate_rig(const RigDefinition& rig);
nlohmann::json diagnostics_json(const std::vector<Diagnostic>& diagnostics);
RigDefinition build_rig(const core::Frame& source);
void segment_regions(RigDefinition& rig);
nlohmann::json rig_json(const RigDefinition& rig);
RigDefinition rig_from_json(const nlohmann::json& document);
RigDefinition apply_overrides(const RigDefinition& rig, const nlohmann::json& overrides);
RigDefinition load_rig(const std::string& path);
RigDefinition load_rig_overrides(const RigDefinition& rig, const std::string& path);
void save_rig(const std::string& path, const RigDefinition& rig);

}  // namespace spratforge::rig