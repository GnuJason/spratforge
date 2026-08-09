#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <spratforge/core/palette_core.hpp>

namespace spratforge::ai {

struct MotionVector {
    int dx = 0;
    int dy = 0;
};

MotionVector parse_motion(const std::string& input);
MotionVector quantize(const MotionVector& motion);
void apply_motion(core::Frame& frame, const MotionVector& motion);
void apply_motion_sequence(std::vector<core::Frame>& frames, const MotionVector& motion);

}  // namespace spratforge::ai