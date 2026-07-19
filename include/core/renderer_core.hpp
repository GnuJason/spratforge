#pragma once

#include <string>
#include <string_view>

#include "atlas/atlas_core.hpp"
#include "profiles/profile_loader.hpp"

namespace spratforge::core {

bool render_single_frame(std::string_view output_path, std::string& error);
bool render_profile(const profiles::AnimationProfile& profile, std::string_view output_directory,
                    std::string& error);
bool render_atlas(const atlas::AtlasLayout& layout, std::string_view output_path, std::string& error);

}  // namespace spratforge::core