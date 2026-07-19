#include "ai/ai_motion_core.hpp"

namespace spratforge::ai {

MotionVector quantize_motion(MotionVector motion, int pixels_per_unit) {
    (void)pixels_per_unit;
    // TODO: Quantize input motion against the selected pixel grid.
    return motion;
}

}  // namespace spratforge::ai