// Phase 2 forge tests: rig sanity, segmentation coverage, normalized motion
// scaling and frame distinctness.
#include <spratforge/core/renderer_core.hpp>
#include <spratforge/forge/forge_motion.hpp>
#include <spratforge/forge/forge_rig.hpp>
#include <spratforge/forge/forge_segment.hpp>
#include <spratforge/forge/forge_synth.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

spratforge::core::Frame load(const std::string& path) {
    spratforge::core::Frame frame;
    std::string error;
    if (!spratforge::core::load_frame_png(path, frame, error)) {
        std::cerr << "FAIL: cannot load " << path << ": " << error << '\n';
        ++failures;
    }
    return frame;
}

}  // namespace

int main() {
    using namespace spratforge::forge;

    const spratforge::core::Frame master = load("../../../RingQueen/assets/master_boxer.png");
    if (master.width == 0) return 1;

    const Rig rig = estimate_rig(master);
    const double height = static_cast<double>(rig.body.height());

    // --- rig sanity -------------------------------------------------------
    const Vec2 head_top = rig.joint(JointId::HeadTop);
    const Vec2 neck = rig.joint(JointId::Neck);
    const Vec2 shoulder_back = rig.joint(JointId::ShoulderBack);
    const Vec2 shoulder_front = rig.joint(JointId::ShoulderFront);
    const Vec2 hand_back = rig.joint(JointId::HandBack);
    const Vec2 hand_front = rig.joint(JointId::HandFront);
    const Vec2 pelvis = rig.joint(JointId::Pelvis);

    check(head_top.y < neck.y, "head top above neck");
    check(neck.y <= shoulder_back.y + 1.0, "neck at or above the shoulder line");
    check(shoulder_back.x < shoulder_front.x, "back shoulder left of front shoulder");
    check(pelvis.y > shoulder_back.y, "pelvis below shoulders");
    check(rig.joint(JointId::KneeBack).y > rig.joint(JointId::HipBack).y, "knee below hip");
    check(rig.joint(JointId::FootBack).y > rig.joint(JointId::KneeBack).y, "foot below knee");
    check(std::abs(rig.pivot.y - static_cast<double>(rig.body.max_y)) <= 1.0, "pivot on the ground line");

    // Regression for the Phase 1 bug: arm joints must not sit in the hair.
    const double hair_floor = static_cast<double>(rig.body.min_y) + 0.22 * height;
    check(hand_back.y > hair_floor, "back hand below the hair band");
    check(hand_front.y > hair_floor, "front hand below the hair band");
    check(shoulder_back.y > hair_floor, "back shoulder below the hair band");
    check(shoulder_front.y > hair_floor, "front shoulder below the hair band");
    // Hands are lateral: outside the shoulder span.
    check(hand_back.x <= shoulder_back.x, "back hand outside the back shoulder");
    check(hand_front.x >= shoulder_front.x, "front hand outside the front shoulder");

    // --- segmentation -----------------------------------------------------
    const Segmentation seg = segment_body(master, rig);
    std::size_t assigned = 0;
    std::size_t silhouette = 0;
    for (std::size_t i = 0; i < seg.silhouette.size(); ++i) {
        if (seg.silhouette[i] == 0U) continue;
        ++silhouette;
        if (seg.owner[i] >= 0) ++assigned;
    }
    check(silhouette > 0, "silhouette is non-empty");
    check(assigned == silhouette, "every silhouette pixel is owned by a part");
    // Phase 1 bug: the gloves region was unreachable (0 px).
    check(seg.pixel_counts[static_cast<std::size_t>(PartId::BackGlove)] > 0, "back glove region reachable");
    check(seg.pixel_counts[static_cast<std::size_t>(PartId::FrontGlove)] > 0, "front glove region reachable");
    check(seg.pixel_counts[static_cast<std::size_t>(PartId::Head)] > 0, "head region reachable");
    check(seg.pixel_counts[static_cast<std::size_t>(PartId::Torso)] > 0, "torso region reachable");

    // --- rig override -----------------------------------------------------
    {
        nlohmann::json doc;
        doc["joints"]["hand_front"] = {1.0, 2.0};
        doc["pivot"] = {3.0, 4.0};
        const Rig overridden = apply_rig_override(rig, doc);
        check(overridden.joint(JointId::HandFront).x == 1.0 && overridden.joint(JointId::HandFront).y == 2.0,
              "override moves the front hand");
        check(overridden.pivot.x == 3.0 && overridden.pivot.y == 4.0, "override moves the pivot");
    }
    {
        nlohmann::json doc;
        doc["jnts"] = nlohmann::json::object();
        bool threw = false;
        try {
            (void)apply_rig_override(rig, doc);
        } catch (const std::exception&) {
            threw = true;
        }
        check(threw, "override rejects unknown keys");
    }

    // --- normalized motion scales with the rig ----------------------------
    const spratforge::core::Frame small = load("tests/fixtures/neutral_boxer.png");
    if (small.width == 0) return 1;
    const Rig small_rig = estimate_rig(small);
    const double small_height = static_cast<double>(small_rig.body.height());
    check(small_height > 0.0, "small rig has a body height");

    const std::vector<Clip> clips = default_boxer_clips();
    const Clip* jab = nullptr;
    for (const Clip& clip : clips) {
        if (clip.name == "jab") jab = &clip;
    }
    check(jab != nullptr, "jab clip exists");
    if (jab != nullptr) {
        const PoseSpec impact = sample_clip(*jab, jab->impact_frame);
        const PosedRig big = apply_pose(rig, impact);
        const PosedRig tiny = apply_pose(small_rig, impact);
        const double big_travel = big.joints[static_cast<std::size_t>(JointId::HandFront)].x - hand_front.x;
        const double tiny_travel = tiny.joints[static_cast<std::size_t>(JointId::HandFront)].x -
                                   small_rig.joint(JointId::HandFront).x;
        const double big_norm = big_travel / height;
        const double tiny_norm = tiny_travel / small_height;
        check(std::abs(big_travel) > 0.05 * height, "jab impact moves the front hand at master scale");
        check(std::abs(big_norm - tiny_norm) < 0.08,
              "jab hand travel is proportional across rig sizes");
    }

    // --- frames are visibly distinct where motion happens -----------------
    const Segmentation small_seg = segment_body(small, small_rig);
    (void)small_seg;
    for (const Clip& clip : clips) {
        std::set<std::string> signatures;
        for (int index = 0; index < clip.frame_count; ++index) {
            PosedRig posed = apply_pose(rig, sample_clip(clip, index));
            if (clip.ground_clamp) clamp_to_ground(rig, posed);
            std::string signature;
            for (const Vec2& joint : posed.joints) {
                signature += std::to_string(std::lround(joint.x * 4.0));
                signature += ',';
                signature += std::to_string(std::lround(joint.y * 4.0));
                signature += ';';
            }
            signatures.insert(signature);
        }
        // Allow the deliberate holds at the tail of knockdown/ko.
        const std::size_t expected = clip.hold_last_frame
                                         ? static_cast<std::size_t>(clip.frame_count) / 2
                                         : static_cast<std::size_t>(clip.frame_count) - 1;
        check(signatures.size() >= expected, clip.name + " produces distinct poses");
    }

    if (failures == 0) std::cout << "test_forge: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
