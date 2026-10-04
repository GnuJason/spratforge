#pragma once

// spratforge::forge — rig estimation for a single neutral biped sprite.
//
// Coordinate system
// -----------------
// All geometry is expressed in *source image space*: x grows to the right,
// y grows DOWNWARD (standard image convention). Angles are measured from the
// +x axis and increase CLOCKWISE on screen, i.e. 0deg = right, +90deg = down,
// -90deg = up, 180deg = left.
//
// Side naming
// -----------
// "front" is the character's screen-RIGHT side (the side that faces the
// opponent in RingQueen's default orientation) and "back" is the screen-LEFT
// side. The renderer draws the back limbs behind the torso and the front limbs
// in front of it, which is what gives the sprite depth.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <json/json.hpp>
#include <spratforge/core/palette_core.hpp>

namespace spratforge::forge {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

struct BoxI {
    int min_x = 0;
    int min_y = 0;
    int max_x = -1;
    int max_y = -1;

    [[nodiscard]] bool valid() const { return max_x >= min_x && max_y >= min_y; }
    [[nodiscard]] int width() const { return valid() ? max_x - min_x + 1 : 0; }
    [[nodiscard]] int height() const { return valid() ? max_y - min_y + 1 : 0; }
};

// Joint identifiers. The order is also the serialization order of rig.json and
// must stay stable: rig override files refer to joints by name, never by index.
enum class JointId : int {
    Pelvis = 0,
    Neck,
    HeadTop,
    ShoulderBack,
    ElbowBack,
    HandBack,
    ShoulderFront,
    ElbowFront,
    HandFront,
    HipBack,
    KneeBack,
    FootBack,
    HipFront,
    KneeFront,
    FootFront,
    Count
};

inline constexpr int kJointCount = static_cast<int>(JointId::Count);

// Body parts. The numeric value is NOT the draw order; see part_z_order().
enum class PartId : int {
    Torso = 0,
    Head,
    BackUpperArm,
    BackForearm,
    BackGlove,
    FrontUpperArm,
    FrontForearm,
    FrontGlove,
    BackThigh,
    BackShin,
    FrontThigh,
    FrontShin,
    Count
};

inline constexpr int kPartCount = static_cast<int>(PartId::Count);

const char* joint_name(JointId joint);
const char* part_name(PartId part);
// Returns JointId::Count when the name is unknown.
JointId joint_from_name(const std::string& name);

// Painter's-algorithm depth. Lower values are drawn first (further away).
int part_z_order(PartId part);

// A part is attached to a bone (two joints) and carries a radius at each end.
// Segmentation and posing both use this definition, so a hand-corrected rig
// automatically re-segments the sprite.
struct PartSpec {
    PartId id = PartId::Torso;
    JointId from = JointId::Pelvis;
    JointId to = JointId::Neck;
    double radius_from = 1.0;
    double radius_to = 1.0;
    // Subtracted from the normalized capsule distance when competing for a
    // pixel. Positive values make the part "greedier".
    double bias = 0.0;
};

struct Rig {
    int width = 0;   // source canvas width
    int height = 0;  // source canvas height
    BoxI body;       // opaque bounding box of the source sprite
    std::array<Vec2, kJointCount> joints{};
    std::array<PartSpec, kPartCount> parts{};
    Vec2 pivot{};  // foot anchor: ground contact point, source space

    [[nodiscard]] const Vec2& joint(JointId id) const { return joints[static_cast<std::size_t>(id)]; }
    [[nodiscard]] Vec2& joint(JointId id) { return joints[static_cast<std::size_t>(id)]; }
    [[nodiscard]] const PartSpec& part(PartId id) const { return parts[static_cast<std::size_t>(id)]; }
    [[nodiscard]] double body_height() const { return static_cast<double>(body.height()); }
    [[nodiscard]] double body_width() const { return static_cast<double>(body.width()); }
};

// Binary silhouette of a frame: 1 where alpha >= threshold.
std::vector<std::uint8_t> silhouette_of(const core::Frame& frame, int alpha_threshold = 16);
BoxI bounds_of(const std::vector<std::uint8_t>& mask, int width, int height);

// Estimates an anatomically plausible rig for a neutral, standing biped.
//
// The estimator is purely geometric and fully deterministic: it reads the
// silhouette's row profile to locate the shoulder line, waist, crotch and
// ground, and uses a chamfer distance transform to find the thickest blob at
// the end of each arm (the glove). It never picks "the topmost cluster of a
// column strip", which is what used to drop the arm pivots into the hair.
//
// Throws std::invalid_argument when the frame has no opaque pixels.
Rig estimate_rig(const core::Frame& frame, int alpha_threshold = 16);

// rig.json round-tripping. The emitted document is stable and human editable.
nlohmann::json rig_to_json(const Rig& rig);
// Applies a user-authored override document on top of an estimated rig.
// Recognised keys:
//   {"joints": {"<joint_name>": [x, y], ...},
//    "pivot": [x, y],
//    "radii": {"<part_name>": {"from": r, "to": r}, ...}}
// Unknown keys are rejected so typos surface immediately.
Rig apply_rig_override(const Rig& rig, const nlohmann::json& override_document);
Rig load_rig_override(const Rig& rig, const std::string& path);

}  // namespace spratforge::forge
