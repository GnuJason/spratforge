#include "core/pixel_normalizer.hpp"

#include <algorithm>
#include <array>

namespace spratforge::core {
namespace {
bool valid(const Frame& frame) {
    return frame.width > 0 && frame.height > 0 &&
           frame.rgba.size() == static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 4U;
}
std::size_t offset(const Frame& frame, int x, int y) { return (static_cast<std::size_t>(y) * frame.width + x) * 4U; }
}  // namespace
void pixel_snap(Frame& frame) {
    if (!valid(frame)) return;
    for (std::size_t index = 0; index < frame.rgba.size(); index += 4U) if (frame.rgba[index + 3U] < 128U) frame.rgba[index + 3U] = 0U;
}
void quantize_palette(Frame& frame, const Palette& palette) {
    if (!valid(frame) || palette.colors.empty()) return;
    for (std::size_t index = 0; index < frame.rgba.size(); index += 4U) {
        if (frame.rgba[index + 3U] == 0U) continue;
        const auto color = nearest_color({frame.rgba[index], frame.rgba[index + 1U], frame.rgba[index + 2U]}, palette);
        frame.rgba[index] = color.r; frame.rgba[index + 1U] = color.g; frame.rgba[index + 2U] = color.b;
    }
}
void cleanup_outline(Frame& frame) {
    if (!valid(frame)) return;
    const auto original = frame.rgba;
    for (int y = 0; y < frame.height; ++y) for (int x = 0; x < frame.width; ++x) {
        const auto current = offset(frame, x, y);
        if (original[current + 3U] == 0U) continue;
        int neighbors = 0;
        for (const auto [dx, dy] : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
            const int nx = x + dx, ny = y + dy;
            if (nx >= 0 && nx < frame.width && ny >= 0 && ny < frame.height && original[offset(frame, nx, ny) + 3U] != 0U) ++neighbors;
        }
        if (neighbors == 0) frame.rgba[current + 3U] = 0U;
    }
}
void remove_antialiasing(Frame& frame) { pixel_snap(frame); }
void normalize_shading(Frame& frame) {
    if (!valid(frame)) return;
    for (std::size_t index = 0; index < frame.rgba.size(); index += 4U) {
        if (frame.rgba[index + 3U] == 0U) continue;
        for (std::size_t channel = 0; channel < 3U; ++channel) frame.rgba[index + channel] = static_cast<std::uint8_t>((frame.rgba[index + channel] / 16U) * 16U);
    }
}
void normalize_pixels(Frame& frame, const Palette& palette) {
    pixel_snap(frame); cleanup_outline(frame); remove_antialiasing(frame); normalize_shading(frame); quantize_palette(frame, palette);
}
}  // namespace spratforge::core
