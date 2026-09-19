#include <spratforge/motion/pose.hpp>

#include <algorithm>
#include <array>
#include <iostream>
#include <queue>
#include <set>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::array<std::uint8_t, 4> pixel(const spratforge::motion::RenderedPose& frame, int x, int y) {
    const auto offset = (static_cast<std::size_t>(y - frame.origin_y) * frame.frame.width + x - frame.origin_x) * 4;
    return {frame.frame.rgba.at(offset), frame.frame.rgba.at(offset + 1), frame.frame.rgba.at(offset + 2), frame.frame.rgba.at(offset + 3)};
}
template<class Action> void rejects(Action action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, "Invalid pose should fail");
}
void connected(const spratforge::motion::RenderedPose& pose) {
    const auto& frame = pose.frame;
    std::set<std::size_t> visible;
    for (std::size_t offset = 3; offset < frame.rgba.size(); offset += 4) if (frame.rgba[offset]) visible.insert(offset / 4);
    check(!visible.empty(), "Rendered region disappeared");
    std::queue<std::size_t> pending;
    pending.push(*visible.begin()); visible.erase(visible.begin());
    while (!pending.empty()) {
        const auto current = pending.front(); pending.pop();
        const int x = static_cast<int>(current % frame.width), y = static_cast<int>(current / frame.width);
        for (int delta_y = -1; delta_y <= 1; ++delta_y) for (int delta_x = -1; delta_x <= 1; ++delta_x) {
            const int next_x = x + delta_x, next_y = y + delta_y;
            if (next_x < 0 || next_x >= frame.width || next_y < 0 || next_y >= frame.height) continue;
            const auto next = static_cast<std::size_t>(next_y) * frame.width + next_x;
            if (visible.erase(next)) pending.push(next);
        }
    }
    check(visible.empty(), "Articulated motion detached a connected region");
}
}
int main() {
    try {
        spratforge::core::Frame source{9, 9, std::vector<std::uint8_t>(9 * 9 * 4, 0)};
        for (int y = 3; y <= 5; ++y) for (int x = 3; x <= 5; ++x) {
            const auto offset = (y * 9 + x) * 4;
            source.rgba[offset] = static_cast<std::uint8_t>(x * 30);
            source.rgba[offset + 1] = static_cast<std::uint8_t>(y * 30);
            source.rgba[offset + 3] = 255;
        }
        auto rig = spratforge::rig::build_rig(source);
        rig = spratforge::rig::apply_overrides(rig, {{"joints", {{{"name", "torso"}, {"x", 4}, {"y", 4}},
                                                                   {{"name", "left_shoulder"}, {"x", 4}, {"y", 4}}}}});
        for (auto& region : rig.regions) std::fill(region.pixels.begin(), region.pixels.end(), 0);
        rig.regions.front().joint = "left_shoulder";
        rig.regions.front().pixels = rig.silhouette;
        const auto neutral = spratforge::motion::render_pose(source, rig, {});
        check(neutral.frame.rgba == source.rgba, "Neutral pose must preserve exact source pixels");
        const spratforge::motion::PoseDefinition pose{{{"left_shoulder", 0, 0, 90}}};
        const auto rotated = spratforge::motion::render_pose(source, rig, pose);
        for (int y = 3; y <= 5; ++y) for (int x = 3; x <= 5; ++x) {
            check(pixel(rotated, 8 - y, x) == pixel(neutral, x, y), "90-degree golden limb rotation mismatch");
        }
        check(rotated.frame.rgba == spratforge::motion::render_pose(source, rig, pose).frame.rgba, "Rendering must be deterministic");
        for (int angle : {-90, -45, -15, 15, 30, 45, 180}) {
            const auto angled = spratforge::motion::render_pose(source, rig, {{{"left_shoulder", 0, 0, angle}}});
            connected(angled);
            for (std::size_t offset = 3; offset < angled.frame.rgba.size(); offset += 4) {
                check(angled.frame.rgba[offset] == 0 || angled.frame.rgba[offset] == 255, "Rotation introduced fractional alpha");
            }
        }
        const auto shifted = spratforge::motion::render_pose(source, rig, {{{"torso", -10, 0}}});
        check(pixel(shifted, -7, 3) == pixel(neutral, 3, 3), "Parent translation must move child without clipping");
        const auto scaled = spratforge::motion::render_pose(source, rig, {{{"left_shoulder", 0, 0, 0, 200, 100}}});
        check(pixel(scaled, 2, 3) == pixel(neutral, 3, 3), "Integer stretch mismatch");
        for (int y = 3; y <= 5; ++y) for (int x = 2; x <= 6; ++x) check(pixel(scaled, x, y)[3] == 255, "Stretch introduced holes");
        for (std::size_t offset = 3; offset < scaled.frame.rgba.size(); offset += 4) check(scaled.frame.rgba[offset] == 0 || scaled.frame.rgba[offset] == 255, "Subpixel alpha artifact");
        std::vector<spratforge::motion::RenderedPose> aligned{neutral, shifted, rotated};
        spratforge::motion::align_frames(aligned);
        for (const auto& frame : aligned) check(frame.frame.width == aligned.front().frame.width &&
            frame.pivot.x == aligned.front().pivot.x && frame.pivot.y == aligned.front().pivot.y, "Unstable frame pivot");
        rejects([&] { spratforge::motion::render_pose(source, rig, {{{"left_shoulder", 0, 0, 13}}}); });
        rejects([&] { spratforge::motion::render_pose(source, rig, {{{"unknown", 1, 0}}}); });
        rejects([&] { spratforge::motion::render_pose(source, rig, {{{"head", 0, 0, 0, 0, 100}}}); });
        rejects([&] { spratforge::motion::render_pose(source, rig, {{{"head", 0, 0}, {"head", 1, 0}}}); });
        for (const std::string name : {"idle", "walk", "jab", "block", "hit", "ko", "run", "victory"}) {
            const auto clip = spratforge::motion::default_motion(name);
            check(clip.frame_count > 1, "Default clip must have multiple frames");
            for (int frame = 0; frame < clip.frame_count; ++frame) (void)spratforge::motion::sample_pose(clip, frame);
        }
        auto clip = spratforge::motion::default_motion("jab");
        clip.keyframes[1].time = 0;
        rejects([&] { spratforge::motion::sample_pose(clip, 1); });
        rig.regions.front().pixels.assign(rig.silhouette.size(), 0);
        rig.regions[1].pixels.assign(rig.silhouette.size(), 0);
        rig.regions.front().pixels[4 * 9 + 3] = 1;
        rig.regions[1].pixels = rig.silhouette;
        rig.regions[1].pixels[4 * 9 + 3] = 0;
        rig.regions.front().z_order = 100;
        const auto layered = spratforge::motion::render_pose(source, rig, {{{"left_shoulder", 2, 0}}});
        check(pixel(layered, 5, 4) == pixel(neutral, 3, 4), "Foreground layer did not win overlap");
        check(pixel(layered, 3, 4)[3] == 255, "One-pixel occlusion seam was not repaired");
        const auto extended = spratforge::motion::render_pose(source, rig, {{{"left_shoulder", 10, 0}}});
        connected(extended);
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}