#include "core/palette_core.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>

namespace spratforge::core {
namespace {

constexpr int bayer4x4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

std::string normalize_name(std::string name) {
    if (name.size() < 4U || name.substr(name.size() - 4U) != ".gpl") name += ".gpl";
    return name;
}

bool valid_frame(const Frame& frame) {
    return frame.width > 0 && frame.height > 0 && frame.rgba.size() ==
        static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 4U;
}

std::uint8_t clamp_byte(int value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

Palette palette_for_mode(PaletteMode mode) {
    switch (mode) {
    case PaletteMode::GB:
        return load_palette("gb");
    case PaletteMode::NES:
    case PaletteMode::Dither:
        return load_palette("nes");
    case PaletteMode::Strict:
        return load_palette("strict");
    }
    return {};
}

}  // namespace

Palette load_palette(const std::string& name) {
    Palette palette{.name = name};
    const std::string filename = normalize_name(name);
    std::ifstream input(std::string(SPRATFORGE_PALETTE_DIR) + "/" + filename);
    if (!input) return palette;

    std::string line;
    while (std::getline(input, line)) {
        std::istringstream stream(line);
        int red = 0;
        int green = 0;
        int blue = 0;
        if (!(stream >> red >> green >> blue)) continue;
        if (red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 || blue > 255) continue;
        palette.colors.push_back({static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green),
                                  static_cast<std::uint8_t>(blue)});
    }
    return palette;
}

Color nearest_color(const Color& input, const Palette& palette) {
    if (palette.colors.empty()) return input;
    Color closest = palette.colors.front();
    unsigned int closest_distance = std::numeric_limits<unsigned int>::max();
    for (const Color candidate : palette.colors) {
        const int delta_red = static_cast<int>(input.r) - candidate.r;
        const int delta_green = static_cast<int>(input.g) - candidate.g;
        const int delta_blue = static_cast<int>(input.b) - candidate.b;
        const unsigned int distance = static_cast<unsigned int>(delta_red * delta_red + delta_green * delta_green +
                                                                 delta_blue * delta_blue);
        if (distance < closest_distance) {
            closest = candidate;
            closest_distance = distance;
        }
    }
    return closest;
}

std::vector<std::uint8_t> apply_palette(const std::vector<std::uint8_t>& rgba, int width, int height,
                                        const Palette& palette, PaletteMode mode) {
    if (width <= 0 || height <= 0 || rgba.size() !=
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U) {
        return {};
    }

    std::vector<std::uint8_t> output = rgba;
    if (mode == PaletteMode::Strict) return output;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4U;
            if (output[index + 3U] == 0U) continue;
            Color input{output[index], output[index + 1U], output[index + 2U]};
            if (mode == PaletteMode::Dither) {
                const int adjustment = (bayer4x4[y % 4][x % 4] - 8) * 4;
                input = {clamp_byte(static_cast<int>(input.r) + adjustment),
                         clamp_byte(static_cast<int>(input.g) + adjustment),
                         clamp_byte(static_cast<int>(input.b) + adjustment)};
            }
            const Color mapped = nearest_color(input, palette);
            output[index] = mapped.r;
            output[index + 1U] = mapped.g;
            output[index + 2U] = mapped.b;
        }
    }
    return output;
}

void enforce_consistency(std::vector<Frame>& frames, const Palette& palette) {
    if (frames.empty() || palette.colors.empty() || !std::all_of(frames.begin(), frames.end(), valid_frame)) return;
    const int width = frames.front().width;
    const int height = frames.front().height;
    if (!std::all_of(frames.begin(), frames.end(), [width, height](const Frame& frame) {
            return frame.width == width && frame.height == height;
        })) return;

    for (std::size_t index = 0; index < frames.front().rgba.size(); index += 4U) {
            int total_red = 0;
            int total_green = 0;
            int total_blue = 0;
        int samples = 0;
        for (const Frame& frame : frames) {
            if (frame.rgba[index + 3U] == 0U) continue;
                total_red += frame.rgba[index];
                total_green += frame.rgba[index + 1U];
                total_blue += frame.rgba[index + 2U];
            ++samples;
        }
        if (samples == 0) continue;
            const Color average{static_cast<std::uint8_t>(total_red / samples),
                                static_cast<std::uint8_t>(total_green / samples),
                                static_cast<std::uint8_t>(total_blue / samples)};
            const Color mapped = nearest_color(average, palette);
        for (Frame& frame : frames) {
            if (frame.rgba[index + 3U] == 0U) continue;
            frame.rgba[index] = mapped.r;
            frame.rgba[index + 1U] = mapped.g;
            frame.rgba[index + 2U] = mapped.b;
        }
    }
}

bool is_supported_palette_mode(std::string_view mode) {
    return mode == "nes" || mode == "gb" || mode == "dither" || mode == "strict";
}

bool apply_palette_mode(std::string_view mode, Frame& frame) {
    if (!is_supported_palette_mode(mode)) return false;
    if (!valid_frame(frame)) return false;
    const PaletteMode palette_mode = mode == "strict" ? PaletteMode::Strict : mode == "gb" ? PaletteMode::GB
        : mode == "dither" ? PaletteMode::Dither : PaletteMode::NES;
    const Palette palette = palette_for_mode(palette_mode);
    if (palette_mode != PaletteMode::Strict && palette.colors.empty()) return false;
    frame.rgba = apply_palette(frame.rgba, frame.width, frame.height, palette, palette_mode);
    return !frame.rgba.empty();
}

}  // namespace spratforge::core