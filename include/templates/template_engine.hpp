#pragma once

#include <string>
#include <vector>

#include "ai/ai_motion_core.hpp"

namespace spratforge::templates {

struct SkeletonMotionCurve {
    ai::MotionVector per_frame;
};
struct TimingRule {
    int frame_count = 1;
    int fps = 8;
};
struct PivotRule {
    int offset_x = 0;
    int offset_y = 0;
};
struct AnimationTemplate {
    std::string name;
    int frame_count = 1;
    int fps = 8;
    ai::MotionVector motion;
    int pivot_offset_x = 0;
    int pivot_offset_y = 0;
    SkeletonMotionCurve motion_curve;
    TimingRule timing;
    PivotRule pivot_rule;
};

AnimationTemplate load_template(const std::string& path);
std::vector<AnimationTemplate> load_template_registry(const std::string& directory);
const AnimationTemplate* find_template(const std::vector<AnimationTemplate>& registry, const std::string& name);

}  // namespace spratforge::templates
