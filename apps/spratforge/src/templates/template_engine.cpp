#include <spratforge/templates/template_engine.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <json/json.hpp>

namespace spratforge::templates {
AnimationTemplate load_template(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Unable to read animation template: " + path);
    nlohmann::json json; input >> json;
    const auto& motion = json.value("motion", nlohmann::json::object());
    AnimationTemplate result{.name = json.at("name"), .frame_count = json.value("frame_count", 1), .fps = json.value("fps", 8),
                             .motion = {motion.value("x", 0), motion.value("y", 0)},
                             .pivot_offset_x = json.value("pivot_offset_x", 0), .pivot_offset_y = json.value("pivot_offset_y", 0)};
    if (result.name.empty() || result.frame_count < 1 || result.fps < 1) throw std::runtime_error("Invalid animation template: " + path);
    result.motion_curve.per_frame = result.motion;
    result.timing = {result.frame_count, result.fps};
    result.pivot_rule = {result.pivot_offset_x, result.pivot_offset_y};
    return result;
}

std::vector<AnimationTemplate> load_template_registry(const std::string& directory) {
    std::vector<AnimationTemplate> templates;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") templates.push_back(load_template(entry.path().string()));
    }
    std::sort(templates.begin(), templates.end(), [](const auto& left, const auto& right) { return left.name < right.name; });
    return templates;
}

const AnimationTemplate* find_template(const std::vector<AnimationTemplate>& registry, const std::string& name) {
    const auto iterator = std::find_if(registry.begin(), registry.end(), [&name](const auto& item) { return item.name == name; });
    return iterator == registry.end() ? nullptr : &*iterator;
}
}  // namespace spratforge::templates
