#pragma once

// spratforge::forge — pixel synthesis.
//
// Rendering is BACKWARD mapped: for every destination pixel the renderer
// inverts the part transform, lands on a source pixel and copies it. Phase 1
// pushed source pixels forward into the destination, which left a growing
// field of holes as soon as a limb was rotated or scaled. Backward mapping
// cannot tear by construction, because every destination pixel is visited
// exactly once.
//
// Parts are composited with the painter's algorithm using part_z_order(), so
// the rear limbs land behind the torso and the lead limbs in front of it.

#include <array>
#include <vector>

#include <spratforge/core/palette_core.hpp>
#include <spratforge/forge/forge_motion.hpp>
#include <spratforge/forge/forge_rig.hpp>
#include <spratforge/forge/forge_segment.hpp>

namespace spratforge::forge {

// Affine map from source space to posed source space:
//   dst = dst_origin + M * (src - src_origin)
struct PartTransform {
    double m00 = 1.0, m01 = 0.0, m10 = 0.0, m11 = 1.0;
    double i00 = 1.0, i01 = 0.0, i10 = 0.0, i11 = 1.0;  // inverse of M
    Vec2 src_origin{};
    Vec2 dst_origin{};
    bool valid = false;
};

using PartTransforms = std::array<PartTransform, kPartCount>;

PartTransforms compute_part_transforms(const Rig& rest, const PosedRig& posed);

// Axis-aligned bounds, in source space, covered by a posed sprite.
struct BoxD {
    double min_x = 0.0;
    double min_y = 0.0;
    double max_x = -1.0;
    double max_y = -1.0;
    [[nodiscard]] bool valid() const { return max_x >= min_x && max_y >= min_y; }
    void extend(double x, double y);
    void extend(const BoxD& other);
};

BoxD posed_bounds(const Segmentation& segmentation, const PartTransforms& transforms);

// A single canvas shared by every frame of every animation, so the character
// never jitters when the game switches clips.
struct CanvasLayout {
    int width = 0;
    int height = 0;
    int origin_x = 0;  // source_x = canvas_x + origin_x
    int origin_y = 0;
    int anchor_x = 0;  // the rest pivot (ground contact), in canvas pixels
    int anchor_y = 0;
};

CanvasLayout compute_canvas(const Rig& rig, const BoxD& union_bounds, int margin);

// Sampling / clean-up controls for render_pose(). Defaults reproduce the
// Phase 4A quality pass; RenderQuality::legacy() reproduces the Phase 2/3
// nearest-neighbour behaviour exactly (used by the regression tests).
struct RenderQuality {
    // Sub-samples per destination axis. 1 == plain nearest neighbour. Higher
    // values remove the dropped rows/columns ("striping") a rotated or
    // minified limb used to show, because a destination pixel that only
    // partially covers a source pixel is still resolved.
    int supersample = 3;
    // Minimum covered fraction of a destination pixel before it is written.
    // 0.5 keeps the silhouette the same size as the source (no edge creep).
    double coverage_threshold = 0.5;
    // When false the winning sub-sample colour is copied verbatim, so the
    // output palette is a strict subset of the source palette. When true the
    // covered sub-samples are averaged in PREMULTIPLIED space and the pixel
    // alpha becomes the coverage, which gives soft edges at the cost of new
    // colours. Premultiplication is what stops the classic dark/bright halo.
    bool soft_edges = false;
    // Fill 1 px pinholes left between two adjacent parts after compositing.
    bool fill_pinholes = true;
    // Drop isolated specks (an opaque pixel with <= 1 opaque 8-neighbour).
    bool despeckle = true;

    [[nodiscard]] static RenderQuality legacy() {
        RenderQuality q;
        q.supersample = 1;
        q.coverage_threshold = 0.0;
        q.soft_edges = false;
        q.fill_pinholes = false;
        q.despeckle = false;
        return q;
    }
};

// Renders one posed frame onto `layout`.
core::Frame render_pose(const core::Frame& source,
                        const Rig& rig,
                        const Segmentation& segmentation,
                        const PosedRig& posed,
                        const CanvasLayout& layout,
                        const RenderQuality& quality = RenderQuality{});

// Morphological clean-up applied after compositing. Exposed for tests.
// `fill_pinholes` closes transparent pixels surrounded by >= 6 opaque
// 8-neighbours using the modal neighbour colour; `despeckle` erases opaque
// pixels with <= 1 opaque 8-neighbour. Both use a 3x3 8-connected
// structuring element and are deliberately conservative so a 1 px limb is
// never eroded.
void clean_frame(core::Frame& frame, const RenderQuality& quality);

// Integer nearest-neighbour magnification (scale >= 1).
core::Frame upscale_nearest(const core::Frame& frame, int scale);

// Horizontal strip sheet; every cell has the frame's dimensions.
core::Frame build_strip_sheet(const std::vector<core::Frame>& frames);

}  // namespace spratforge::forge
