#include <cassert>

#include "profiles/profile_loader.hpp"

int main() {
    // TODO: Parse profiles/idle_6.json and validate all required profile fields.
    const auto profile = spratforge::profiles::load_profile("idle_6");
    assert(profile);
    assert(profile->frame_count == 6);
    assert(!profile->timing.empty());
    return 0;
}