#include <cassert>
#include <string>

#include "core/renderer_core.hpp"

int main() {
    // TODO: Compare generated frame_000.png byte-for-byte or pixel-for-pixel with a golden PNG.
    std::string error;
    assert(spratforge::core::render_single_frame("frame_000.png", error));
    return 0;
}