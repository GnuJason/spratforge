#include <spratforge/rig/pixel_rig.hpp>

#include <algorithm>
#include <stdexcept>

namespace spratforge::rig {
void segment_regions(RigDefinition& rig) {
    if (rig.width < 1 || rig.height < 1 || rig.width > 4096 || rig.height > 4096 ||
        rig.silhouette.size() != static_cast<std::size_t>(rig.width) * rig.height) {
        throw std::invalid_argument("Invalid segmentation dimensions");
    }
    int left = rig.width, right = -1, top = rig.height, bottom = -1;
    for (int y = 0; y < rig.height; ++y) for (int x = 0; x < rig.width; ++x) {
        if (!rig.silhouette[static_cast<std::size_t>(y) * rig.width + x]) continue;
        left = std::min(left, x); right = std::max(right, x);
        top = std::min(top, y); bottom = std::max(bottom, y);
    }
    if (right < left) throw std::invalid_argument("Cannot segment an empty silhouette");
    rig.regions.clear();
    const auto add = [&](const std::string& name, const std::string& joint, int order) {
        rig.regions.push_back({name, joint, order, std::vector<std::uint8_t>(rig.silhouette.size(), 0)});
    };
    add("head", "head", 30); add("torso", "torso", 20);
    for (const std::string side : {"left", "right"}) {
        const int order = side == "left" ? 0 : 40;
        add(side + "_arm", side + "_shoulder", order + 2);
        add(side + "_forearm", side + "_elbow", order + 3);
        add(side + "_glove", side + "_wrist", order + 4);
        add(side + "_leg", side + "_hip", order);
        add(side + "_shin", side + "_knee", order + 1);
        add(side + "_foot", side + "_ankle", order + 2);
    }
    const auto joint = [&](const std::string& name) -> const Joint& {
        const auto found = std::find_if(rig.joints.begin(), rig.joints.end(), [&](const auto& item) { return item.name == name; });
        if (found == rig.joints.end()) throw std::invalid_argument("Missing segmentation joint: " + name);
        return *found;
    };
    const int center = joint("torso").x;
    const int width = right - left + 1, height = bottom - top + 1;
    for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) {
        const auto offset = static_cast<std::size_t>(y) * rig.width + x;
        if (!rig.silhouette[offset]) continue;
        const int vertical = (y - top) * 100 / height;
        const bool is_left = x <= center;
        const std::string side = is_left ? "left" : "right";
        const auto& wrist = joint(side + "_wrist");
        std::string region;
        if (vertical < 20) region = "head";
        else if (vertical >= 90) region = side + "_foot";
        else if (vertical >= 78) region = side + "_shin";
        else if (vertical >= 65) region = side + "_leg";
        else if (std::abs(x - center) <= std::max(1, width / 6)) region = "torso";
        else if (std::abs(x - wrist.x) + std::abs(y - wrist.y) <= std::max(1, width / 8)) region = side + "_glove";
        else region = side + (vertical < 40 ? "_arm" : "_forearm");
        const auto found = std::find_if(rig.regions.begin(), rig.regions.end(), [&](const auto& item) { return item.name == region; });
        found->pixels[offset] = 1;
    }
}
}  // namespace spratforge::rig