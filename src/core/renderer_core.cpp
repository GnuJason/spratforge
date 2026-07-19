#include "core/renderer_core.hpp"

namespace spratforge::core {

bool render_single_frame(std::string_view output_path, std::string& error) {
    if (output_path.empty()) {
        error = "An output path is required";
        return false;
    }
    // TODO: Render and encode a single pixel-art frame in a later phase.
    return true;
}

bool render_profile(const profiles::AnimationProfile& profile, std::string_view output_directory,
                    std::string& error) {
    if (profile.frame_count <= 0 || output_directory.empty()) {
        error = "A valid profile and output directory are required";
        return false;
    }
    // TODO: Render the profile's frames and write them to the output directory.
    return true;
}

bool render_atlas(const atlas::AtlasLayout& layout, std::string_view output_path, std::string& error) {
    if (layout.frame_slots <= 0 || output_path.empty()) {
        error = "A valid atlas layout and output path are required";
        return false;
    }
    // TODO: Render frame content into the requested atlas layout.
    return true;
}

}  // namespace spratforge::core