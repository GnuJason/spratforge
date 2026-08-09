#include <cassert>

#include "core/pixel_normalizer.hpp"

int main() {
    const spratforge::core::Palette palette{.name = "test", .colors = {{0, 0, 0}, {255, 0, 0}}};
    spratforge::core::Frame first{.width = 2, .height = 1, .rgba = {250, 4, 4, 255, 1, 1, 1, 127}};
    auto second = first;
    spratforge::core::normalize_pixels(first, palette);
    spratforge::core::normalize_pixels(second, palette);
    assert(first.rgba == second.rgba);
    assert(first.rgba[3] == 0U && first.rgba[7] == 0U);
    return 0;
}
