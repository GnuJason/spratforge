#pragma once

#include <string>
#include <vector>

#include "silhouette.hpp"

namespace spratgen {

struct Joint {
    int x = 0;
    int y = 0;
    std::string name;

    Joint() = default;
    Joint(int jointX, int jointY) : x(jointX), y(jointY) {}
    Joint(std::string jointName, int jointX, int jointY)
        : x(jointX), y(jointY), name(std::move(jointName)) {}
};

struct Skeleton {
    Joint head;
    Joint torso;
    Joint left_arm;
    Joint right_arm;
    Joint left_leg;
    Joint right_leg;
    std::vector<Joint> joints;
};

class SkeletonBuilder {
public:
    Skeleton build(const Silhouette& silhouette);

private:
    Joint findHead(const Silhouette& silhouette);
    Joint findTorso(const Silhouette& silhouette);
    Joint findArm(const Silhouette& silhouette, bool left);
    Joint findLeg(const Silhouette& silhouette, bool left);
};

}  // namespace spratgen