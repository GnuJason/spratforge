#include "core/palette_core.hpp"

namespace spratforge::core {

bool is_supported_palette_mode(std::string_view mode) {
    return mode == "nes" || mode == "gb" || mode == "dither";
}

bool apply_palette_mode(std::string_view mode, Frame& frame) {
    if (!is_supported_palette_mode(mode)) return false;
    (void)frame;
    // TODO: Map frame pixels to the selected palette in a later phase.
    return true;
}

}  // namespace spratforge::core