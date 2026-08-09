#pragma once

#include <string>

#include "skeleton.hpp"

namespace spratgen {

struct PoseJoint {
    int x = 0;
    int y = 0;
};

struct PoseSkeleton {
    PoseJoint head;
    PoseJoint torso;
    PoseJoint left_arm;
    PoseJoint right_arm;
    PoseJoint left_leg;
    PoseJoint right_leg;
};

class PoseInterpolator {
public:
    PoseSkeleton apply(const Skeleton& base, const Skeleton& target, float t);
    float applyCurve(float t, const std::string& curve);

private:
    PoseJoint lerpJoint(const Joint& a, const Joint& b, float t);
};

}  // namespace spratgen