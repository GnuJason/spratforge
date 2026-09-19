#pragma once

#include <string>
#include <vector>

#include <spratforge/rig/pixel_rig.hpp>

namespace spratforge::motion {

enum class Interpolation { linear, ease_in, ease_out, ease_in_out, smoothstep };
struct JointTransform {
    std::string joint;
    int dx = 0;
    int dy = 0;
    int rotation = 0;
    int scale_x = 100;
    int scale_y = 100;
};
struct PoseDefinition { std::vector<JointTransform> transforms; };
struct Keyframe {
    int time = 0;
    PoseDefinition pose;
    Interpolation interpolation = Interpolation::linear;
};
struct MotionClip {
    std::string name;
    int frame_count = 1;
    bool loop = false;
    std::vector<Keyframe> keyframes;
};
struct RenderedPose {
    core::Frame frame;
    anchor::Pivot pivot;
    int origin_x = 0;
    int origin_y = 0;
};
struct AnimationRecord {
    std::string name;
    int fps = 8;
    std::vector<RenderedPose> frames;
};

MotionClip default_motion(const std::string& name);
PoseDefinition sample_pose(const MotionClip& clip, int frame_index);
RenderedPose render_pose(const core::Frame& source, const rig::RigDefinition& rig, const PoseDefinition& pose);
void align_frames(std::vector<RenderedPose>& frames);

}  // namespace spratforge::motion