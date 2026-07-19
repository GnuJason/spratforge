#include <cassert>

#include "core/palette_core.hpp"

int main() {
    // TODO: Verify actual pixel mapping for each palette mode.
    spratforge::core::Frame frame;
    assert(spratforge::core::apply_palette_mode("nes", frame));
    assert(spratforge::core::apply_palette_mode("gb", frame));
    assert(spratforge::core::apply_palette_mode("dither", frame));
    assert(!spratforge::core::apply_palette_mode("unknown", frame));
    return 0;
}