#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace spratgen {

struct Color {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 255;
};

struct Palette {
    Color base;
    Color accent;
    Color gloves;
    Color shorts;
    Color skin;
};

struct Image {
    int width = 0;
    int height = 0;
    std::vector<Color> pixels;
};

class PaletteManager {
public:
    std::vector<Color> extractFromImage(const Image& image) const;
    std::vector<Color> quantize(const std::vector<Color>& colors, std::size_t maxColors) const;
};

Palette makeBoxerPalette();

}  // namespace spratgen