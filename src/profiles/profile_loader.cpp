#include "profiles/profile_loader.hpp"

namespace spratforge::profiles {

std::optional<AnimationProfile> load_profile(std::string_view name) {
    if (name != "idle_6") return std::nullopt;
    // TODO: Parse profiles/<name>.json rather than returning the example profile directly.
    return AnimationProfile{
        .name = "idle_6",
        .frame_count = 6,
        .timing = "default",
        .metadata = {{"source", "profiles/idle_6.json"}},
    };
}

}  // namespace spratforge::profiles