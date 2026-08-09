#pragma once

#include "core/palette_core.hpp"

namespace spratforge::core {

void pixel_snap(Frame& frame);
void quantize_palette(Frame& frame, const Palette& palette);
void cleanup_outline(Frame& frame);
void remove_antialiasing(Frame& frame);
void normalize_shading(Frame& frame);
void normalize_pixels(Frame& frame, const Palette& palette);

}  // namespace spratforge::core
