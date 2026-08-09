#include "anchor/anchor_core.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <tuple>

#include <json/json.hpp>

namespace spratforge::anchor {
namespace {
bool valid(const core::Frame& frame) {
    return frame.width > 0 && frame.height > 0 &&
           frame.rgba.size() == static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 4U;
}
}  // namespace

std::vector<std::uint8_t> extract_silhouette(const core::Frame& frame) {
    if (!valid(frame)) return {};
    std::vector<std::uint8_t> result(static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height));
    for (std::size_t index = 0; index < result.size(); ++index) result[index] = frame.rgba[index * 4U + 3U] == 0U ? 0U : 1U;
    return result;
}
std::vector<core::Color> extract_palette(const core::Frame& frame) {
    if (!valid(frame)) return {};
    std::vector<core::Color> colors;
    for (std::size_t offset = 0; offset < frame.rgba.size(); offset += 4U) {
        if (frame.rgba[offset + 3U] == 0U) continue;
        const core::Color color{frame.rgba[offset], frame.rgba[offset + 1U], frame.rgba[offset + 2U]};
        if (std::find(colors.begin(), colors.end(), color) == colors.end()) colors.push_back(color);
    }
    std::sort(colors.begin(), colors.end(), [](const auto& left, const auto& right) {
        return std::tie(left.r, left.g, left.b) < std::tie(right.r, right.g, right.b);
    });
    return colors;
}
BoundingBox normalize_bounding_box(const core::Frame& frame) {
    const auto silhouette = extract_silhouette(frame);
    if (silhouette.empty()) return {};
    int left = frame.width, top = frame.height, right = -1, bottom = -1;
    for (int y = 0; y < frame.height; ++y) for (int x = 0; x < frame.width; ++x) {
        if (silhouette[static_cast<std::size_t>(y) * frame.width + x] == 0U) continue;
        left = std::min(left, x); top = std::min(top, y); right = std::max(right, x); bottom = std::max(bottom, y);
    }
    return right < left ? BoundingBox{} : BoundingBox{left, top, right - left + 1, bottom - top + 1};
}
Pivot detect_pivot(const core::Frame& frame, const BoundingBox& bounds) {
    if (!valid(frame) || bounds.width <= 0 || bounds.height <= 0) return {};
    return {bounds.x + (bounds.width - 1) / 2, bounds.y + bounds.height - 1};
}
AnchorData extract_anchor(const core::Frame& frame) {
    const auto bounds = normalize_bounding_box(frame);
    return {frame.width, frame.height, bounds, detect_pivot(frame, bounds), extract_silhouette(frame), extract_palette(frame)};
}
AnchorData load_anchor_profile(const std::string& path) {
    std::ifstream input(path); if (!input) throw std::runtime_error("Unable to read anchor profile: " + path);
    nlohmann::json json; input >> json; AnchorData anchor;
    anchor.width = json.at("width"); anchor.height = json.at("height");
    const auto& bounds = json.at("bounds"); anchor.bounds = {bounds.at("x"), bounds.at("y"), bounds.at("width"), bounds.at("height")};
    const auto& pivot = json.at("pivot"); anchor.pivot = {pivot.at("x"), pivot.at("y")};
    anchor.silhouette = json.value("silhouette", std::vector<std::uint8_t>{});
    for (const auto& color : json.value("palette", nlohmann::json::array())) anchor.palette.push_back({color.at(0), color.at(1), color.at(2)});
    return anchor;
}
void save_anchor_profile(const std::string& path, const AnchorData& anchor) {
    nlohmann::json json{{"width", anchor.width}, {"height", anchor.height},
        {"bounds", {{"x", anchor.bounds.x}, {"y", anchor.bounds.y}, {"width", anchor.bounds.width}, {"height", anchor.bounds.height}}},
        {"pivot", {{"x", anchor.pivot.x}, {"y", anchor.pivot.y}}},
        {"silhouette", anchor.silhouette}, {"palette", nlohmann::json::array()}};
    for (const auto& color : anchor.palette) json["palette"].push_back({color.r, color.g, color.b});
    const std::filesystem::path output(path); if (output.has_parent_path()) std::filesystem::create_directories(output.parent_path());
    std::ofstream file(path); if (!file) throw std::runtime_error("Unable to write anchor profile: " + path);
    file << json.dump(2) << '\n';
}
}  // namespace spratforge::anchor
