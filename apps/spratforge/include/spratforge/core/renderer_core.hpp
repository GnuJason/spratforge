#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <spratforge/ai/ai_motion_core.hpp>
#include <spratforge/core/palette_core.hpp>
#include <spratforge/profiles/profile_loader.hpp>

namespace spratforge::core {

struct RenderOptions {
    std::string input_path;
    int grid_width = 16;
    int grid_height = 16;
    std::optional<std::string> palette_mode;
};

bool load_frame_png(std::string_view input_path, Frame& frame, std::string& error);
bool save_frame_png(const Frame& frame, std::string_view output_path, std::string& error);
bool render_single_frame(const RenderOptions& options, std::string_view output_path, std::string& error);
bool render_profile(const profiles::AnimationProfile& profile, const RenderOptions& options,
                    std::string_view output_directory, std::string& error);

Frame downsample_nearest(const Frame& source, int width, int height);
std::vector<Frame> render_profile_frames(const profiles::AnimationProfile& profile, const RenderOptions& options,
                                         std::string& error);
std::vector<Frame> render_profile_frames(const Frame& source, const profiles::AnimationProfile& profile,
                                         const RenderOptions& options, std::string& error);
std::vector<Frame> render_ai_motion_frames(const Frame& source, const ai::MotionVector& motion, std::size_t frame_count,
                                           const RenderOptions& options, std::string& error);
bool render_ai_motion(const RenderOptions& options, const ai::MotionVector& motion, std::size_t frame_count,
                      std::string_view output_path, std::string& error);

}  // namespace spratforge::core