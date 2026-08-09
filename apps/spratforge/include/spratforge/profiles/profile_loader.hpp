#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <spratforge/core/palette_core.hpp>

namespace spratforge::profiles {

struct AnimationProfile {
    std::string name;
    int frame_count = 0;
    std::string interpolation;
    std::string motion;
    std::string palette_override;
};

bool is_valid_interpolation(std::string_view interpolation);
bool is_valid_motion(std::string_view motion);
AnimationProfile load_profile(std::string_view profile_name);
void apply_interpolation(std::vector<core::Frame>& frames, const AnimationProfile& profile);
void apply_motion_hint(std::vector<core::Frame>& frames, const AnimationProfile& profile);

}  // namespace spratforge::profiles