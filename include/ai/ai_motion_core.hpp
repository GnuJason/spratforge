#pragma once

namespace spratforge::ai {

struct MotionVector {
    int x = 0;
    int y = 0;
};

MotionVector quantize_motion(MotionVector motion, int pixels_per_unit);

}  // namespace spratforge::ai