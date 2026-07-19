#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace spratforge::profiles {

struct AnimationProfile {
    std::string name;
    int frame_count = 0;
    std::string timing;
    std::map<std::string, std::string> metadata;
};

std::optional<AnimationProfile> load_profile(std::string_view name);

}  // namespace spratforge::profiles