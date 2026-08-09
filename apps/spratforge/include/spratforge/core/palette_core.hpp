#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace spratforge::core {

struct Frame {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    bool operator==(const Color&) const = default;
};

struct Palette {
    std::string name;
    std::vector<Color> colors;
};

enum class PaletteMode {
    Strict,
    NES,
    GB,
    Dither,
};

Palette load_palette(const std::string& name);
Color nearest_color(const Color& input, const Palette& palette);
std::vector<std::uint8_t> apply_palette(const std::vector<std::uint8_t>& rgba, int width, int height,
                                        const Palette& palette, PaletteMode mode);
void enforce_consistency(std::vector<Frame>& frames, const Palette& palette);

bool apply_palette_mode(std::string_view mode, Frame& frame);
bool is_supported_palette_mode(std::string_view mode);

}  // namespace spratforge::core