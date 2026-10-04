#pragma once

// spratforge::forge — pose model, motion curves and the authored boxing clips.
//
// Everything in this header is expressed in NORMALIZED units:
//   * translations are fractions of the source sprite's body height,
//   * rotations are degrees.
// A clip authored for a 48 px fixture therefore produces the same motion on a
// 400 px master. Phase 1 stored raw pixel amplitudes tuned for 32 px art,
// which is why the 400 px master barely moved.
//
// Limbs are driven by INVERSE kinematics from hand/foot targets rather than by
// per-bone angles. For a front-facing boxer that is far easier to author: a
// punch is "move the glove towards the centre of the frame and scale it up"
// (foreshortening), and a breathing bob is "lower the pelvis while the feet
// stay planted", which automatically produces the knee bend.

#include <array>
#include <string>
#include <vector>

#include <spratforge/forge/forge_rig.hpp>

namespace spratforge::forge {

// A normalized 2D offset. +x is screen-right, +y is screen-down.
struct LimbTarget {
    double dx = 0.0;
    double dy = 0.0;
};

struct ArmPose {
    LimbTarget hand;          // offset applied to the shoulder-relative rest hand
    double glove_scale = 1.0; // foreshortening: >1 means the glove comes at the camera
};

// A complete character pose. Every field is interpolatable; the default value
// of every field reproduces the neutral rest pose exactly.
struct PoseSpec {
    double root_dx = 0.0;     // pelvis offset, x body height
    double root_dy = 0.0;     // pelvis offset, x body height (+ = crouch)
    double spine = 0.0;       // degrees; rotates pelvis->neck and carries the shoulders
    double head = 0.0;        // degrees; extra rotation of neck->head_top
    double pelvis_rot = 0.0;  // degrees; rotates the hip line
    double body_rot = 0.0;    // degrees; whole body, about the rotation centre
    bool rot_about_hips = false;  // false: rotate about the foot pivot (toppling)
    double world_dx = 0.0;    // world-space offset applied after body_rot
    double world_dy = 0.0;
    ArmPose back_arm;         // screen-left arm
    ArmPose front_arm;        // screen-right arm
    LimbTarget back_foot;     // world-space offset of the planted rest foot
    LimbTarget front_foot;
    bool promote_back_arm = false;  // draw the rear arm in front of the torso
};

// Absolute joint positions in source space, plus the per-frame extras the
// renderer needs.
struct PosedRig {
    std::array<Vec2, kJointCount> joints{};
    double back_glove_scale = 1.0;
    double front_glove_scale = 1.0;
    bool promote_back_arm = false;

    [[nodiscard]] const Vec2& joint(JointId id) const { return joints[static_cast<std::size_t>(id)]; }
    [[nodiscard]] Vec2& joint(JointId id) { return joints[static_cast<std::size_t>(id)]; }
};

enum class Ease {
    Linear,
    EaseIn,      // slow start - anticipation
    EaseOut,     // fast start, slow stop - impacts and settles
    EaseInOut,   // smooth both ends - breathing, walking
    Hold         // step function; the previous pose is held until this key
};

struct Keyframe {
    int frame = 0;             // frame index this key lands on
    PoseSpec pose;
    Ease ease = Ease::EaseInOut;  // easing of the segment ENDING at this key
};

struct Clip {
    std::string name;
    // Coarse grouping for consumers that want to pick a clip by role rather
    // than by name: "idle" | "locomotion" | "defense" | "attack" | "reaction".
    std::string category = "idle";
    // "left" | "right" | "up" | "down" for the directional locomotion clips,
    // empty otherwise.
    std::string direction;
    // Non-empty when this clip is kept only as a compatibility alias of
    // another clip (same motion, legacy name).
    std::string alias_of;
    int frame_count = 1;
    int fps = 12;
    bool loop = false;
    bool hold_last_frame = false;
    // When true the posed skeleton is translated vertically so the lowest
    // limb never sinks below the rest ground line. Used by the grounded
    // clips (knockdown, ko) so the body always lies ON the canvas.
    bool ground_clamp = false;
    // Attack timing. -1 means "not an attack".
    int active_from = -1;   // first frame with an active hitbox
    int active_to = -1;     // last frame with an active hitbox
    int impact_frame = -1;  // the single readable "money" frame
    std::vector<Keyframe> keys;
};

// Evaluates a clip at an integer frame index.
PoseSpec sample_clip(const Clip& clip, int frame);

// Resolves a pose into absolute joint positions for a given rest rig.
PosedRig apply_pose(const Rig& rig, const PoseSpec& pose);

// Translates `posed` vertically so that no part of the posed skeleton (joint
// position expanded by its part radius) extends below the rest ground line.
// Deterministic and idempotent.
void clamp_to_ground(const Rig& rest, PosedRig& posed);

// The authored boxing clip library: idle, walk, walk_down, walk_up,
// walk_left, walk_right, block, jab, hook, uppercut, hit, knockdown, ko.
// Order is stable and is the order they are emitted in.
std::vector<Clip> default_boxer_clips();

}  // namespace spratforge::forge
