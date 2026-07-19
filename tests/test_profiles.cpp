#include <cassert>
#include <stdexcept>

#include "core/renderer_core.hpp"
#include "profiles/profile_loader.hpp"

int main() {
    using namespace spratforge;

    const profiles::AnimationProfile profile = profiles::load_profile("idle_6");
    assert(profile.name == "idle_6");
    assert(profile.frame_count == 6);
    assert(profile.interpolation == "ease_in_out");
    assert(profile.motion == "idle");
    assert(profile.palette_override == "nes");
    assert(profiles::is_valid_interpolation("linear"));
    assert(!profiles::is_valid_interpolation("spline"));
    assert(profiles::is_valid_motion("jab"));
    assert(!profiles::is_valid_motion("spin"));

    bool threw_for_missing_profile = false;
    try {
        (void)profiles::load_profile("missing_profile");
    } catch (const std::runtime_error&) {
        threw_for_missing_profile = true;
    }
    assert(threw_for_missing_profile);

    std::vector<core::Frame> linear_frames = {
        {.width = 1, .height = 1, .rgba = {0, 0, 0, 255}},
        {.width = 1, .height = 1, .rgba = {0, 0, 0, 255}},
        {.width = 1, .height = 1, .rgba = {200, 0, 0, 255}},
    };
    profiles::apply_interpolation(linear_frames, {.interpolation = "linear"});
    assert(linear_frames[1].rgba[0] == 100U);

    std::vector<core::Frame> eased_frames = {
        {.width = 1, .height = 1, .rgba = {0, 0, 0, 255}},
        {.width = 1, .height = 1, .rgba = {0, 0, 0, 255}},
        {.width = 1, .height = 1, .rgba = {0, 0, 0, 255}},
        {.width = 1, .height = 1, .rgba = {255, 0, 0, 255}},
    };
    profiles::apply_interpolation(eased_frames, {.interpolation = "ease_in_out"});
    assert(eased_frames[1].rgba[0] == 66U);
    assert(eased_frames[2].rgba[0] == 188U);

    const core::Frame motion_source{.width = 4, .height = 1,
                                    .rgba = {255, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
    std::vector<core::Frame> subtle_frames(2, motion_source);
    profiles::apply_motion_hint(subtle_frames, {.motion = "subtle"});
    assert(subtle_frames[0].rgba[3] == 0U);
    assert(subtle_frames[1].rgba[7] == 255U);

    std::vector<core::Frame> idle_frames(2, motion_source);
    profiles::apply_motion_hint(idle_frames, {.motion = "idle"});
    assert(idle_frames[1].rgba[3] == 0U);

    std::vector<core::Frame> walk_frames(4, motion_source);
    profiles::apply_motion_hint(walk_frames, {.motion = "walk"});
    assert(walk_frames[1].rgba[7] == 255U);
    assert(walk_frames[3].rgba[3] == 0U);

    std::vector<core::Frame> jab_frames(3, motion_source);
    profiles::apply_motion_hint(jab_frames, {.motion = "jab"});
    assert(jab_frames[1].rgba[11] == 255U);
    assert(jab_frames[2].rgba[3] == 255U);

    const core::Frame source{
        .width = 2,
        .height = 2,
        .rgba = {0, 20, 120, 255, 0, 20, 120, 255, 0, 20, 120, 255, 0, 20, 120, 255},
    };
    const core::RenderOptions options{.grid_width = 1, .grid_height = 1, .palette_mode = "gb"};
    std::string error;
    const auto frames = core::render_profile_frames(source, profile, options, error);
    assert(frames.size() == 6U);
    assert(frames.front().width == 1);
    assert(frames.front().height == 1);
    assert(frames.front().rgba[0] == 0U && frames.front().rgba[1] == 30U && frames.front().rgba[2] == 116U);
    return 0;
}