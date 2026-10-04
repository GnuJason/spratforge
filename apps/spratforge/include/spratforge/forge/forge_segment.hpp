#pragma once

// spratforge::forge — rig-driven body part segmentation.
//
// Phase 1's renderer classified pixels with hard-coded geometric slabs
// ("everything above y = 0.32 * H is the head"), which made the Gloves region
// unreachable and broke as soon as the sprite proportions changed. This module
// derives the segmentation from the rig instead: every part is a tapered
// capsule between two joints, and each silhouette pixel is awarded to the
// capsule with the smallest *normalized* signed distance. Correcting the rig
// therefore automatically corrects the segmentation.

#include <array>
#include <cstdint>
#include <vector>

#include <spratforge/core/palette_core.hpp>
#include <spratforge/forge/forge_rig.hpp>

namespace spratforge::forge {

struct Segmentation {
    int width = 0;
    int height = 0;

    // 1 where the source sprite is opaque.
    std::vector<std::uint8_t> silhouette;

    // Exclusive ownership map: index of the owning PartId, or -1 when the
    // pixel is transparent. Used for the debug overlay and for statistics.
    std::vector<std::int8_t> owner;

    // Exclusive per-part masks (owner == part).
    std::array<std::vector<std::uint8_t>, kPartCount> masks{};

    // Sampling masks: the exclusive mask grown by `overlap` pixels and clipped
    // back to the silhouette. Neighbouring parts therefore share a band of
    // pixels around each joint, which is what keeps the sockets filled when
    // the limbs rotate instead of opening a wedge-shaped hole.
    std::array<std::vector<std::uint8_t>, kPartCount> sample_masks{};

    // Bounding box of each sampling mask (invalid when the part is empty).
    std::array<BoxI, kPartCount> bounds{};

    // Pixel count of each exclusive mask.
    std::array<int, kPartCount> pixel_counts{};

    // Dilation radius actually used, in source pixels (>= 1).
    int overlap = 1;

    [[nodiscard]] bool empty_part(PartId part) const {
        return pixel_counts[static_cast<std::size_t>(part)] == 0;
    }
};

// Segments `frame` using `rig`. Deterministic: ties are broken by PartId order.
Segmentation segment_body(const core::Frame& frame, const Rig& rig, int alpha_threshold = 16);

// Renders a human-readable debug overlay: the source sprite dimmed, tinted by
// part ownership, with bones and joint markers drawn on top. `scale` is an
// integer nearest-neighbour magnification applied at the end so that joints
// stay visible on tiny sprites.
core::Frame render_rig_overlay(const core::Frame& frame,
                               const Rig& rig,
                               const Segmentation& segmentation,
                               int scale = 1);

}  // namespace spratforge::forge
