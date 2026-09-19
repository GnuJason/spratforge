#include <spratforge/rig/pixel_rig.hpp>

#include <algorithm>
#include <map>
#include <set>

namespace spratforge::rig {
std::vector<Diagnostic> validate_rig(const RigDefinition& rig) {
    std::vector<Diagnostic> result;
    const auto error = [&](const std::string& code, const std::string& path, const std::string& message) {
        result.push_back({code, path, message});
    };
    if (rig.width < 1 || rig.height < 1 || rig.width > 4096 || rig.height > 4096) {
        error("dimensions", "rig", "Dimensions must be between 1 and 4096"); return result;
    }
    const auto count = static_cast<std::size_t>(rig.width) * rig.height;
    if (rig.silhouette.size() != count) {
        error("mask_size", "silhouette", "Silhouette size does not match dimensions"); return result;
    }
    int bottom = -1;
    for (std::size_t pixel = 0; pixel < count; ++pixel) {
        if (rig.silhouette[pixel] > 1) error("mask_value", "silhouette", "Silhouette must be binary");
        if (rig.silhouette[pixel]) bottom = static_cast<int>(pixel / rig.width);
    }
    if (bottom < 0) error("empty", "silhouette", "No visible source pixels");
    if (rig.pivot.x < 0 || rig.pivot.x >= rig.width || rig.pivot.y != bottom) {
        error("pivot", "pivot", "Pivot must remain on the source ground row and inside the canvas");
    }
    if (rig.joints.size() > 64) {
        error("joint_limit", "joints", "A pixel rig supports at most 64 joints"); return result;
    }
    std::set<std::string> names;
    for (const auto& joint : rig.joints) {
        if (joint.name.empty() || !names.insert(joint.name).second) error("joint_name", joint.name, "Joint names must be unique and nonempty");
        if (joint.x < 0 || joint.x >= rig.width || joint.y < 0 || joint.y >= rig.height) error("joint_bounds", joint.name, "Joint is outside canvas");
        if (joint.confidence < 0 || joint.confidence > 1000) error("confidence", joint.name, "Confidence must be between 0 and 1000");
    }
    std::vector<std::string> required{"torso", "neck", "head"};
    for (const std::string side : {"left", "right"}) for (const std::string part : {"shoulder", "elbow", "wrist", "hip", "knee", "ankle"}) required.push_back(side + "_" + part);
    for (const auto& name : required) if (!names.count(name)) error("missing_joint", name, "Required boxer joint is missing");
    std::map<std::string, std::string> parents;
    for (const auto& bone : rig.bones) {
        if (!names.count(bone.parent) || !names.count(bone.child)) error("bone_joint", bone.child, "Bone references unknown joint");
        if (bone.child == "torso" || !parents.emplace(bone.child, bone.parent).second) error("bone_parent", bone.child, "Each non-root joint must have one parent");
    }
    for (const auto& name : names) {
        std::set<std::string> visited;
        std::string current = name;
        while (current != "torso") {
            if (!visited.insert(current).second) { error("bone_cycle", name, "Bone graph contains a cycle"); break; }
            const auto parent = parents.find(current);
            if (parent == parents.end()) { error("bone_root", name, "Joint is disconnected from torso"); break; }
            current = parent->second;
        }
    }
    std::vector<unsigned int> owners(count, 0);
    std::set<std::string> region_names;
    for (const auto& region : rig.regions) {
        if (region.name.empty() || !region_names.insert(region.name).second) error("region_name", region.name, "Region names must be unique and nonempty");
        if (!names.count(region.joint)) error("region_joint", region.name, "Region references unknown joint");
        if (region.pixels.size() != count) { error("mask_size", region.name, "Region size does not match dimensions"); continue; }
        for (std::size_t pixel = 0; pixel < count; ++pixel) {
            if (region.pixels[pixel] > 1) error("mask_value", region.name, "Region mask must be binary");
            if (region.pixels[pixel]) ++owners[pixel];
        }
    }
    for (std::size_t pixel = 0; pixel < count; ++pixel) {
        if (owners[pixel] > 1) { error("mask_overlap", "regions", "Visible pixels must have one owner"); break; }
    }
    for (std::size_t pixel = 0; pixel < count; ++pixel) {
        if ((owners[pixel] != 0) != (rig.silhouette[pixel] != 0)) { error("mask_coverage", "regions", "Region union must equal source silhouette"); break; }
    }
    std::vector<std::string> required_regions{"head", "torso"};
    for (const std::string side : {"left", "right"}) for (const std::string part : {"arm", "forearm", "glove", "leg", "shin", "foot"}) {
        required_regions.push_back(side + "_" + part);
    }
    for (const auto& name : required_regions) if (!region_names.count(name)) error("missing_region", name, "Required boxer region is missing");
    return result;
}
nlohmann::json diagnostics_json(const std::vector<Diagnostic>& diagnostics) {
    nlohmann::json document{{"valid", diagnostics.empty()}, {"diagnostics", nlohmann::json::array()}};
    for (const auto& diagnostic : diagnostics) document["diagnostics"].push_back({{"code", diagnostic.code},
        {"path", diagnostic.path}, {"message", diagnostic.message}});
    return document;
}
}  // namespace spratforge::rig