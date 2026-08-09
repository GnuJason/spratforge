#include "pose_model.hpp"

#include <algorithm>
#include <utility>

namespace spratgen {

namespace {

PoseSkeleton neutral_pose() {
    return {};
}

void offset_head(PoseSkeleton& pose, int dx, int dy) {
    pose.head.x += dx;
    pose.head.y += dy;
}

void offset_torso(PoseSkeleton& pose, int dx, int dy) {
    pose.torso.x += dx;
    pose.torso.y += dy;
}

void offset_left_arm(PoseSkeleton& pose, int dx, int dy) {
    pose.left_arm.x += dx;
    pose.left_arm.y += dy;
}

void offset_right_arm(PoseSkeleton& pose, int dx, int dy) {
    pose.right_arm.x += dx;
    pose.right_arm.y += dy;
}

void offset_arms(PoseSkeleton& pose, int dx, int dy) {
    offset_left_arm(pose, dx, dy);
    offset_right_arm(pose, dx, dy);
}

void offset_left_leg(PoseSkeleton& pose, int dx, int dy) {
    pose.left_leg.x += dx;
    pose.left_leg.y += dy;
}

void offset_right_leg(PoseSkeleton& pose, int dx, int dy) {
    pose.right_leg.x += dx;
    pose.right_leg.y += dy;
}

void offset_legs(PoseSkeleton& pose, int dx, int dy) {
    offset_left_leg(pose, dx, dy);
    offset_right_leg(pose, dx, dy);
}

PoseSkeleton mirror_pose(const PoseSkeleton& pose) {
    PoseSkeleton mirrored;
    mirrored.head = PoseJoint{-pose.head.x, pose.head.y};
    mirrored.torso = PoseJoint{-pose.torso.x, pose.torso.y};
    mirrored.left_arm = PoseJoint{-pose.right_arm.x, pose.right_arm.y};
    mirrored.right_arm = PoseJoint{-pose.left_arm.x, pose.left_arm.y};
    mirrored.left_leg = PoseJoint{-pose.right_leg.x, pose.right_leg.y};
    mirrored.right_leg = PoseJoint{-pose.left_leg.x, pose.left_leg.y};
    return mirrored;
}

AnimationTemplate make_template(std::string name, int frameCount, bool loop, std::vector<Keyframe> keyframes) {
    return AnimationTemplate{
        std::move(name),
        std::move(keyframes),
        frameCount,
        loop,
    };
}

}  // namespace

PoseModel::PoseModel() {
    loadDefaults();
}

const AnimationTemplate& PoseModel::getTemplate(const std::string& name) const {
    return templates.at(name);
}

int PoseModel::getFrameCount(const std::string& name) const {
    return templates.at(name).frameCount;
}

PoseSkeleton PoseModel::getKeyframe(const std::string& name, int index) const {
    return templates.at(name).keyframes.at(static_cast<std::size_t>(index)).pose;
}

std::vector<std::string> PoseModel::getAnimationNames() const {
    std::vector<std::string> names;
    names.reserve(templates.size());
    for (const auto& [name, animation] : templates) {
        static_cast<void>(animation);
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

void PoseModel::loadDefaults() {
    templates.clear();

    PoseSkeleton idleBreathe = neutral_pose();
    offset_torso(idleBreathe, 0, 2);
    offset_head(idleBreathe, 0, 1);
    offset_arms(idleBreathe, 0, 1);
    templates.emplace(
        "idle",
        make_template("idle", 25, true, {
            Keyframe{neutral_pose(), 0.0f, "easeInOut"},
            Keyframe{idleBreathe, 1.0f, "easeInOut"},
        }));

    PoseSkeleton walkA = neutral_pose();
    offset_left_leg(walkA, 0, 0);
    offset_right_leg(walkA, 0, 4);
    offset_left_arm(walkA, 0, 2);
    offset_right_arm(walkA, 0, -2);

    PoseSkeleton walkB = neutral_pose();
    offset_left_leg(walkB, 0, 4);
    offset_right_leg(walkB, 0, 0);
    offset_left_arm(walkB, 0, -2);
    offset_right_arm(walkB, 0, 2);
    templates.emplace(
        "walk",
        make_template("walk", 25, true, {
            Keyframe{walkA, 0.0f, "linear"},
            Keyframe{walkB, 0.33f, "linear"},
            Keyframe{mirror_pose(walkA), 0.66f, "linear"},
            Keyframe{mirror_pose(walkB), 1.0f, "linear"},
        }));

    PoseSkeleton jabStrike = neutral_pose();
    offset_right_arm(jabStrike, 12, -4);
    offset_torso(jabStrike, 3, 0);
    offset_head(jabStrike, 1, 0);
    templates.emplace(
        "jab",
        make_template("jab", 25, false, {
            Keyframe{neutral_pose(), 0.0f, "easeOut"},
            Keyframe{jabStrike, 0.4f, "easeOut"},
            Keyframe{neutral_pose(), 1.0f, "easeOut"},
        }));

    PoseSkeleton hookStrike = neutral_pose();
    offset_left_arm(hookStrike, 10, -6);
    offset_torso(hookStrike, -2, 0);
    offset_head(hookStrike, -1, 0);
    templates.emplace(
        "hook",
        make_template("hook", 25, false, {
            Keyframe{neutral_pose(), 0.0f, "cubic"},
            Keyframe{hookStrike, 0.5f, "cubic"},
            Keyframe{neutral_pose(), 1.0f, "cubic"},
        }));

    PoseSkeleton uppercutStrike = neutral_pose();
    offset_right_arm(uppercutStrike, 2, -12);
    offset_torso(uppercutStrike, 0, -4);
    offset_head(uppercutStrike, 0, -3);
    templates.emplace(
        "uppercut",
        make_template("uppercut", 25, false, {
            Keyframe{neutral_pose(), 0.0f, "easeOut"},
            Keyframe{uppercutStrike, 0.5f, "easeOut"},
            Keyframe{neutral_pose(), 1.0f, "easeOut"},
        }));

    PoseSkeleton hitReact = neutral_pose();
    offset_torso(hitReact, -6, 0);
    offset_head(hitReact, -4, 0);
    offset_arms(hitReact, -2, 0);
    offset_legs(hitReact, -1, 0);
    templates.emplace(
        "hit",
        make_template("hit", 15, false, {
            Keyframe{neutral_pose(), 0.0f, "easeIn"},
            Keyframe{hitReact, 1.0f, "easeIn"},
        }));

    PoseSkeleton blockGuard = neutral_pose();
    offset_arms(blockGuard, -6, -4);
    offset_torso(blockGuard, -2, 0);
    templates.emplace(
        "block",
        make_template("block", 25, true, {
            Keyframe{neutral_pose(), 0.0f, "linear"},
            Keyframe{blockGuard, 1.0f, "linear"},
        }));

    PoseSkeleton knockdownMid = neutral_pose();
    offset_torso(knockdownMid, 0, 10);
    offset_head(knockdownMid, 0, 12);
    offset_legs(knockdownMid, 0, 8);

    PoseSkeleton knockdownEnd = neutral_pose();
    offset_torso(knockdownEnd, 0, 20);
    offset_head(knockdownEnd, 0, 24);
    offset_legs(knockdownEnd, 0, 16);
    templates.emplace(
        "knockdown",
        make_template("knockdown", 25, false, {
            Keyframe{neutral_pose(), 0.0f, "easeIn"},
            Keyframe{knockdownMid, 0.5f, "easeIn"},
            Keyframe{knockdownEnd, 1.0f, "easeIn"},
        }));

    PoseSkeleton koPose = knockdownEnd;
    offset_torso(koPose, 0, 6);
    offset_head(koPose, 0, 8);
    offset_arms(koPose, 0, 4);
    offset_legs(koPose, 0, 2);
    templates.emplace(
        "ko",
        make_template("ko", 25, false, {
            Keyframe{knockdownEnd, 0.0f, "linear"},
            Keyframe{koPose, 1.0f, "linear"},
        }));
}

}  // namespace spratgen