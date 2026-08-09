#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/palette_core.hpp"

namespace spratforge::anchor {

struct BoundingBox { int x = 0; int y = 0; int width = 0; int height = 0; };
struct Pivot { int x = 0; int y = 0; };
struct AnchorData {
    int width = 0;
    int height = 0;
    BoundingBox bounds;
    Pivot pivot;
    std::vector<std::uint8_t> silhouette;
    std::vector<core::Color> palette;
};

std::vector<std::uint8_t> extract_silhouette(const core::Frame& frame);
std::vector<core::Color> extract_palette(const core::Frame& frame);
Pivot detect_pivot(const core::Frame& frame, const BoundingBox& bounds);
BoundingBox normalize_bounding_box(const core::Frame& frame);
AnchorData extract_anchor(const core::Frame& frame);
AnchorData load_anchor_profile(const std::string& path);
void save_anchor_profile(const std::string& path, const AnchorData& anchor);

}  // namespace spratforge::anchor
