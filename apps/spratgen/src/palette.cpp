#include "palette.hpp"

#include <algorithm>

namespace spratgen {

Palette makeBoxerPalette() {
    Palette palette;
    palette.base = Color{255, 0, 0, 255};
    palette.accent = Color{255, 255, 255, 255};
    palette.gloves = Color{255, 0, 0, 255};
    palette.shorts = Color{255, 255, 255, 255};
    palette.skin = Color{240, 200, 160, 255};
    return palette;
}

std::vector<Color> PaletteManager::extractFromImage(const Image& image) const {
    return image.pixels;
}

std::vector<Color> PaletteManager::quantize(const std::vector<Color>& colors, std::size_t maxColors) const {
    if (maxColors == 0 || colors.size() <= maxColors) {
        return colors;
    }

    return std::vector<Color>(colors.begin(), colors.begin() + static_cast<std::ptrdiff_t>(maxColors));
}

}  // namespace spratgen