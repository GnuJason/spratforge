#include "pose_interp.hpp"

#include <algorithm>
#include <cmath>

namespace spratgen {

PoseSkeleton PoseInterpolator::apply(const Skeleton& base, const Skeleton& target, float t) {
    const float clampedT = std::clamp(t, 0.0f, 1.0f);

    PoseSkeleton posed;
    posed.head = lerpJoint(base.head, target.head, clampedT);
    posed.torso = lerpJoint(base.torso, target.torso, clampedT);
    posed.left_arm = lerpJoint(base.left_arm, target.left_arm, clampedT);
    posed.right_arm = lerpJoint(base.right_arm, target.right_arm, clampedT);
    posed.left_leg = lerpJoint(base.left_leg, target.left_leg, clampedT);
    posed.right_leg = lerpJoint(base.right_leg, target.right_leg, clampedT);
    return posed;
}

float PoseInterpolator::applyCurve(float t, const std::string& curve) {
    const float clampedT = std::clamp(t, 0.0f, 1.0f);

    if (curve == "easeIn") {
        return clampedT * clampedT;
    }

    if (curve == "easeOut") {
        return 1.0f - (1.0f - clampedT) * (1.0f - clampedT);
    }

    if (curve == "easeInOut") {
        return clampedT < 0.5f
            ? 2.0f * clampedT * clampedT
            : 1.0f - static_cast<float>(std::pow(-2.0f * clampedT + 2.0f, 2.0f)) / 2.0f;
    }

    if (curve == "cubic") {
        return clampedT * clampedT * (3.0f - 2.0f * clampedT);
    }

    return clampedT;
}

PoseJoint PoseInterpolator::lerpJoint(const Joint& a, const Joint& b, float t) {
    const float x = static_cast<float>(a.x) + static_cast<float>(b.x - a.x) * t;
    const float y = static_cast<float>(a.y) + static_cast<float>(b.y - a.y) * t;
    return PoseJoint{static_cast<int>(x), static_cast<int>(y)};
}

}  // namespace spratgen