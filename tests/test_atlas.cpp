#include <cassert>

#include "atlas/atlas_core.hpp"

int main() {
    // TODO: Verify final sprite placement and emitted atlas metadata.
    const auto config = spratforge::atlas::parse_atlas_dimensions("5x5");
    assert(config);
    const auto layout = spratforge::atlas::build_atlas(*config);
    assert(layout.frame_slots == 25);
    return 0;
}