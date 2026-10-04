#include <spratforge/forge/forge_synth.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace spratforge::forge {
namespace {

Vec2 sub(Vec2 a, Vec2 b) { return Vec2{a.x - b.x, a.y - b.y}; }
double length(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

PartTransform make_transform(Vec2 src_origin, Vec2 dst_origin, double scale, double cos_t, double sin_t) {
    PartTransform transform;
    transform.src_origin = src_origin;
    transform.dst_origin = dst_origin;
    transform.m00 = scale * cos_t;
    transform.m01 = -scale * sin_t;
    transform.m10 = scale * sin_t;
    transform.m11 = scale * cos_t;
    const double det = transform.m00 * transform.m11 - transform.m01 * transform.m10;
    if (std::fabs(det) < 1e-12) {
        transform.valid = false;
        return transform;
    }
    transform.i00 = transform.m11 / det;
    transform.i01 = -transform.m01 / det;
    transform.i10 = -transform.m10 / det;
    transform.i11 = transform.m00 / det;
    transform.valid = true;
    return transform;
}

Vec2 apply_forward(const PartTransform& transform, double x, double y) {
    const double dx = x - transform.src_origin.x;
    const double dy = y - transform.src_origin.y;
    return Vec2{transform.dst_origin.x + transform.m00 * dx + transform.m01 * dy,
                transform.dst_origin.y + transform.m10 * dx + transform.m11 * dy};
}

Vec2 apply_inverse(const PartTransform& transform, double x, double y) {
    const double dx = x - transform.dst_origin.x;
    const double dy = y - transform.dst_origin.y;
    return Vec2{transform.src_origin.x + transform.i00 * dx + transform.i01 * dy,
                transform.src_origin.y + transform.i10 * dx + transform.i11 * dy};
}

std::uint8_t to_u8(double v) {
    const double rounded = std::floor(v + 0.5);
    return static_cast<std::uint8_t>(std::clamp(rounded, 0.0, 255.0));
}

int effective_z_order(PartId part, bool promote_back_arm) {
    const int base = part_z_order(part);
    if (!promote_back_arm) {
        return base;
    }
    switch (part) {
        case PartId::BackUpperArm: return 70;
        case PartId::BackForearm: return 71;
        case PartId::BackGlove: return 72;
        default: return base;
    }
}

}  // namespace

void BoxD::extend(double x, double y) {
    if (!valid()) {
        min_x = max_x = x;
        min_y = max_y = y;
        return;
    }
    min_x = std::min(min_x, x);
    max_x = std::max(max_x, x);
    min_y = std::min(min_y, y);
    max_y = std::max(max_y, y);
}

void BoxD::extend(const BoxD& other) {
    if (!other.valid()) {
        return;
    }
    extend(other.min_x, other.min_y);
    extend(other.max_x, other.max_y);
}

PartTransforms compute_part_transforms(const Rig& rest, const PosedRig& posed) {
    PartTransforms transforms{};
    for (int i = 0; i < kPartCount; ++i) {
        const PartSpec& spec = rest.parts[static_cast<std::size_t>(i)];
        const PartId part = static_cast<PartId>(i);

        if (spec.from != spec.to) {
            const Vec2 a = rest.joint(spec.from);
            const Vec2 b = rest.joint(spec.to);
            const Vec2 pa = posed.joint(spec.from);
            const Vec2 pb = posed.joint(spec.to);
            const Vec2 v = sub(b, a);
            const Vec2 pv = sub(pb, pa);
            const double len = length(v);
            const double plen = length(pv);
            if (len < 1e-6) {
                transforms[static_cast<std::size_t>(i)] = make_transform(a, pa, 1.0, 1.0, 0.0);
                continue;
            }
            // Bone length changes are rare (IK clamps keep them small) but a
            // pure rotation is used when the posed bone collapses, so the part
            // never vanishes.
            double scale = plen < 1e-6 ? 1.0 : plen / len;
            scale = std::clamp(scale, 0.25, 4.0);
            const double angle = std::atan2(pv.y, pv.x) - std::atan2(v.y, v.x);
            transforms[static_cast<std::size_t>(i)] =
                make_transform(a, pa, scale, std::cos(angle), std::sin(angle));
            continue;
        }

        // Degenerate bone: the gloves. They ride on the hand joint, rotate with
        // the forearm and scale with the authored foreshortening factor.
        const JointId hand = spec.from;
        const bool is_front = (part == PartId::FrontGlove);
        const JointId elbow = is_front ? JointId::ElbowFront : JointId::ElbowBack;
        const Vec2 rest_v = sub(rest.joint(hand), rest.joint(elbow));
        const Vec2 posed_v = sub(posed.joint(hand), posed.joint(elbow));
        double angle = 0.0;
        if (length(rest_v) > 1e-6 && length(posed_v) > 1e-6) {
            angle = std::atan2(posed_v.y, posed_v.x) - std::atan2(rest_v.y, rest_v.x);
        }
        const double glove_scale =
            std::clamp(is_front ? posed.front_glove_scale : posed.back_glove_scale, 0.25, 4.0);
        transforms[static_cast<std::size_t>(i)] = make_transform(
            rest.joint(hand), posed.joint(hand), glove_scale, std::cos(angle), std::sin(angle));
    }
    return transforms;
}

BoxD posed_bounds(const Segmentation& segmentation, const PartTransforms& transforms) {
    BoxD bounds;
    for (int i = 0; i < kPartCount; ++i) {
        const BoxI& source_box = segmentation.bounds[static_cast<std::size_t>(i)];
        const PartTransform& transform = transforms[static_cast<std::size_t>(i)];
        if (!source_box.valid() || !transform.valid) {
            continue;
        }
        const double xs[2] = {static_cast<double>(source_box.min_x),
                              static_cast<double>(source_box.max_x) + 1.0};
        const double ys[2] = {static_cast<double>(source_box.min_y),
                              static_cast<double>(source_box.max_y) + 1.0};
        for (int cy = 0; cy < 2; ++cy) {
            for (int cx = 0; cx < 2; ++cx) {
                const Vec2 corner = apply_forward(transform, xs[cx], ys[cy]);
                bounds.extend(corner.x, corner.y);
            }
        }
    }
    return bounds;
}

CanvasLayout compute_canvas(const Rig& rig, const BoxD& union_bounds, int margin) {
    BoxD bounds = union_bounds;
    // Always keep the ground contact point inside the canvas, even for clips
    // that never touch it.
    bounds.extend(rig.pivot.x, rig.pivot.y);
    if (!bounds.valid()) {
        throw std::invalid_argument("forge: cannot size a canvas from an empty sprite");
    }

    CanvasLayout layout;
    layout.origin_x = static_cast<int>(std::floor(bounds.min_x)) - margin;
    layout.origin_y = static_cast<int>(std::floor(bounds.min_y)) - margin;
    layout.width = static_cast<int>(std::ceil(bounds.max_x)) + margin - layout.origin_x;
    layout.height = static_cast<int>(std::ceil(bounds.max_y)) + margin - layout.origin_y;
    layout.width = std::max(1, layout.width);
    layout.height = std::max(1, layout.height);
    layout.anchor_x = static_cast<int>(std::lround(rig.pivot.x)) - layout.origin_x;
    layout.anchor_y = static_cast<int>(std::lround(rig.pivot.y)) - layout.origin_y;
    return layout;
}

core::Frame render_pose(const core::Frame& source,
                        const Rig& rig,
                        const Segmentation& segmentation,
                        const PosedRig& posed,
                        const CanvasLayout& layout,
                        const RenderQuality& quality) {
    const int samples = std::clamp(quality.supersample, 1, 8);
    const double inv_samples = 1.0 / static_cast<double>(samples);
    const double sample_count = static_cast<double>(samples) * samples;
    const double coverage_threshold = std::clamp(quality.coverage_threshold, 0.0, 1.0);

    core::Frame output;
    output.width = layout.width;
    output.height = layout.height;
    output.rgba.assign(static_cast<std::size_t>(layout.width) * layout.height * 4, 0);

    const PartTransforms transforms = compute_part_transforms(rig, posed);

    // Stable painter's order: z first, PartId second so ties never depend on
    // container iteration order.
    std::array<int, kPartCount> order{};
    for (int i = 0; i < kPartCount; ++i) {
        order[static_cast<std::size_t>(i)] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const int za = effective_z_order(static_cast<PartId>(a), posed.promote_back_arm);
        const int zb = effective_z_order(static_cast<PartId>(b), posed.promote_back_arm);
        if (za != zb) {
            return za < zb;
        }
        return a < b;
    });

    for (const int part_index : order) {
        const std::size_t part = static_cast<std::size_t>(part_index);
        const PartTransform& transform = transforms[part];
        const BoxI& source_box = segmentation.bounds[part];
        if (!transform.valid || !source_box.valid()) {
            continue;
        }
        const std::vector<std::uint8_t>& mask = segmentation.sample_masks[part];

        // Destination bounding box from the transformed source box corners.
        BoxD dest;
        const double xs[2] = {static_cast<double>(source_box.min_x),
                              static_cast<double>(source_box.max_x) + 1.0};
        const double ys[2] = {static_cast<double>(source_box.min_y),
                              static_cast<double>(source_box.max_y) + 1.0};
        for (int cy = 0; cy < 2; ++cy) {
            for (int cx = 0; cx < 2; ++cx) {
                const Vec2 corner = apply_forward(transform, xs[cx], ys[cy]);
                dest.extend(corner.x, corner.y);
            }
        }
        if (!dest.valid()) {
            continue;
        }

        int x0 = static_cast<int>(std::floor(dest.min_x)) - layout.origin_x - 1;
        int y0 = static_cast<int>(std::floor(dest.min_y)) - layout.origin_y - 1;
        int x1 = static_cast<int>(std::ceil(dest.max_x)) - layout.origin_x + 1;
        int y1 = static_cast<int>(std::ceil(dest.max_y)) - layout.origin_y + 1;
        x0 = std::max(0, x0);
        y0 = std::max(0, y0);
        x1 = std::min(layout.width - 1, x1);
        y1 = std::min(layout.height - 1, y1);

        // Scratch buffers for the per-pixel sub-sample vote. Declared outside
        // the loop so the hot path does not allocate.
        std::vector<std::uint32_t> hit_colors;
        std::vector<int> hit_counts;
        hit_colors.reserve(static_cast<std::size_t>(samples) * samples);
        hit_counts.reserve(static_cast<std::size_t>(samples) * samples);

        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                hit_colors.clear();
                hit_counts.clear();
                int covered = 0;
                double acc_r = 0.0;
                double acc_g = 0.0;
                double acc_b = 0.0;
                double acc_a = 0.0;

                for (int sj = 0; sj < samples; ++sj) {
                    // Sub-sample centres, so a 1x1 grid degenerates to the
                    // original pixel-centre nearest-neighbour tap.
                    const double world_y =
                        y + (sj + 0.5) * inv_samples + layout.origin_y;
                    for (int si = 0; si < samples; ++si) {
                        const double world_x =
                            x + (si + 0.5) * inv_samples + layout.origin_x;
                        const Vec2 src = apply_inverse(transform, world_x, world_y);
                        const int sx = static_cast<int>(std::floor(src.x));
                        const int sy = static_cast<int>(std::floor(src.y));
                        if (sx < 0 || sy < 0 || sx >= source.width || sy >= source.height) {
                            continue;
                        }
                        const std::size_t src_index =
                            static_cast<std::size_t>(sy) * source.width + sx;
                        if (mask[src_index] == 0) {
                            continue;
                        }
                        const std::size_t src_rgba = src_index * 4;
                        const std::uint8_t a = source.rgba[src_rgba + 3];
                        if (a == 0) {
                            continue;
                        }
                        ++covered;
                        const std::uint8_t r = source.rgba[src_rgba + 0];
                        const std::uint8_t g = source.rgba[src_rgba + 1];
                        const std::uint8_t b = source.rgba[src_rgba + 2];
                        if (quality.soft_edges) {
                            // Premultiplied accumulation: averaging straight
                            // RGB with a transparent neighbour is what creates
                            // the dark fringe, so weight by alpha here and
                            // un-premultiply once at the end.
                            const double af = a / 255.0;
                            acc_r += r * af;
                            acc_g += g * af;
                            acc_b += b * af;
                            acc_a += af;
                        } else {
                            const std::uint32_t key = (static_cast<std::uint32_t>(r) << 24) |
                                                      (static_cast<std::uint32_t>(g) << 16) |
                                                      (static_cast<std::uint32_t>(b) << 8) |
                                                      static_cast<std::uint32_t>(a);
                            bool found = false;
                            for (std::size_t k = 0; k < hit_colors.size(); ++k) {
                                if (hit_colors[k] == key) {
                                    ++hit_counts[k];
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                hit_colors.push_back(key);
                                hit_counts.push_back(1);
                            }
                        }
                    }
                }

                if (covered == 0) {
                    continue;
                }
                const double coverage = covered / sample_count;
                if (coverage < coverage_threshold) {
                    continue;
                }

                const std::size_t dst_rgba = (static_cast<std::size_t>(y) * layout.width + x) * 4;
                if (quality.soft_edges) {
                    const double alpha = acc_a / sample_count;
                    if (alpha <= 0.0) {
                        continue;
                    }
                    const double inv_alpha_sum = 1.0 / acc_a;
                    output.rgba[dst_rgba + 0] = to_u8(acc_r * inv_alpha_sum);
                    output.rgba[dst_rgba + 1] = to_u8(acc_g * inv_alpha_sum);
                    output.rgba[dst_rgba + 2] = to_u8(acc_b * inv_alpha_sum);
                    output.rgba[dst_rgba + 3] = to_u8(alpha * 255.0);
                } else {
                    // Modal source colour wins. Ties resolve on the smallest
                    // packed RGBA key, which keeps the result deterministic
                    // and keeps the palette a strict subset of the source.
                    std::size_t best = 0;
                    for (std::size_t k = 1; k < hit_colors.size(); ++k) {
                        if (hit_counts[k] > hit_counts[best] ||
                            (hit_counts[k] == hit_counts[best] && hit_colors[k] < hit_colors[best])) {
                            best = k;
                        }
                    }
                    const std::uint32_t key = hit_colors[best];
                    output.rgba[dst_rgba + 0] = static_cast<std::uint8_t>((key >> 24) & 0xFFu);
                    output.rgba[dst_rgba + 1] = static_cast<std::uint8_t>((key >> 16) & 0xFFu);
                    output.rgba[dst_rgba + 2] = static_cast<std::uint8_t>((key >> 8) & 0xFFu);
                    output.rgba[dst_rgba + 3] = static_cast<std::uint8_t>(key & 0xFFu);
                }
            }
        }
    }

    clean_frame(output, quality);
    return output;
}

void clean_frame(core::Frame& frame, const RenderQuality& quality) {
    if (!quality.fill_pinholes && !quality.despeckle) {
        return;
    }
    const int w = frame.width;
    const int h = frame.height;
    if (w <= 0 || h <= 0) {
        return;
    }

    if (quality.fill_pinholes) {
        // Morphological close restricted to single transparent pixels that are
        // almost fully enclosed. Operating on a snapshot keeps the result
        // independent of scan order (and therefore deterministic).
        const std::vector<std::uint8_t> snapshot = frame.rgba;
        auto snap_alpha = [&](int x, int y) -> std::uint8_t {
            if (x < 0 || y < 0 || x >= w || y >= h) {
                return 0;
            }
            return snapshot[(static_cast<std::size_t>(y) * w + x) * 4 + 3];
        };
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (snap_alpha(x, y) != 0) {
                    continue;
                }
                int opaque = 0;
                std::uint32_t colors[8];
                int counts[8];
                int unique = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) {
                            continue;
                        }
                        const int nx = x + dx;
                        const int ny = y + dy;
                        if (snap_alpha(nx, ny) == 0) {
                            continue;
                        }
                        ++opaque;
                        const std::size_t idx = (static_cast<std::size_t>(ny) * w + nx) * 4;
                        const std::uint32_t key =
                            (static_cast<std::uint32_t>(snapshot[idx + 0]) << 24) |
                            (static_cast<std::uint32_t>(snapshot[idx + 1]) << 16) |
                            (static_cast<std::uint32_t>(snapshot[idx + 2]) << 8) |
                            static_cast<std::uint32_t>(snapshot[idx + 3]);
                        bool found = false;
                        for (int k = 0; k < unique; ++k) {
                            if (colors[k] == key) {
                                ++counts[k];
                                found = true;
                                break;
                            }
                        }
                        if (!found && unique < 8) {
                            colors[unique] = key;
                            counts[unique] = 1;
                            ++unique;
                        }
                    }
                }
                if (opaque < 6 || unique == 0) {
                    continue;
                }
                int best = 0;
                for (int k = 1; k < unique; ++k) {
                    if (counts[k] > counts[best] ||
                        (counts[k] == counts[best] && colors[k] < colors[best])) {
                        best = k;
                    }
                }
                const std::size_t dst = (static_cast<std::size_t>(y) * w + x) * 4;
                frame.rgba[dst + 0] = static_cast<std::uint8_t>((colors[best] >> 24) & 0xFFu);
                frame.rgba[dst + 1] = static_cast<std::uint8_t>((colors[best] >> 16) & 0xFFu);
                frame.rgba[dst + 2] = static_cast<std::uint8_t>((colors[best] >> 8) & 0xFFu);
                frame.rgba[dst + 3] = static_cast<std::uint8_t>(colors[best] & 0xFFu);
            }
        }
    }

    if (quality.despeckle) {
        const std::vector<std::uint8_t> snapshot = frame.rgba;
        auto snap_alpha = [&](int x, int y) -> std::uint8_t {
            if (x < 0 || y < 0 || x >= w || y >= h) {
                return 0;
            }
            return snapshot[(static_cast<std::size_t>(y) * w + x) * 4 + 3];
        };
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (snap_alpha(x, y) == 0) {
                    continue;
                }
                int opaque = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) {
                            continue;
                        }
                        if (snap_alpha(x + dx, y + dy) != 0) {
                            ++opaque;
                        }
                    }
                }
                // <= 1 neighbour means a speck or the tip of a 1 px filament
                // that is already detached; a genuine 1 px limb always has two
                // neighbours along its run, so it survives.
                if (opaque <= 1) {
                    const std::size_t dst = (static_cast<std::size_t>(y) * w + x) * 4;
                    frame.rgba[dst + 0] = 0;
                    frame.rgba[dst + 1] = 0;
                    frame.rgba[dst + 2] = 0;
                    frame.rgba[dst + 3] = 0;
                }
            }
        }
    }
}

core::Frame upscale_nearest(const core::Frame& frame, int scale) {
    if (scale <= 1) {
        return frame;
    }
    core::Frame scaled;
    scaled.width = frame.width * scale;
    scaled.height = frame.height * scale;
    scaled.rgba.assign(static_cast<std::size_t>(scaled.width) * scaled.height * 4, 0);
    for (int y = 0; y < scaled.height; ++y) {
        const int sy = y / scale;
        for (int x = 0; x < scaled.width; ++x) {
            const int sx = x / scale;
            const std::size_t src = (static_cast<std::size_t>(sy) * frame.width + sx) * 4;
            const std::size_t dst = (static_cast<std::size_t>(y) * scaled.width + x) * 4;
            scaled.rgba[dst + 0] = frame.rgba[src + 0];
            scaled.rgba[dst + 1] = frame.rgba[src + 1];
            scaled.rgba[dst + 2] = frame.rgba[src + 2];
            scaled.rgba[dst + 3] = frame.rgba[src + 3];
        }
    }
    return scaled;
}

core::Frame build_strip_sheet(const std::vector<core::Frame>& frames) {
    if (frames.empty()) {
        throw std::invalid_argument("forge: cannot build a sheet from zero frames");
    }
    const int cell_width = frames.front().width;
    const int cell_height = frames.front().height;
    core::Frame sheet;
    sheet.width = cell_width * static_cast<int>(frames.size());
    sheet.height = cell_height;
    sheet.rgba.assign(static_cast<std::size_t>(sheet.width) * sheet.height * 4, 0);

    for (std::size_t index = 0; index < frames.size(); ++index) {
        const core::Frame& frame = frames[index];
        if (frame.width != cell_width || frame.height != cell_height) {
            throw std::invalid_argument("forge: every frame in a sheet must share the canvas size");
        }
        const int offset_x = static_cast<int>(index) * cell_width;
        for (int y = 0; y < cell_height; ++y) {
            for (int x = 0; x < cell_width; ++x) {
                const std::size_t src = (static_cast<std::size_t>(y) * cell_width + x) * 4;
                const std::size_t dst =
                    (static_cast<std::size_t>(y) * sheet.width + (offset_x + x)) * 4;
                sheet.rgba[dst + 0] = frame.rgba[src + 0];
                sheet.rgba[dst + 1] = frame.rgba[src + 1];
                sheet.rgba[dst + 2] = frame.rgba[src + 2];
                sheet.rgba[dst + 3] = frame.rgba[src + 3];
            }
        }
    }
    return sheet;
}

}  // namespace spratforge::forge
