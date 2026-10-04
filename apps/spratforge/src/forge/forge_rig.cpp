#include <spratforge/forge/forge_rig.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace spratforge::forge {
namespace {

constexpr double kChamferNear = 1.0;      // orthogonal step cost
constexpr double kChamferDiagonal = 1.4;  // diagonal step cost

struct RowProfile {
    std::vector<int> width;   // opaque pixel count per row, indexed from body.min_y
    std::vector<int> left;    // first opaque column, -1 when empty
    std::vector<int> right;   // last opaque column, -1 when empty
    std::vector<int> runs;    // number of horizontal runs in the row
    std::vector<double> centroid;
};

RowProfile build_row_profile(const std::vector<std::uint8_t>& mask, int width, const BoxI& body) {
    RowProfile profile;
    const int rows = body.height();
    profile.width.assign(static_cast<std::size_t>(rows), 0);
    profile.left.assign(static_cast<std::size_t>(rows), -1);
    profile.right.assign(static_cast<std::size_t>(rows), -1);
    profile.runs.assign(static_cast<std::size_t>(rows), 0);
    profile.centroid.assign(static_cast<std::size_t>(rows), 0.0);
    for (int row = 0; row < rows; ++row) {
        const int y = body.min_y + row;
        long long sum = 0;
        int count = 0;
        int runs = 0;
        bool previous = false;
        for (int x = body.min_x; x <= body.max_x; ++x) {
            const bool opaque = mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                     static_cast<std::size_t>(x)] != 0U;
            if (opaque) {
                if (!previous) ++runs;
                if (profile.left[static_cast<std::size_t>(row)] < 0) profile.left[static_cast<std::size_t>(row)] = x;
                profile.right[static_cast<std::size_t>(row)] = x;
                sum += x;
                ++count;
            }
            previous = opaque;
        }
        profile.width[static_cast<std::size_t>(row)] = count;
        profile.runs[static_cast<std::size_t>(row)] = runs;
        profile.centroid[static_cast<std::size_t>(row)] =
            count > 0 ? static_cast<double>(sum) / static_cast<double>(count) : 0.0;
    }
    return profile;
}

// Two-pass chamfer distance transform. Deterministic, O(w*h), and good enough
// to find the thickest blob of a limb (the boxing glove).
std::vector<double> distance_transform(const std::vector<std::uint8_t>& mask, int width, int height) {
    const double infinity = std::numeric_limits<double>::max() / 4.0;
    std::vector<double> distance(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0);
    const auto at = [width](int x, int y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) distance[at(x, y)] = mask[at(x, y)] != 0U ? infinity : 0.0;
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (distance[at(x, y)] == 0.0) continue;
            double best = distance[at(x, y)];
            if (y > 0) best = std::min(best, distance[at(x, y - 1)] + kChamferNear);
            if (x > 0) best = std::min(best, distance[at(x - 1, y)] + kChamferNear);
            if (x > 0 && y > 0) best = std::min(best, distance[at(x - 1, y - 1)] + kChamferDiagonal);
            if (x + 1 < width && y > 0) best = std::min(best, distance[at(x + 1, y - 1)] + kChamferDiagonal);
            distance[at(x, y)] = best;
        }
    }
    for (int y = height - 1; y >= 0; --y) {
        for (int x = width - 1; x >= 0; --x) {
            if (distance[at(x, y)] == 0.0) continue;
            double best = distance[at(x, y)];
            if (y + 1 < height) best = std::min(best, distance[at(x, y + 1)] + kChamferNear);
            if (x + 1 < width) best = std::min(best, distance[at(x + 1, y)] + kChamferNear);
            if (x + 1 < width && y + 1 < height) best = std::min(best, distance[at(x + 1, y + 1)] + kChamferDiagonal);
            if (x > 0 && y + 1 < height) best = std::min(best, distance[at(x - 1, y + 1)] + kChamferDiagonal);
            distance[at(x, y)] = best;
        }
    }
    return distance;
}

double median_of(std::vector<double> values, double fallback) {
    if (values.empty()) return fallback;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

}  // namespace

const char* joint_name(JointId joint) {
    switch (joint) {
        case JointId::Pelvis: return "pelvis";
        case JointId::Neck: return "neck";
        case JointId::HeadTop: return "head_top";
        case JointId::ShoulderBack: return "shoulder_back";
        case JointId::ElbowBack: return "elbow_back";
        case JointId::HandBack: return "hand_back";
        case JointId::ShoulderFront: return "shoulder_front";
        case JointId::ElbowFront: return "elbow_front";
        case JointId::HandFront: return "hand_front";
        case JointId::HipBack: return "hip_back";
        case JointId::KneeBack: return "knee_back";
        case JointId::FootBack: return "foot_back";
        case JointId::HipFront: return "hip_front";
        case JointId::KneeFront: return "knee_front";
        case JointId::FootFront: return "foot_front";
        case JointId::Count: break;
    }
    return "unknown";
}

JointId joint_from_name(const std::string& name) {
    for (int index = 0; index < kJointCount; ++index) {
        const auto id = static_cast<JointId>(index);
        if (name == joint_name(id)) return id;
    }
    return JointId::Count;
}

const char* part_name(PartId part) {
    switch (part) {
        case PartId::Torso: return "torso";
        case PartId::Head: return "head";
        case PartId::BackUpperArm: return "back_upper_arm";
        case PartId::BackForearm: return "back_forearm";
        case PartId::BackGlove: return "back_glove";
        case PartId::FrontUpperArm: return "front_upper_arm";
        case PartId::FrontForearm: return "front_forearm";
        case PartId::FrontGlove: return "front_glove";
        case PartId::BackThigh: return "back_thigh";
        case PartId::BackShin: return "back_shin";
        case PartId::FrontThigh: return "front_thigh";
        case PartId::FrontShin: return "front_shin";
        case PartId::Count: break;
    }
    return "unknown";
}

int part_z_order(PartId part) {
    switch (part) {
        case PartId::BackThigh: return 10;
        case PartId::BackShin: return 11;
        case PartId::BackUpperArm: return 20;
        case PartId::BackForearm: return 21;
        case PartId::BackGlove: return 22;
        case PartId::Torso: return 30;
        case PartId::Head: return 40;
        case PartId::FrontThigh: return 50;
        case PartId::FrontShin: return 51;
        case PartId::FrontUpperArm: return 60;
        case PartId::FrontForearm: return 61;
        case PartId::FrontGlove: return 62;
        case PartId::Count: break;
    }
    return 0;
}

std::vector<std::uint8_t> silhouette_of(const core::Frame& frame, int alpha_threshold) {
    const std::size_t pixels = static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height);
    std::vector<std::uint8_t> mask(pixels, 0U);
    if (frame.rgba.size() < pixels * 4U) return mask;
    for (std::size_t index = 0; index < pixels; ++index) {
        mask[index] = frame.rgba[index * 4U + 3U] >= static_cast<std::uint8_t>(alpha_threshold) ? 1U : 0U;
    }
    return mask;
}

BoxI bounds_of(const std::vector<std::uint8_t>& mask, int width, int height) {
    BoxI box{width, height, -1, -1};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] == 0U) {
                continue;
            }
            box.min_x = std::min(box.min_x, x);
            box.min_y = std::min(box.min_y, y);
            box.max_x = std::max(box.max_x, x);
            box.max_y = std::max(box.max_y, y);
        }
    }
    return box;
}

Rig estimate_rig(const core::Frame& frame, int alpha_threshold) {
    if (frame.width <= 0 || frame.height <= 0) throw std::invalid_argument("estimate_rig: empty frame");
    const auto mask = silhouette_of(frame, alpha_threshold);
    const BoxI body = bounds_of(mask, frame.width, frame.height);
    if (!body.valid()) throw std::invalid_argument("estimate_rig: source sprite has no opaque pixels");

    Rig rig;
    rig.width = frame.width;
    rig.height = frame.height;
    rig.body = body;

    const int rows = body.height();
    const double height_d = static_cast<double>(rows);
    const RowProfile profile = build_row_profile(mask, frame.width, body);
    const auto row_of = [&](double fraction) {
        return std::clamp(static_cast<int>(std::lround(fraction * (height_d - 1.0))), 0, rows - 1);
    };
    const auto y_of = [&](int row) { return static_cast<double>(body.min_y + row); };

    // ---------------------------------------------------------------- waist
    // The waist is the narrowest row of the lower torso. It gives both the
    // torso half-width and a reliable body centre line.
    int waist_row = row_of(0.58);
    {
        int best_width = std::numeric_limits<int>::max();
        for (int row = row_of(0.45); row <= row_of(0.72); ++row) {
            if (profile.width[static_cast<std::size_t>(row)] > 0 &&
                profile.width[static_cast<std::size_t>(row)] < best_width) {
                best_width = profile.width[static_cast<std::size_t>(row)];
                waist_row = row;
            }
        }
    }
    const double torso_half_width =
        std::max(1.0, static_cast<double>(profile.width[static_cast<std::size_t>(waist_row)]) * 0.5);

    // Body centre line: median of the row centroids over the waist band, which
    // is the part of a standing figure least disturbed by arms and hair.
    double center_x = 0.0;
    {
        std::vector<double> centers;
        for (int row = row_of(0.50); row <= row_of(0.72); ++row) {
            if (profile.width[static_cast<std::size_t>(row)] > 0) {
                centers.push_back(profile.centroid[static_cast<std::size_t>(row)]);
            }
        }
        center_x = median_of(centers, static_cast<double>(body.min_x) + 0.5 * body.width());
    }

    // ------------------------------------------------------------- shoulders
    // The shoulder line is the row with the largest increase in silhouette
    // width in the upper body: that is where the arms join the head/neck
    // column. This replaces the old "topmost cluster of a column strip" rule
    // that put both arm pivots inside the hair.
    int shoulder_row = row_of(0.28);
    {
        const int window = std::max(1, static_cast<int>(std::lround(height_d * 0.03)));
        int best_gain = std::numeric_limits<int>::min();
        for (int row = row_of(0.12); row + window <= row_of(0.52); ++row) {
            const int gain = profile.width[static_cast<std::size_t>(row + window)] -
                             profile.width[static_cast<std::size_t>(row)];
            if (gain > best_gain) {
                best_gain = gain;
                shoulder_row = row + window;
            }
        }
        // A perfectly uniform silhouette (no detectable shoulder flare) falls
        // back to the anatomical default of 28% of body height.
        if (best_gain <= 0) shoulder_row = row_of(0.28);
    }
    const double shoulder_y = y_of(shoulder_row);

    // ------------------------------------------------------------------ head
    const int neck_row = std::max(1, shoulder_row - std::max(1, static_cast<int>(std::lround(height_d * 0.02))));
    double head_center_x = center_x;
    double head_radius = torso_half_width;
    {
        long long sum = 0;
        long long count = 0;
        int widest = 0;
        for (int row = 0; row <= neck_row; ++row) {
            const int y = body.min_y + row;
            widest = std::max(widest, profile.width[static_cast<std::size_t>(row)]);
            for (int x = body.min_x; x <= body.max_x; ++x) {
                if (mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.width) +
                         static_cast<std::size_t>(x)] != 0U) {
                    sum += x;
                    ++count;
                }
            }
        }
        if (count > 0) head_center_x = static_cast<double>(sum) / static_cast<double>(count);
        head_radius = std::max(1.0, static_cast<double>(widest) * 0.5);
    }
    const double neck_y = y_of(neck_row);
    // The head bone runs from the neck to a point just inside the skull so the
    // head rotates about the neck rather than about its own crown.
    const double head_top_y = y_of(0) + 0.18 * (neck_y - y_of(0));

    // ------------------------------------------------------------ hips/crotch
    // The crotch is the first row below the waist where the silhouette splits
    // into two runs; that is where the legs separate.
    int crotch_row = row_of(0.76);
    for (int row = waist_row; row <= row_of(0.92); ++row) {
        if (profile.runs[static_cast<std::size_t>(row)] >= 2) {
            crotch_row = row;
            break;
        }
    }
    const int hip_row = std::max(waist_row,
                                 crotch_row - std::max(1, static_cast<int>(std::lround(height_d * 0.04))));
    const double hip_y = y_of(hip_row);
    const double hip_center_x = profile.width[static_cast<std::size_t>(hip_row)] > 0
                                    ? profile.centroid[static_cast<std::size_t>(hip_row)]
                                    : center_x;
    const double hip_half_width =
        std::max(1.0, static_cast<double>(profile.width[static_cast<std::size_t>(hip_row)]) * 0.5);
    const double hip_offset = std::max(1.0, hip_half_width * 0.50);

    // ------------------------------------------------------------------ feet
    const double ground_y = y_of(rows - 1);
    const int foot_band = std::max(1, static_cast<int>(std::lround(height_d * 0.05)));
    double foot_back_x = hip_center_x - hip_offset;
    double foot_front_x = hip_center_x + hip_offset;
    {
        long long back_sum = 0;
        long long back_count = 0;
        long long front_sum = 0;
        long long front_count = 0;
        for (int row = rows - foot_band; row < rows; ++row) {
            if (row < 0) continue;
            const int y = body.min_y + row;
            for (int x = body.min_x; x <= body.max_x; ++x) {
                if (mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.width) +
                         static_cast<std::size_t>(x)] == 0U) {
                    continue;
                }
                if (static_cast<double>(x) < hip_center_x) {
                    back_sum += x;
                    ++back_count;
                } else {
                    front_sum += x;
                    ++front_count;
                }
            }
        }
        if (back_count > 0) foot_back_x = static_cast<double>(back_sum) / static_cast<double>(back_count);
        if (front_count > 0) foot_front_x = static_cast<double>(front_sum) / static_cast<double>(front_count);
    }

    // ----------------------------------------------------------------- knees
    const int knee_row = std::clamp((hip_row + rows - 1) / 2, hip_row + 1, rows - 1);
    double knee_back_x = 0.5 * (hip_center_x - hip_offset + foot_back_x);
    double knee_front_x = 0.5 * (hip_center_x + hip_offset + foot_front_x);
    {
        long long back_sum = 0;
        long long back_count = 0;
        long long front_sum = 0;
        long long front_count = 0;
        const int y = body.min_y + knee_row;
        for (int x = body.min_x; x <= body.max_x; ++x) {
            if (mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.width) +
                     static_cast<std::size_t>(x)] == 0U) {
                continue;
            }
            if (static_cast<double>(x) < hip_center_x) {
                back_sum += x;
                ++back_count;
            } else {
                front_sum += x;
                ++front_count;
            }
        }
        if (back_count > 0) knee_back_x = static_cast<double>(back_sum) / static_cast<double>(back_count);
        if (front_count > 0) knee_front_x = static_cast<double>(front_sum) / static_cast<double>(front_count);
    }
    const double knee_y = y_of(knee_row);
    const double leg_radius = std::max(1.0, 0.5 * hip_offset * 1.3);

    // ------------------------------------------------------------------ arms
    // The arm zone is everything between the shoulder line and the hip line
    // that lies outside the torso column. The glove is the thickest blob in
    // the distal half of that zone, located with a distance transform.
    const auto distance = distance_transform(mask, frame.width, frame.height);
    const double arm_top = shoulder_y - 0.06 * height_d;
    // The arm zone stops at the waist: below it the hips flare out and those
    // wide, thick pixels would otherwise out-score the gloves.
    const double arm_bottom = std::min(hip_y, y_of(waist_row) + 0.03 * height_d);
    const double torso_column = torso_half_width * 1.05;

    struct ArmResult {
        Vec2 shoulder{};
        Vec2 elbow{};
        Vec2 hand{};
        double glove_radius = 1.0;
        double arm_radius = 1.0;
    };

    const double shoulder_offset = std::max(1.0, torso_half_width * 0.92);
    const auto solve_arm = [&](int sign) {
        ArmResult arm;
        arm.shoulder = Vec2{center_x + sign * shoulder_offset, shoulder_y};
        // Collect the arm-zone pixels for this side.
        double farthest = 0.0;
        std::vector<double> thickness;
        for (int y = static_cast<int>(std::floor(arm_top)); y <= static_cast<int>(std::ceil(arm_bottom)); ++y) {
            if (y < body.min_y || y > body.max_y) continue;
            for (int x = body.min_x; x <= body.max_x; ++x) {
                const double dx = static_cast<double>(x) - center_x;
                if (sign < 0 ? dx > -torso_column : dx < torso_column) continue;
                if (mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.width) +
                         static_cast<std::size_t>(x)] == 0U) {
                    continue;
                }
                const double ddx = static_cast<double>(x) - arm.shoulder.x;
                const double ddy = static_cast<double>(y) - arm.shoulder.y;
                farthest = std::max(farthest, std::sqrt(ddx * ddx + ddy * ddy));
                thickness.push_back(distance[static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.width) +
                                             static_cast<std::size_t>(x)]);
            }
        }
        if (farthest <= 0.0) {
            // No arm pixels outside the torso column (e.g. arms tucked in):
            // fall back to a proportional guess so the rig stays complete.
            arm.hand = Vec2{center_x + sign * torso_half_width * 1.6, shoulder_y + 0.18 * height_d};
            arm.elbow = Vec2{0.5 * (arm.shoulder.x + arm.hand.x), 0.5 * (arm.shoulder.y + arm.hand.y)};
            arm.glove_radius = std::max(1.0, torso_half_width * 0.45);
            arm.arm_radius = std::max(1.0, arm.glove_radius * 0.6);
            return arm;
        }
        // Distal half of the arm zone: the glove lives there.
        double best_score = -1.0;
        Vec2 best_point{arm.shoulder.x + sign * torso_half_width, shoulder_y + 0.15 * height_d};
        for (int y = static_cast<int>(std::floor(arm_top)); y <= static_cast<int>(std::ceil(arm_bottom)); ++y) {
            if (y < body.min_y || y > body.max_y) continue;
            for (int x = body.min_x; x <= body.max_x; ++x) {
                const double dx = static_cast<double>(x) - center_x;
                if (sign < 0 ? dx > -torso_column : dx < torso_column) continue;
                const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.width) +
                                          static_cast<std::size_t>(x);
                if (mask[index] == 0U) continue;
                const double ddx = static_cast<double>(x) - arm.shoulder.x;
                const double ddy = static_cast<double>(y) - arm.shoulder.y;
                const double reach = std::sqrt(ddx * ddx + ddy * ddy);
                if (reach < farthest * 0.45) continue;
                // Prefer thick pixels, break ties towards the extremity. The
                // comparison is strict so the scan order (top-left to
                // bottom-right) makes the result deterministic.
                const double lateral = std::abs(dx);
                const double score = distance[index] + 0.20 * reach + 0.20 * lateral;
                if (score > best_score) {
                    best_score = score;
                    best_point = Vec2{static_cast<double>(x), static_cast<double>(y)};
                }
            }
        }
        arm.hand = best_point;
        arm.glove_radius = std::max(
            1.0, distance[static_cast<std::size_t>(std::lround(best_point.y)) * static_cast<std::size_t>(frame.width) +
                          static_cast<std::size_t>(std::lround(best_point.x))]);
        arm.arm_radius = std::max(1.0, std::min(arm.glove_radius * 0.75, median_of(thickness, arm.glove_radius * 0.6)));
        // Elbow: halfway along the arm, nudged outwards so the limb has a
        // natural bend instead of being a straight stick.
        const double mid_x = 0.5 * (arm.shoulder.x + arm.hand.x);
        const double mid_y = 0.5 * (arm.shoulder.y + arm.hand.y);
        const double span_x = arm.hand.x - arm.shoulder.x;
        const double span_y = arm.hand.y - arm.shoulder.y;
        const double span = std::max(1.0, std::sqrt(span_x * span_x + span_y * span_y));
        arm.elbow = Vec2{mid_x + sign * 0.12 * span, mid_y};
        return arm;
    };

    const ArmResult back_arm = solve_arm(-1);
    const ArmResult front_arm = solve_arm(+1);

    // ------------------------------------------------------------ assemble
    rig.joint(JointId::Pelvis) = Vec2{hip_center_x, hip_y};
    rig.joint(JointId::Neck) = Vec2{0.5 * (center_x + head_center_x), neck_y};
    rig.joint(JointId::HeadTop) = Vec2{head_center_x, head_top_y};
    rig.joint(JointId::ShoulderBack) = back_arm.shoulder;
    rig.joint(JointId::ElbowBack) = back_arm.elbow;
    rig.joint(JointId::HandBack) = back_arm.hand;
    rig.joint(JointId::ShoulderFront) = front_arm.shoulder;
    rig.joint(JointId::ElbowFront) = front_arm.elbow;
    rig.joint(JointId::HandFront) = front_arm.hand;
    rig.joint(JointId::HipBack) = Vec2{hip_center_x - hip_offset, hip_y};
    rig.joint(JointId::KneeBack) = Vec2{knee_back_x, knee_y};
    rig.joint(JointId::FootBack) = Vec2{foot_back_x, ground_y};
    rig.joint(JointId::HipFront) = Vec2{hip_center_x + hip_offset, hip_y};
    rig.joint(JointId::KneeFront) = Vec2{knee_front_x, knee_y};
    rig.joint(JointId::FootFront) = Vec2{foot_front_x, ground_y};

    rig.pivot = Vec2{0.5 * (foot_back_x + foot_front_x), ground_y};

    const double shoulder_span = std::max(torso_half_width, shoulder_offset * 1.15);
    const double neck_radius = std::max(1.0, head_radius * 0.62);
    const auto set_part = [&](PartId id, JointId from, JointId to, double r_from, double r_to, double bias) {
        rig.parts[static_cast<std::size_t>(id)] = PartSpec{id, from, to, r_from, r_to, bias};
    };
    set_part(PartId::Torso, JointId::Pelvis, JointId::Neck, hip_half_width * 1.02, shoulder_span, 0.04);
    set_part(PartId::Head, JointId::Neck, JointId::HeadTop, neck_radius, head_radius, 0.10);
    set_part(PartId::BackUpperArm, JointId::ShoulderBack, JointId::ElbowBack, back_arm.arm_radius * 1.15,
             back_arm.arm_radius, 0.0);
    set_part(PartId::BackForearm, JointId::ElbowBack, JointId::HandBack, back_arm.arm_radius,
             back_arm.arm_radius * 1.1, 0.0);
    set_part(PartId::BackGlove, JointId::HandBack, JointId::HandBack, back_arm.glove_radius, back_arm.glove_radius,
             0.22);
    set_part(PartId::FrontUpperArm, JointId::ShoulderFront, JointId::ElbowFront, front_arm.arm_radius * 1.15,
             front_arm.arm_radius, 0.0);
    set_part(PartId::FrontForearm, JointId::ElbowFront, JointId::HandFront, front_arm.arm_radius,
             front_arm.arm_radius * 1.1, 0.0);
    set_part(PartId::FrontGlove, JointId::HandFront, JointId::HandFront, front_arm.glove_radius,
             front_arm.glove_radius, 0.22);
    set_part(PartId::BackThigh, JointId::HipBack, JointId::KneeBack, leg_radius * 1.2, leg_radius, 0.0);
    set_part(PartId::BackShin, JointId::KneeBack, JointId::FootBack, leg_radius, leg_radius * 1.1, 0.0);
    set_part(PartId::FrontThigh, JointId::HipFront, JointId::KneeFront, leg_radius * 1.2, leg_radius, 0.0);
    set_part(PartId::FrontShin, JointId::KneeFront, JointId::FootFront, leg_radius, leg_radius * 1.1, 0.0);

    return rig;
}

nlohmann::json rig_to_json(const Rig& rig) {
    nlohmann::json document;
    document["schema_version"] = 1;
    document["format"] = "spratforge-rig";
    document["source_size"] = nlohmann::json::array({rig.width, rig.height});
    document["body_bounds"] = nlohmann::json{{"x", rig.body.min_x},
                                             {"y", rig.body.min_y},
                                             {"width", rig.body.width()},
                                             {"height", rig.body.height()}};
    document["pivot"] = nlohmann::json::array({rig.pivot.x, rig.pivot.y});
    nlohmann::json joints = nlohmann::json::object();
    for (int index = 0; index < kJointCount; ++index) {
        const auto id = static_cast<JointId>(index);
        joints[joint_name(id)] = nlohmann::json::array({rig.joint(id).x, rig.joint(id).y});
    }
    document["joints"] = joints;
    nlohmann::json parts = nlohmann::json::array();
    for (int index = 0; index < kPartCount; ++index) {
        const auto& spec = rig.parts[static_cast<std::size_t>(index)];
        parts.push_back(nlohmann::json{{"name", part_name(spec.id)},
                                       {"from", joint_name(spec.from)},
                                       {"to", joint_name(spec.to)},
                                       {"radius_from", spec.radius_from},
                                       {"radius_to", spec.radius_to},
                                       {"z_order", part_z_order(spec.id)}});
    }
    document["parts"] = parts;
    return document;
}

Rig apply_rig_override(const Rig& rig, const nlohmann::json& override_document) {
    Rig result = rig;
    if (override_document.is_null()) return result;
    if (!override_document.is_object()) throw std::invalid_argument("rig override must be a JSON object");
    for (auto entry = override_document.begin(); entry != override_document.end(); ++entry) {
        const std::string& key = entry.key();
        if (key != "joints" && key != "pivot" && key != "radii" && key != "schema_version" && key != "format" &&
            key != "source_size" && key != "body_bounds" && key != "parts") {
            throw std::invalid_argument("rig override: unknown key '" + key + "'");
        }
    }
    const auto read_point = [](const nlohmann::json& value, const std::string& where) {
        if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number()) {
            throw std::invalid_argument("rig override: " + where + " must be [x, y]");
        }
        return Vec2{value[0].get<double>(), value[1].get<double>()};
    };
    if (override_document.contains("joints")) {
        const auto& joints = override_document.at("joints");
        if (!joints.is_object()) throw std::invalid_argument("rig override: 'joints' must be an object");
        for (auto entry = joints.begin(); entry != joints.end(); ++entry) {
            const JointId id = joint_from_name(entry.key());
            if (id == JointId::Count) throw std::invalid_argument("rig override: unknown joint '" + entry.key() + "'");
            result.joint(id) = read_point(entry.value(), "joints." + entry.key());
        }
    }
    if (override_document.contains("pivot")) {
        result.pivot = read_point(override_document.at("pivot"), "pivot");
    }
    if (override_document.contains("radii")) {
        const auto& radii = override_document.at("radii");
        if (!radii.is_object()) throw std::invalid_argument("rig override: 'radii' must be an object");
        for (auto entry = radii.begin(); entry != radii.end(); ++entry) {
            bool matched = false;
            for (int index = 0; index < kPartCount; ++index) {
                auto& spec = result.parts[static_cast<std::size_t>(index)];
                if (entry.key() != part_name(spec.id)) continue;
                matched = true;
                const auto& value = entry.value();
                if (!value.is_object()) throw std::invalid_argument("rig override: radii." + entry.key() + " must be an object");
                if (value.contains("from")) spec.radius_from = std::max(0.5, value.at("from").get<double>());
                if (value.contains("to")) spec.radius_to = std::max(0.5, value.at("to").get<double>());
            }
            if (!matched) throw std::invalid_argument("rig override: unknown part '" + entry.key() + "'");
        }
    }
    return result;
}

Rig load_rig_override(const Rig& rig, const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::invalid_argument("Unable to open rig override '" + path + "'");
    nlohmann::json document;
    input >> document;
    return apply_rig_override(rig, document);
}

}  // namespace spratforge::forge
