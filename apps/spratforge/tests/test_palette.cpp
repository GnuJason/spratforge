#include <cassert>

#include <spratforge/core/palette_core.hpp>

int main() {
    using namespace spratforge::core;

    const Palette nes = load_palette("nes");
    const Palette gb = load_palette("gb.gpl");
    const Palette strict = load_palette("strict");
    assert(nes.colors.size() == 8U);
    assert(gb.colors.size() == 4U);
    assert(strict.colors.empty());
    assert((nearest_color(Color{0, 20, 120}, nes) == Color{0, 30, 116}));

    const std::vector<std::uint8_t> source = {0, 20, 120, 255, 255, 255, 255, 255,
                                               90, 90, 90, 255, 10, 10, 10, 0};
    assert(apply_palette(source, 2, 2, strict, PaletteMode::Strict) == source);
    const auto nes_rgba = apply_palette(source, 2, 2, nes, PaletteMode::NES);
    assert(nes_rgba[0] == 0U && nes_rgba[1] == 30U && nes_rgba[2] == 116U);
    assert(nes_rgba[3] == 255U && nes_rgba[15] == 0U);
    const auto gb_rgba = apply_palette(source, 2, 2, gb, PaletteMode::GB);
    assert(gb_rgba[0] == 15U && gb_rgba[1] == 56U && gb_rgba[2] == 15U);

    bool found_dither_pattern = false;
    for (int gray_value = 0; gray_value <= 255 && !found_dither_pattern; ++gray_value) {
        const auto channel = static_cast<std::uint8_t>(gray_value);
        const std::vector<std::uint8_t> gray = {channel, channel, channel, 255, channel, channel, channel, 255,
                                                 channel, channel, channel, 255, channel, channel, channel, 255};
        const auto dithered = apply_palette(gray, 2, 2, nes, PaletteMode::Dither);
        found_dither_pattern = dithered[0] != dithered[8] || dithered[1] != dithered[9] || dithered[2] != dithered[10];
    }
    assert(found_dither_pattern);

    std::vector<Frame> frames = {
        {.width = 1, .height = 1, .rgba = {0, 20, 120, 255}},
        {.width = 1, .height = 1, .rgba = {5, 25, 115, 255}},
    };
    enforce_consistency(frames, nes);
    assert(frames[0].rgba == frames[1].rgba);
    assert(frames[0].rgba == std::vector<std::uint8_t>({0, 30, 116, 255}));

    Frame frame{.width = 1, .height = 1, .rgba = {0, 20, 120, 255}};
    assert(apply_palette_mode("nes", frame));
    assert(frame.rgba == std::vector<std::uint8_t>({0, 30, 116, 255}));
    assert(!apply_palette_mode("unknown", frame));
    return 0;
}