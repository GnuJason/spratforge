#include <spratforge/forge/forge_motion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace spratforge::forge {
namespace {

constexpr double kPi = 3.14159265358979323846;

double to_radians(double degrees) { return degrees * kPi / 180.0; }

// Rotation by `degrees`. With y growing downwards this reads as a CLOCKWISE
// rotation on screen, matching the angle convention documented in forge_rig.hpp.
Vec2 rotate(Vec2 v, double degrees) {
    const double r = to_radians(degrees);
    const double c = std::cos(r);
    const double s = std::sin(r);
    return Vec2{v.x * c - v.y * s, v.x * s + v.y * c};
}

Vec2 sub(Vec2 a, Vec2 b) { return Vec2{a.x - b.x, a.y - b.y}; }
Vec2 add(Vec2 a, Vec2 b) { return Vec2{a.x + b.x, a.y + b.y}; }
double length(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

double ease_value(Ease ease, double t) {
    t = std::clamp(t, 0.0, 1.0);
    switch (ease) {
        case Ease::Linear: return t;
        case Ease::EaseIn: return t * t;
        case Ease::EaseOut: return 1.0 - (1.0 - t) * (1.0 - t);
        case Ease::EaseInOut:
            return t < 0.5 ? 2.0 * t * t : 1.0 - 2.0 * (1.0 - t) * (1.0 - t);
        case Ease::Hold: return 0.0;
    }
    return t;
}

double mix(double a, double b, double t) { return a + (b - a) * t; }

LimbTarget mix_target(const LimbTarget& a, const LimbTarget& b, double t) {
    return LimbTarget{mix(a.dx, b.dx, t), mix(a.dy, b.dy, t)};
}

ArmPose mix_arm(const ArmPose& a, const ArmPose& b, double t) {
    ArmPose out;
    out.hand = mix_target(a.hand, b.hand, t);
    out.glove_scale = mix(a.glove_scale, b.glove_scale, t);
    return out;
}

PoseSpec mix_pose(const PoseSpec& a, const PoseSpec& b, double t) {
    PoseSpec out;
    out.root_dx = mix(a.root_dx, b.root_dx, t);
    out.root_dy = mix(a.root_dy, b.root_dy, t);
    out.spine = mix(a.spine, b.spine, t);
    out.head = mix(a.head, b.head, t);
    out.pelvis_rot = mix(a.pelvis_rot, b.pelvis_rot, t);
    out.body_rot = mix(a.body_rot, b.body_rot, t);
    out.rot_about_hips = (t < 0.5) ? a.rot_about_hips : b.rot_about_hips;
    out.world_dx = mix(a.world_dx, b.world_dx, t);
    out.world_dy = mix(a.world_dy, b.world_dy, t);
    out.back_arm = mix_arm(a.back_arm, b.back_arm, t);
    out.front_arm = mix_arm(a.front_arm, b.front_arm, t);
    out.back_foot = mix_target(a.back_foot, b.back_foot, t);
    out.front_foot = mix_target(a.front_foot, b.front_foot, t);
    out.promote_back_arm = (t < 0.5) ? a.promote_back_arm : b.promote_back_arm;
    return out;
}

// Two-bone IK. `bend_sign` selects which way the middle joint buckles; it is
// derived from the rest rig so a hand-corrected rig keeps its own anatomy.
void solve_two_bone(Vec2 root, Vec2 target, double l1, double l2, double bend_sign,
                    Vec2& mid, Vec2& end) {
    Vec2 delta = sub(target, root);
    double distance = length(delta);
    if (distance < 1e-6) {
        delta = Vec2{0.0, 1.0};
        distance = 1e-6;
    }
    const double dir_x = delta.x / distance;
    const double dir_y = delta.y / distance;

    const double min_reach = std::fabs(l1 - l2) + 1e-3;
    const double max_reach = std::max(min_reach + 1e-3, l1 + l2 - 1e-3);
    const double reach = std::clamp(distance, min_reach, max_reach);

    const double base = std::atan2(dir_y, dir_x);
    double cos_alpha = (reach * reach + l1 * l1 - l2 * l2) / (2.0 * reach * std::max(l1, 1e-6));
    cos_alpha = std::clamp(cos_alpha, -1.0, 1.0);
    const double alpha = std::acos(cos_alpha);
    const double mid_angle = base + bend_sign * alpha;

    mid = Vec2{root.x + l1 * std::cos(mid_angle), root.y + l1 * std::sin(mid_angle)};
    end = Vec2{root.x + dir_x * reach, root.y + dir_y * reach};
}

// cross < 0 means the middle joint sits clockwise from the root->end line,
// which corresponds to bend_sign = +1 in solve_two_bone().
double bend_sign_from_rest(Vec2 root, Vec2 mid, Vec2 end) {
    const double cross = (mid.x - root.x) * (end.y - root.y) - (mid.y - root.y) * (end.x - root.x);
    return cross < 0.0 ? 1.0 : -1.0;
}

}  // namespace

PoseSpec sample_clip(const Clip& clip, int frame) {
    if (clip.keys.empty()) {
        return PoseSpec{};
    }
    const int last_index = clip.frame_count > 0 ? clip.frame_count - 1 : 0;
    frame = std::clamp(frame, 0, last_index);

    // Loops wrap back onto their first key, so the authored keys only need to
    // cover [0, frame_count) and the cycle closes seamlessly.
    std::vector<Keyframe> keys = clip.keys;
    if (clip.loop) {
        Keyframe wrap = keys.front();
        wrap.frame = clip.frame_count;
        wrap.ease = Ease::EaseInOut;
        keys.push_back(wrap);
    }

    if (frame <= keys.front().frame) {
        return keys.front().pose;
    }
    if (frame >= keys.back().frame) {
        return keys.back().pose;
    }

    for (std::size_t i = 1; i < keys.size(); ++i) {
        const Keyframe& next = keys[i];
        if (frame > next.frame) {
            continue;
        }
        const Keyframe& prev = keys[i - 1];
        const int span = next.frame - prev.frame;
        if (span <= 0) {
            return next.pose;
        }
        const double raw = static_cast<double>(frame - prev.frame) / static_cast<double>(span);
        return mix_pose(prev.pose, next.pose, ease_value(next.ease, raw));
    }
    return keys.back().pose;
}

PosedRig apply_pose(const Rig& rig, const PoseSpec& pose) {
    PosedRig posed;
    const double height = std::max(1.0, rig.body_height());

    const Vec2 rest_pelvis = rig.joint(JointId::Pelvis);
    const Vec2 rest_neck = rig.joint(JointId::Neck);

    const Vec2 pelvis{rest_pelvis.x + pose.root_dx * height, rest_pelvis.y + pose.root_dy * height};
    posed.joint(JointId::Pelvis) = pelvis;

    // Spine carries the neck, the shoulders and (with an extra delta) the head.
    const Vec2 neck = add(pelvis, rotate(sub(rest_neck, rest_pelvis), pose.spine));
    posed.joint(JointId::Neck) = neck;
    posed.joint(JointId::HeadTop) =
        add(neck, rotate(sub(rig.joint(JointId::HeadTop), rest_neck), pose.spine + pose.head));

    const Vec2 shoulder_back = add(neck, rotate(sub(rig.joint(JointId::ShoulderBack), rest_neck), pose.spine));
    const Vec2 shoulder_front = add(neck, rotate(sub(rig.joint(JointId::ShoulderFront), rest_neck), pose.spine));
    posed.joint(JointId::ShoulderBack) = shoulder_back;
    posed.joint(JointId::ShoulderFront) = shoulder_front;

    // Pelvis rotation carries the hip line.
    const Vec2 hip_back = add(pelvis, rotate(sub(rig.joint(JointId::HipBack), rest_pelvis), pose.pelvis_rot));
    const Vec2 hip_front = add(pelvis, rotate(sub(rig.joint(JointId::HipFront), rest_pelvis), pose.pelvis_rot));
    posed.joint(JointId::HipBack) = hip_back;
    posed.joint(JointId::HipFront) = hip_front;

    struct ArmBinding {
        JointId shoulder;
        JointId elbow;
        JointId hand;
        Vec2 posed_shoulder;
        const ArmPose* pose;
    };
    const ArmBinding arms[2] = {
        {JointId::ShoulderBack, JointId::ElbowBack, JointId::HandBack, shoulder_back, &pose.back_arm},
        {JointId::ShoulderFront, JointId::ElbowFront, JointId::HandFront, shoulder_front, &pose.front_arm},
    };
    for (const ArmBinding& arm : arms) {
        const Vec2 rest_shoulder = rig.joint(arm.shoulder);
        const Vec2 rest_elbow = rig.joint(arm.elbow);
        const Vec2 rest_hand = rig.joint(arm.hand);
        const double l1 = std::max(1e-3, length(sub(rest_elbow, rest_shoulder)));
        const double l2 = std::max(1e-3, length(sub(rest_hand, rest_elbow)));
        const double sign = bend_sign_from_rest(rest_shoulder, rest_elbow, rest_hand);

        // The hand follows the shoulder and the spine rotation; the authored
        // offset is then applied in world space on top of that.
        Vec2 target = add(arm.posed_shoulder, rotate(sub(rest_hand, rest_shoulder), pose.spine));
        target.x += arm.pose->hand.dx * height;
        target.y += arm.pose->hand.dy * height;

        Vec2 elbow{};
        Vec2 hand{};
        solve_two_bone(arm.posed_shoulder, target, l1, l2, sign, elbow, hand);
        posed.joint(arm.elbow) = elbow;
        posed.joint(arm.hand) = hand;
    }

    struct LegBinding {
        JointId hip;
        JointId knee;
        JointId foot;
        Vec2 posed_hip;
        const LimbTarget* target;
    };
    const LegBinding legs[2] = {
        {JointId::HipBack, JointId::KneeBack, JointId::FootBack, hip_back, &pose.back_foot},
        {JointId::HipFront, JointId::KneeFront, JointId::FootFront, hip_front, &pose.front_foot},
    };
    for (const LegBinding& leg : legs) {
        const Vec2 rest_hip = rig.joint(leg.hip);
        const Vec2 rest_knee = rig.joint(leg.knee);
        const Vec2 rest_foot = rig.joint(leg.foot);
        const double l1 = std::max(1e-3, length(sub(rest_knee, rest_hip)));
        const double l2 = std::max(1e-3, length(sub(rest_foot, rest_knee)));
        const double sign = bend_sign_from_rest(rest_hip, rest_knee, rest_foot);

        // Feet stay planted in world space, so lowering the pelvis bends the
        // knees instead of sinking the whole character into the floor.
        const Vec2 target{rest_foot.x + leg.target->dx * height,
                          rest_foot.y + leg.target->dy * height};

        Vec2 knee{};
        Vec2 foot{};
        solve_two_bone(leg.posed_hip, target, l1, l2, sign, knee, foot);
        posed.joint(leg.knee) = knee;
        posed.joint(leg.foot) = foot;
    }

    if (std::fabs(pose.body_rot) > 1e-9) {
        const Vec2 centre = pose.rot_about_hips ? pelvis : rig.pivot;
        for (int i = 0; i < kJointCount; ++i) {
            posed.joints[static_cast<std::size_t>(i)] =
                add(centre, rotate(sub(posed.joints[static_cast<std::size_t>(i)], centre), pose.body_rot));
        }
    }
    if (std::fabs(pose.world_dx) > 1e-9 || std::fabs(pose.world_dy) > 1e-9) {
        const Vec2 offset{pose.world_dx * height, pose.world_dy * height};
        for (int i = 0; i < kJointCount; ++i) {
            posed.joints[static_cast<std::size_t>(i)] =
                add(posed.joints[static_cast<std::size_t>(i)], offset);
        }
    }

    posed.back_glove_scale = pose.back_arm.glove_scale;
    posed.front_glove_scale = pose.front_arm.glove_scale;
    posed.promote_back_arm = pose.promote_back_arm;
    return posed;
}

// ---------------------------------------------------------------------------
// Authored clip library
// ---------------------------------------------------------------------------
namespace {

Keyframe key(int frame, const PoseSpec& pose, Ease ease = Ease::EaseInOut) {
    Keyframe k;
    k.frame = frame;
    k.pose = pose;
    k.ease = ease;
    return k;
}

PoseSpec guard() { return PoseSpec{}; }

Clip make_idle() {
    Clip clip;
    clip.name = "idle";
    clip.category = "idle";
    clip.frame_count = 8;
    clip.fps = 10;
    clip.loop = true;

    PoseSpec a = guard();

    PoseSpec b = guard();
    b.root_dy = 0.014;
    b.spine = 1.4;
    b.head = -1.2;
    b.back_arm.hand = {0.006, 0.013};
    b.front_arm.hand = {-0.006, 0.013};

    PoseSpec c = guard();
    c.root_dy = 0.026;
    c.spine = 2.4;
    c.head = -2.0;
    c.back_arm.hand = {0.010, 0.024};
    c.front_arm.hand = {-0.010, 0.024};

    PoseSpec d = guard();
    d.root_dy = 0.012;
    d.spine = 1.0;
    d.head = -0.8;
    d.back_arm.hand = {0.005, 0.010};
    d.front_arm.hand = {-0.005, 0.010};

    clip.keys = {key(0, a), key(2, b), key(4, c), key(6, d)};
    return clip;
}

Clip make_walk() {
    Clip clip;
    clip.name = "walk";
    clip.category = "locomotion";
    // Backward compatibility: `walk` keeps the Phase 2/3 shuffle verbatim and
    // is documented as the alias of the default (downward-facing) walk.
    clip.alias_of = "walk_down";
    clip.frame_count = 8;
    clip.fps = 12;
    clip.loop = true;

    // A front-facing boxer "walks" by shuffling: one foot lifts and resets
    // while the body sways over the planted foot.
    PoseSpec k0 = guard();
    k0.back_foot = {-0.020, 0.0};
    k0.front_foot = {0.020, 0.0};
    k0.root_dy = 0.012;
    k0.root_dx = -0.004;

    PoseSpec k1 = guard();
    k1.back_foot = {-0.005, -0.042};
    k1.front_foot = {0.020, 0.0};
    k1.root_dy = 0.002;
    k1.root_dx = 0.010;
    k1.spine = 2.5;
    k1.back_arm.hand = {0.012, -0.010};

    PoseSpec k2 = guard();
    k2.back_foot = {0.012, -0.012};
    k2.front_foot = {0.022, 0.0};
    k2.root_dy = 0.016;
    k2.root_dx = 0.006;
    k2.spine = 1.0;

    PoseSpec k3 = guard();
    k3.back_foot = {0.008, 0.0};
    k3.front_foot = {0.022, 0.0};
    k3.root_dy = 0.010;
    k3.root_dx = 0.002;

    PoseSpec k4 = guard();
    k4.back_foot = {0.008, 0.0};
    k4.front_foot = {0.014, -0.040};
    k4.root_dy = 0.002;
    k4.root_dx = -0.010;
    k4.spine = -2.5;
    k4.front_arm.hand = {-0.012, -0.010};

    PoseSpec k5 = guard();
    k5.back_foot = {-0.008, 0.0};
    k5.front_foot = {0.006, -0.014};
    k5.root_dy = 0.016;
    k5.root_dx = -0.006;
    k5.spine = -1.0;

    PoseSpec k6 = guard();
    k6.back_foot = {-0.016, 0.0};
    k6.front_foot = {0.012, 0.0};
    k6.root_dy = 0.012;
    k6.root_dx = -0.002;

    clip.keys = {key(0, k0), key(1, k1, Ease::EaseOut), key(2, k2), key(3, k3),
                 key(4, k4, Ease::EaseOut), key(5, k5), key(6, k6), key(7, k0)};
    return clip;
}

Clip make_block() {
    Clip clip;
    clip.name = "block";
    clip.category = "defense";
    clip.frame_count = 4;
    clip.fps = 14;
    clip.hold_last_frame = true;

    PoseSpec raise = guard();
    raise.root_dy = 0.016;
    raise.spine = 1.5;
    raise.head = 1.5;
    raise.back_arm.hand = {0.034, -0.050};
    raise.front_arm.hand = {-0.034, -0.054};

    PoseSpec settle = raise;
    settle.root_dy = 0.022;
    settle.back_arm.hand = {0.044, -0.066};
    settle.front_arm.hand = {-0.044, -0.070};

    PoseSpec held = raise;
    held.root_dy = 0.020;
    held.back_arm.hand = {0.040, -0.062};
    held.front_arm.hand = {-0.040, -0.066};

    clip.keys = {key(0, guard()), key(1, raise, Ease::EaseOut), key(2, settle, Ease::EaseOut),
                 key(3, held)};
    return clip;
}

Clip make_jab() {
    Clip clip;
    clip.name = "jab";
    clip.category = "attack";
    clip.frame_count = 7;
    clip.fps = 18;
    clip.active_from = 3;
    clip.active_to = 4;
    clip.impact_frame = 3;

    PoseSpec wind = guard();  // anticipation: small pull back and weight shift
    wind.root_dx = -0.012;
    wind.spine = -2.5;
    wind.front_arm.hand = {0.016, 0.014};
    wind.front_arm.glove_scale = 0.92;

    PoseSpec launch = guard();
    launch.root_dx = 0.014;
    launch.spine = 3.0;
    launch.front_arm.hand = {-0.058, 0.010};
    launch.front_arm.glove_scale = 1.26;

    PoseSpec impact = guard();
    impact.root_dx = 0.026;
    impact.spine = 5.0;
    impact.head = 2.0;
    impact.front_arm.hand = {-0.104, 0.004};
    impact.front_arm.glove_scale = 1.62;
    impact.back_arm.hand = {0.010, -0.014};

    PoseSpec settle = guard();
    settle.root_dx = 0.016;
    settle.spine = 3.5;
    settle.front_arm.hand = {-0.072, 0.008};
    settle.front_arm.glove_scale = 1.38;

    PoseSpec recover = guard();
    recover.root_dx = 0.004;
    recover.spine = 1.2;
    recover.front_arm.hand = {-0.022, 0.012};
    recover.front_arm.glove_scale = 1.10;

    clip.keys = {key(0, guard()), key(1, wind, Ease::EaseIn), key(2, launch, Ease::EaseOut),
                 key(3, impact, Ease::EaseOut), key(4, settle), key(5, recover),
                 key(6, guard(), Ease::EaseInOut)};
    return clip;
}

Clip make_hook() {
    Clip clip;
    clip.name = "hook";
    clip.category = "attack";
    clip.frame_count = 8;
    clip.fps = 16;
    clip.active_from = 4;
    clip.active_to = 5;
    clip.impact_frame = 4;

    PoseSpec wind = guard();
    wind.root_dx = -0.014;
    wind.spine = -5.0;
    wind.pelvis_rot = -2.0;
    wind.front_arm.hand = {0.048, -0.012};

    PoseSpec coil = guard();
    coil.root_dx = -0.018;
    coil.spine = -6.5;
    coil.pelvis_rot = -3.0;
    coil.front_arm.hand = {0.036, -0.020};

    PoseSpec swing = guard();
    swing.root_dx = 0.012;
    swing.spine = 4.0;
    swing.front_arm.hand = {-0.046, -0.026};
    swing.front_arm.glove_scale = 1.22;

    PoseSpec impact = guard();
    impact.root_dx = 0.030;
    impact.spine = 10.0;
    impact.head = 4.0;
    impact.pelvis_rot = 4.0;
    impact.front_arm.hand = {-0.150, -0.016};
    impact.front_arm.glove_scale = 1.46;
    impact.back_arm.hand = {0.014, -0.012};

    PoseSpec follow = guard();  // overshoot before the recovery
    follow.root_dx = 0.032;
    follow.spine = 11.5;
    follow.head = 3.0;
    follow.pelvis_rot = 4.5;
    follow.front_arm.hand = {-0.178, 0.004};
    follow.front_arm.glove_scale = 1.30;

    PoseSpec recover = guard();
    recover.root_dx = 0.014;
    recover.spine = 5.0;
    recover.front_arm.hand = {-0.086, 0.012};
    recover.front_arm.glove_scale = 1.10;

    clip.keys = {key(0, guard()), key(1, wind, Ease::EaseIn), key(2, coil, Ease::EaseIn),
                 key(3, swing, Ease::EaseOut), key(4, impact, Ease::EaseOut), key(5, follow),
                 key(6, recover), key(7, guard())};
    return clip;
}

Clip make_uppercut() {
    Clip clip;
    clip.name = "uppercut";
    clip.category = "attack";
    clip.frame_count = 9;
    clip.fps = 16;
    clip.active_from = 4;
    clip.active_to = 5;
    clip.impact_frame = 4;

    // The rear (screen-left) arm throws the uppercut, so it is promoted in
    // front of the torso for the whole clip to keep the depth order readable.
    PoseSpec base = guard();
    base.promote_back_arm = true;

    PoseSpec drop = base;
    drop.root_dy = 0.016;
    drop.spine = -3.0;
    drop.back_arm.hand = {0.012, 0.048};
    drop.back_arm.glove_scale = 0.94;

    PoseSpec coil = base;
    coil.root_dy = 0.026;
    coil.spine = -4.5;
    coil.pelvis_rot = -2.0;
    coil.back_arm.hand = {0.018, 0.064};
    coil.back_arm.glove_scale = 0.92;

    PoseSpec rise = base;
    rise.root_dy = 0.004;
    rise.spine = 2.0;
    rise.back_arm.hand = {0.024, 0.006};
    rise.back_arm.glove_scale = 1.20;

    PoseSpec impact = base;
    impact.root_dy = -0.024;
    impact.spine = 6.0;
    impact.head = -3.0;
    impact.pelvis_rot = 3.0;
    impact.back_arm.hand = {0.034, -0.108};
    impact.back_arm.glove_scale = 1.52;
    impact.front_arm.hand = {-0.010, 0.010};

    PoseSpec follow = base;
    follow.root_dy = -0.032;
    follow.spine = 7.0;
    follow.head = -4.0;
    follow.pelvis_rot = 3.5;
    follow.back_arm.hand = {0.038, -0.134};
    follow.back_arm.glove_scale = 1.40;

    PoseSpec descend = base;
    descend.root_dy = -0.012;
    descend.spine = 3.5;
    descend.back_arm.hand = {0.026, -0.072};
    descend.back_arm.glove_scale = 1.20;

    PoseSpec recover = base;
    recover.root_dy = 0.002;
    recover.spine = 1.0;
    recover.back_arm.hand = {0.010, -0.022};
    recover.back_arm.glove_scale = 1.06;

    clip.keys = {key(0, base), key(1, drop, Ease::EaseIn), key(2, coil, Ease::EaseIn),
                 key(3, rise, Ease::EaseOut), key(4, impact, Ease::EaseOut), key(5, follow),
                 key(6, descend), key(7, recover), key(8, base)};
    return clip;
}

Clip make_hit() {
    Clip clip;
    clip.name = "hit";
    clip.category = "reaction";
    clip.frame_count = 5;
    clip.fps = 14;

    PoseSpec snap = guard();
    snap.root_dx = -0.032;
    snap.spine = -7.0;
    snap.head = -9.0;
    snap.body_rot = -2.5;
    snap.back_arm.hand = {-0.016, 0.022};
    snap.front_arm.hand = {0.022, 0.030};

    PoseSpec deep = guard();
    deep.root_dx = -0.048;
    deep.spine = -11.0;
    deep.head = -13.0;
    deep.body_rot = -4.5;
    deep.root_dy = 0.014;
    deep.back_arm.hand = {-0.022, 0.030};
    deep.front_arm.hand = {0.030, 0.040};

    PoseSpec ease_back = guard();
    ease_back.root_dx = -0.018;
    ease_back.spine = -4.0;
    ease_back.head = -5.0;
    ease_back.body_rot = -1.5;
    ease_back.front_arm.hand = {0.012, 0.016};

    clip.keys = {key(0, guard()), key(1, snap, Ease::EaseOut), key(2, deep, Ease::EaseOut),
                 key(3, ease_back), key(4, guard())};
    return clip;
}

Clip make_knockdown() {
    Clip clip;
    clip.name = "knockdown";
    clip.category = "reaction";
    clip.ground_clamp = true;
    clip.frame_count = 10;
    clip.fps = 12;
    clip.hold_last_frame = true;

    PoseSpec snap = guard();
    snap.spine = -8.0;
    snap.head = -11.0;
    snap.root_dx = -0.022;

    PoseSpec reel = guard();
    reel.spine = -14.0;
    reel.head = -16.0;
    reel.root_dx = -0.046;
    reel.body_rot = -7.0;
    reel.back_arm.hand = {-0.030, -0.040};
    reel.front_arm.hand = {0.050, -0.046};

    PoseSpec topple = guard();
    topple.spine = -14.0;
    topple.head = -14.0;
    topple.body_rot = -18.0;
    topple.root_dy = 0.030;
    topple.back_arm.hand = {-0.046, -0.030};
    topple.front_arm.hand = {0.066, -0.030};
    topple.back_foot = {0.016, 0.0};
    topple.front_foot = {0.030, 0.0};

    PoseSpec falling = guard();
    falling.spine = -10.0;
    falling.head = -8.0;
    falling.body_rot = -34.0;
    falling.root_dy = 0.050;
    falling.world_dy = 0.040;
    falling.back_arm.hand = {-0.056, 0.010};
    falling.front_arm.hand = {0.070, 0.010};
    falling.back_foot = {0.040, -0.020};
    falling.front_foot = {0.060, -0.020};

    PoseSpec landing = guard();
    landing.spine = -6.0;
    landing.head = -4.0;
    landing.body_rot = -52.0;
    landing.root_dy = 0.060;
    landing.world_dy = 0.120;
    landing.world_dx = -0.020;
    landing.back_arm.hand = {-0.070, 0.050};
    landing.front_arm.hand = {0.070, 0.050};
    landing.back_foot = {0.070, -0.040};
    landing.front_foot = {0.100, -0.040};

    PoseSpec bounce = landing;
    bounce.body_rot = -60.0;
    bounce.world_dy = 0.160;
    bounce.world_dx = -0.034;
    bounce.spine = -3.0;
    bounce.head = -1.0;
    bounce.back_arm.hand = {-0.082, 0.070};
    bounce.front_arm.hand = {0.078, 0.070};
    bounce.back_foot = {0.090, -0.050};
    bounce.front_foot = {0.124, -0.050};

    PoseSpec rest_down = bounce;
    rest_down.body_rot = -56.0;
    rest_down.world_dy = 0.150;
    rest_down.spine = -1.0;
    rest_down.head = 2.0;

    PoseSpec held = rest_down;
    held.body_rot = -57.0;
    held.world_dy = 0.154;

    clip.keys = {key(0, guard()),
                 key(1, snap, Ease::EaseOut),
                 key(2, reel, Ease::EaseOut),
                 key(3, topple, Ease::EaseIn),
                 key(4, falling, Ease::EaseIn),
                 key(5, landing, Ease::EaseIn),
                 key(6, bounce, Ease::EaseOut),
                 key(7, rest_down, Ease::EaseOut),
                 key(8, held),
                 key(9, held)};
    return clip;
}

Clip make_ko() {
    Clip clip;
    clip.name = "ko";
    clip.category = "reaction";
    clip.ground_clamp = true;
    clip.frame_count = 14;
    clip.fps = 12;
    clip.hold_last_frame = true;

    PoseSpec stun = guard();
    stun.spine = -5.0;
    stun.head = -8.0;
    stun.root_dy = 0.010;
    stun.back_arm.hand = {-0.010, 0.040};
    stun.front_arm.hand = {0.014, 0.044};

    PoseSpec wobble = guard();
    wobble.spine = 3.0;
    wobble.head = 6.0;
    wobble.root_dx = 0.016;
    wobble.root_dy = 0.022;
    wobble.body_rot = 3.0;
    wobble.back_arm.hand = {-0.016, 0.070};
    wobble.front_arm.hand = {0.020, 0.074};

    PoseSpec buckle = guard();
    buckle.spine = -6.0;
    buckle.head = -6.0;
    buckle.root_dx = -0.024;
    buckle.root_dy = 0.044;
    buckle.body_rot = -6.0;
    buckle.back_arm.hand = {-0.030, 0.080};
    buckle.front_arm.hand = {0.034, 0.084};

    PoseSpec tipping = guard();
    tipping.spine = -10.0;
    tipping.head = -10.0;
    tipping.root_dy = 0.050;
    tipping.body_rot = -20.0;
    tipping.back_arm.hand = {-0.050, 0.030};
    tipping.front_arm.hand = {0.060, 0.030};
    tipping.back_foot = {0.020, 0.0};
    tipping.front_foot = {0.034, 0.0};

    PoseSpec falling = guard();
    falling.spine = -6.0;
    falling.head = -4.0;
    falling.root_dy = 0.050;
    falling.body_rot = -42.0;
    falling.world_dy = 0.060;
    falling.world_dx = -0.010;
    falling.back_arm.hand = {-0.070, 0.010};
    falling.front_arm.hand = {0.080, 0.010};
    falling.back_foot = {0.050, -0.030};
    falling.front_foot = {0.078, -0.030};

    PoseSpec slam = guard();
    slam.spine = -2.0;
    slam.head = 2.0;
    slam.root_dy = 0.055;
    slam.body_rot = -74.0;
    slam.world_dy = 0.195;
    slam.world_dx = -0.040;
    slam.back_arm.hand = {-0.100, 0.070};
    slam.front_arm.hand = {0.100, 0.075};
    slam.back_foot = {0.110, -0.060};
    slam.front_foot = {0.150, -0.060};

    PoseSpec rebound = slam;  // small impact bounce
    rebound.body_rot = -78.0;
    rebound.world_dy = 0.215;
    rebound.world_dx = -0.050;
    rebound.back_arm.hand = {-0.116, 0.090};
    rebound.front_arm.hand = {0.112, 0.094};
    rebound.back_foot = {0.126, -0.066};
    rebound.front_foot = {0.168, -0.066};

    PoseSpec flat = rebound;
    flat.body_rot = -84.0;
    flat.world_dy = 0.235;
    flat.world_dx = -0.058;
    flat.spine = 0.0;
    flat.head = 4.0;
    flat.back_arm.hand = {-0.126, 0.100};
    flat.front_arm.hand = {0.120, 0.104};
    flat.back_foot = {0.140, -0.070};
    flat.front_foot = {0.186, -0.070};

    PoseSpec twitch = flat;
    twitch.head = 1.0;
    twitch.back_arm.hand = {-0.120, 0.094};
    twitch.front_arm.hand = {0.126, 0.108};

    PoseSpec still = flat;

    clip.keys = {key(0, guard()),
                 key(1, stun, Ease::EaseOut),
                 key(2, wobble),
                 key(3, buckle),
                 key(4, tipping, Ease::EaseIn),
                 key(5, falling, Ease::EaseIn),
                 key(6, slam, Ease::EaseIn),
                 key(7, rebound, Ease::EaseOut),
                 key(8, flat, Ease::EaseOut),
                 key(9, twitch),
                 key(10, still),
                 key(11, still),
                 key(12, still),
                 key(13, still)};
    return clip;
}

}  // namespace

void clamp_to_ground(const Rig& rest, PosedRig& posed) {
    // Per-joint vertical extent = joint y plus the largest radius of any part
    // that ends at that joint. The deepest extent is compared against the rest
    // ground line (the pivot, which sits at the feet).
    std::array<double, kJointCount> radius{};
    radius.fill(0.0);
    for (const PartSpec& part : rest.parts) {
        const std::size_t from = static_cast<std::size_t>(part.from);
        const std::size_t to = static_cast<std::size_t>(part.to);
        radius[from] = std::max(radius[from], part.radius_from);
        radius[to] = std::max(radius[to], part.radius_to);
    }
    double lowest = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < kJointCount; ++index) {
        lowest = std::max(lowest, posed.joints[index].y + radius[index]);
    }
    const double ground = rest.pivot.y;
    if (lowest > ground) {
        const double shift = lowest - ground;
        for (Vec2& joint : posed.joints) joint.y -= shift;
    }
}

namespace {

// ---------------------------------------------------------------------------
// Directional walk cycles.
//
// A front-facing boxer cannot be re-drawn in profile from a single neutral
// sprite, so the directional clips encode the four canonical pixel-art walk
// PHASES explicitly and lean/bias the body towards the travel direction:
//
//   phase 0  CONTACT  - lead heel plants, body at its lowest forward reach
//   phase 1  DOWN     - weight transfers onto the planted foot, pelvis lowest
//   phase 2  PASSING  - swing foot passes the planted one, pelvis highest
//   phase 3  UP       - push-off, body rises and begins the next reach
//
// Eight frames = two full strides (left lead, then right lead), which is what
// stops the cycle from looking like a hop. Foot sliding is avoided by moving
// the PLANTED foot backwards at a constant rate for the whole time it is on
// the ground: the planted contact point therefore travels at exactly the same
// speed as the body, which is the definition of a non-sliding cycle.
// `stride` scales the foot travel, `bias_x`/`bias_y` lean the torso, and
// `lift` is how high the swing foot clears the ground.
Clip make_directional_walk(const std::string& name,
                           double stride,
                           double lift,
                           double bias_x,
                           double bias_y,
                           double spine_amplitude) {
    Clip clip;
    clip.name = name;
    clip.category = "locomotion";
    clip.direction = name.substr(name.rfind('_') + 1);
    clip.frame_count = 8;
    clip.fps = 12;
    clip.loop = true;

    // Pelvis height per phase: contact mid, down low, passing high, up mid.
    const double bob[4] = {0.010, 0.020, 0.000, 0.008};

    auto phase_pose = [&](int phase, bool back_is_swing) {
        PoseSpec pose = guard();
        // Planted foot: travels linearly from +stride/2 (just planted, ahead
        // of the body) to -stride/2 (about to leave the ground).
        const double planted_t = static_cast<double>(phase) / 3.0;  // 0 .. 1
        const double planted_x = (0.5 - planted_t) * stride;
        // Swing foot: mirrors the planted one, one half-cycle out of step, and
        // lifts through the passing phase.
        const double swing_t = planted_t;
        const double swing_x = (-0.5 + swing_t) * stride;
        const double swing_lift = -lift * std::sin(swing_t * 3.14159265358979323846);

        LimbTarget planted{planted_x + bias_x * 0.25, 0.0};
        LimbTarget swing{swing_x + bias_x * 0.25, swing_lift};

        if (back_is_swing) {
            pose.back_foot = swing;
            pose.front_foot = planted;
            pose.spine = spine_amplitude;
            pose.back_arm.hand = {0.010 + bias_x * 0.4, -0.008};
            pose.front_arm.hand = {-0.004 + bias_x * 0.4, 0.004};
        } else {
            pose.back_foot = planted;
            pose.front_foot = swing;
            pose.spine = -spine_amplitude;
            pose.front_arm.hand = {-0.010 + bias_x * 0.4, -0.008};
            pose.back_arm.hand = {0.004 + bias_x * 0.4, 0.004};
        }
        pose.root_dy = bob[phase] + bias_y;
        pose.root_dx = bias_x + (back_is_swing ? 0.004 : -0.004);
        pose.pelvis_rot = (back_is_swing ? 1.5 : -1.5);
        return pose;
    };

    // Phases run CONTACT, DOWN, PASSING, UP twice; easing is EaseInOut for the
    // weight transfers and EaseOut for the push-off, which reads as
    // follow-through rather than a metronome.
    clip.keys.reserve(9);
    for (int step = 0; step < 2; ++step) {
        for (int phase = 0; phase < 4; ++phase) {
            const int frame = step * 4 + phase;
            const Ease ease = (phase == 3) ? Ease::EaseOut : Ease::EaseInOut;
            clip.keys.push_back(key(frame, phase_pose(phase, step == 0), ease));
        }
    }
    // No duplicated terminal key: frame 7 is the UP phase of the second
    // stride and loops straight back into frame 0 (CONTACT), so the cycle
    // never stutters on a repeated pose.
    return clip;
}

Clip make_walk_down() { return make_directional_walk("walk_down", 0.075, 0.045, 0.000, 0.000, 2.5); }
// Walking "up" (away from the camera) reads as a slightly smaller, more
// upright shuffle; walking left/right leans the torso into the travel.
Clip make_walk_up() { return make_directional_walk("walk_up", 0.060, 0.034, 0.000, -0.006, 1.6); }
Clip make_walk_left() { return make_directional_walk("walk_left", 0.070, 0.040, -0.014, 0.002, 2.2); }
Clip make_walk_right() { return make_directional_walk("walk_right", 0.070, 0.040, 0.014, 0.002, 2.2); }

}  // namespace

std::vector<Clip> default_boxer_clips() {
    // `walk` stays FIRST among the walks and keeps the Phase 2/3 shuffle so
    // existing consumers (RingQueen) see no behavioural change. The four
    // directional clips are additive.
    return {make_idle(),      make_walk(),       make_walk_down(), make_walk_up(),
            make_walk_left(), make_walk_right(), make_block(),     make_jab(),
            make_hook(),      make_uppercut(),   make_hit(),       make_knockdown(),
            make_ko()};
}

}  // namespace spratforge::forge
