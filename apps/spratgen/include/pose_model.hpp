#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "pose_interp.hpp"
#include "skeleton.hpp"

namespace spratgen {

struct Keyframe {
    PoseSkeleton pose;
    float t = 0.0f;
    std::string curve;
};

struct AnimationTemplate {
    std::string name;
    std::vector<Keyframe> keyframes;
    int frameCount = 0;
    bool loop = false;
};

class PoseModel {
public:
    PoseModel();

    const AnimationTemplate& getTemplate(const std::string& name) const;
    int getFrameCount(const std::string& name) const;
    PoseSkeleton getKeyframe(const std::string& name, int index) const;
    std::vector<std::string> getAnimationNames() const;

private:
    friend class Generator;

    std::unordered_map<std::string, AnimationTemplate> templates;

    void loadDefaults();
};

}  // namespace spratgen