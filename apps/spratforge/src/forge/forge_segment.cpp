#include <spratforge/forge/forge_segment.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace spratforge::forge {
namespace {

struct Capsule {
    Vec2 a{};
    Vec2 b{};
    double radius_a = 1.0;
    double radius_b = 1.0;
    double bias = 0.0;
};

// Signed, radius-normalized distance from `p` to a tapered capsule.
// < 0 inside, 0 on the surface, > 0 outside. Normalizing by the local radius
// stops thick parts (the torso) from swallowing thin ones (the forearms).
double normalized_capsule_distance(const Capsule& capsule, double px, double py) {
    const double abx = capsule.b.x - capsule.a.x;
    const double aby = capsule.b.y - capsule.a.y;
    const double apx = px - capsule.a.x;
    const double apy = py - capsule.a.y;
    const double len_sq = abx * abx + aby * aby;
    double t = 0.0;
    if (len_sq > 1e-9) {
        t = (apx * abx + apy * aby) / len_sq;
        t = std::clamp(t, 0.0, 1.0);
    }
    const double cx = capsule.a.x + abx * t;
    const double cy = capsule.a.y + aby * t;
    const double dx = px - cx;
    const double dy = py - cy;
    const double distance = std::sqrt(dx * dx + dy * dy);
    const double radius = std::max(0.5, capsule.radius_a + (capsule.radius_b - capsule.radius_a) * t);
    return (distance - radius) / radius - capsule.bias;
}

// Grows `mask` by `iterations` 8-connected steps, clipped to `limit`.
void dilate_within(std::vector<std::uint8_t>& mask,
                   const std::vector<std::uint8_t>& limit,
                   int width,
                   int height,
                   int iterations) {
    if (iterations <= 0) {
        return;
    }
    std::vector<std::uint8_t> next(mask.size(), 0);
    for (int step = 0; step < iterations; ++step) {
        next = mask;
        bool changed = false;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * width + x;
                if (mask[index] != 0 || limit[index] == 0) {
                    continue;
                }
                bool neighbour = false;
                for (int dy = -1; dy <= 1 && !neighbour; ++dy) {
                    for (int dx = -1; dx <= 1 && !neighbour; ++dx) {
                        if (dx == 0 && dy == 0) {
                            continue;
                        }
                        const int nx = x + dx;
                        const int ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                            continue;
                        }
                        if (mask[static_cast<std::size_t>(ny) * width + nx] != 0) {
                            neighbour = true;
                        }
                    }
                }
                if (neighbour) {
                    next[index] = 1;
                    changed = true;
                }
            }
        }
        mask.swap(next);
        if (!changed) {
            break;
        }
    }
}

struct Rgb {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};

Rgb part_debug_color(PartId part) {
    switch (part) {
        case PartId::Torso: return {80, 110, 215};
        case PartId::Head: return {240, 185, 120};
        case PartId::BackUpperArm: return {120, 55, 55};
        case PartId::BackForearm: return {170, 85, 80};
        case PartId::BackGlove: return {225, 55, 55};
        case PartId::FrontUpperArm: return {45, 115, 60};
        case PartId::FrontForearm: return {80, 175, 90};
        case PartId::FrontGlove: return {60, 225, 95};
        case PartId::BackThigh: return {110, 55, 160};
        case PartId::BackShin: return {165, 105, 205};
        case PartId::FrontThigh: return {200, 160, 35};
        case PartId::FrontShin: return {240, 215, 95};
        default: return {128, 128, 128};
    }
}

void plot(core::Frame& frame, int x, int y, Rgb color, std::uint8_t alpha) {
    if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) {
        return;
    }
    const std::size_t index = (static_cast<std::size_t>(y) * frame.width + x) * 4;
    frame.rgba[index + 0] = color.r;
    frame.rgba[index + 1] = color.g;
    frame.rgba[index + 2] = color.b;
    frame.rgba[index + 3] = alpha;
}

void draw_line(core::Frame& frame, Vec2 a, Vec2 b, Rgb color, int thickness) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const int steps = static_cast<int>(std::ceil(std::max(std::fabs(dx), std::fabs(dy)))) + 1;
    const int half = std::max(0, thickness / 2);
    for (int i = 0; i <= steps; ++i) {
        const double t = steps == 0 ? 0.0 : static_cast<double>(i) / steps;
        const int x = static_cast<int>(std::lround(a.x + dx * t));
        const int y = static_cast<int>(std::lround(a.y + dy * t));
        for (int oy = -half; oy <= half; ++oy) {
            for (int ox = -half; ox <= half; ++ox) {
                plot(frame, x + ox, y + oy, color, 255);
            }
        }
    }
}

void draw_marker(core::Frame& frame, Vec2 p, Rgb color, int radius) {
    const int cx = static_cast<int>(std::lround(p.x));
    const int cy = static_cast<int>(std::lround(p.y));
    for (int oy = -radius - 1; oy <= radius + 1; ++oy) {
        for (int ox = -radius - 1; ox <= radius + 1; ++ox) {
            const int dist = std::max(std::abs(ox), std::abs(oy));
            if (dist > radius + 1) {
                continue;
            }
            const Rgb shade = (dist > radius) ? Rgb{20, 20, 20} : color;
            plot(frame, cx + ox, cy + oy, shade, 255);
        }
    }
}

}  // namespace

Segmentation segment_body(const core::Frame& frame, const Rig& rig, int alpha_threshold) {
    Segmentation result;
    result.width = frame.width;
    result.height = frame.height;
    result.silhouette = silhouette_of(frame, alpha_threshold);
    const std::size_t pixel_count = result.silhouette.size();
    result.owner.assign(pixel_count, -1);
    for (int i = 0; i < kPartCount; ++i) {
        result.masks[static_cast<std::size_t>(i)].assign(pixel_count, 0);
        result.pixel_counts[static_cast<std::size_t>(i)] = 0;
    }

    std::array<Capsule, kPartCount> capsules{};
    for (int i = 0; i < kPartCount; ++i) {
        const PartSpec& spec = rig.parts[static_cast<std::size_t>(i)];
        Capsule& capsule = capsules[static_cast<std::size_t>(i)];
        capsule.a = rig.joint(spec.from);
        capsule.b = rig.joint(spec.to);
        capsule.radius_a = spec.radius_from;
        capsule.radius_b = spec.radius_to;
        capsule.bias = spec.bias;
    }

    // Pass 1: award every silhouette pixel to the best capsule. The normalized
    // distance is signed, so a pixel outside every capsule still resolves to
    // the "least outside" one — no silhouette pixel is ever left unassigned.
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * frame.width + x;
            if (result.silhouette[index] == 0) {
                continue;
            }
            const double px = x + 0.5;
            const double py = y + 0.5;
            double best = std::numeric_limits<double>::infinity();
            int best_part = 0;
            for (int i = 0; i < kPartCount; ++i) {
                const double d = normalized_capsule_distance(capsules[static_cast<std::size_t>(i)], px, py);
                if (d < best) {
                    best = d;
                    best_part = i;
                }
            }
            result.owner[index] = static_cast<std::int8_t>(best_part);
            result.masks[static_cast<std::size_t>(best_part)][index] = 1;
            result.pixel_counts[static_cast<std::size_t>(best_part)] += 1;
        }
    }

    // Pass 2: sampling masks. The overlap scales with the sprite so the same
    // rig behaves identically on a 48 px and a 400 px character.
    const double body_height = std::max(1.0, rig.body_height());
    result.overlap = std::max(1, static_cast<int>(std::lround(0.030 * body_height)));
    for (int i = 0; i < kPartCount; ++i) {
        std::vector<std::uint8_t> grown = result.masks[static_cast<std::size_t>(i)];
        if (result.pixel_counts[static_cast<std::size_t>(i)] > 0) {
            dilate_within(grown, result.silhouette, frame.width, frame.height, result.overlap);
        }
        result.bounds[static_cast<std::size_t>(i)] = bounds_of(grown, frame.width, frame.height);
        result.sample_masks[static_cast<std::size_t>(i)] = std::move(grown);
    }

    return result;
}

core::Frame render_rig_overlay(const core::Frame& frame,
                               const Rig& rig,
                               const Segmentation& segmentation,
                               int scale) {
    core::Frame overlay;
    overlay.width = frame.width;
    overlay.height = frame.height;
    overlay.rgba.assign(static_cast<std::size_t>(frame.width) * frame.height * 4, 0);

    // Dark checkerboard so transparent regions stay readable.
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            const std::size_t index = (static_cast<std::size_t>(y) * frame.width + x) * 4;
            const bool light = (((x / 8) + (y / 8)) % 2) == 0;
            const std::uint8_t base = light ? 42 : 30;
            overlay.rgba[index + 0] = base;
            overlay.rgba[index + 1] = base;
            overlay.rgba[index + 2] = base;
            overlay.rgba[index + 3] = 255;
        }
    }

    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            const std::size_t pixel = static_cast<std::size_t>(y) * frame.width + x;
            const std::int8_t owner = segmentation.owner[pixel];
            if (owner < 0) {
                continue;
            }
            const std::size_t index = pixel * 4;
            const Rgb tint = part_debug_color(static_cast<PartId>(owner));
            // Keep a little of the source luminance so the artwork is still
            // recognisable under the false-colour tint.
            const double luma = (0.299 * frame.rgba[index + 0] + 0.587 * frame.rgba[index + 1] +
                                 0.114 * frame.rgba[index + 2]) /
                                255.0;
            const double shade = 0.45 + 0.55 * luma;
            overlay.rgba[index + 0] = static_cast<std::uint8_t>(std::clamp(tint.r * shade, 0.0, 255.0));
            overlay.rgba[index + 1] = static_cast<std::uint8_t>(std::clamp(tint.g * shade, 0.0, 255.0));
            overlay.rgba[index + 2] = static_cast<std::uint8_t>(std::clamp(tint.b * shade, 0.0, 255.0));
            overlay.rgba[index + 3] = 255;
        }
    }

    const double body_height = std::max(1.0, rig.body_height());
    const int bone_thickness = body_height >= 120.0 ? 2 : 1;
    const int marker_radius = std::max(1, static_cast<int>(std::lround(0.016 * body_height)));

    for (int i = 0; i < kPartCount; ++i) {
        const PartSpec& spec = rig.parts[static_cast<std::size_t>(i)];
        draw_line(overlay, rig.joint(spec.from), rig.joint(spec.to), Rgb{250, 250, 250}, bone_thickness);
    }

    for (int i = 0; i < kJointCount; ++i) {
        const JointId joint = static_cast<JointId>(i);
        Rgb color{255, 90, 200};
        if (joint == JointId::HandBack || joint == JointId::HandFront) {
            color = Rgb{255, 230, 60};
        } else if (joint == JointId::HeadTop || joint == JointId::Neck) {
            color = Rgb{60, 230, 255};
        } else if (joint == JointId::FootBack || joint == JointId::FootFront) {
            color = Rgb{255, 140, 40};
        }
        draw_marker(overlay, rig.joint(joint), color, marker_radius);
    }

    // Pivot marker: a cross on the ground contact point.
    const int cross = marker_radius * 3;
    for (int d = -cross; d <= cross; ++d) {
        plot(overlay, static_cast<int>(std::lround(rig.pivot.x)) + d,
             static_cast<int>(std::lround(rig.pivot.y)), Rgb{255, 255, 255}, 255);
        plot(overlay, static_cast<int>(std::lround(rig.pivot.x)),
             static_cast<int>(std::lround(rig.pivot.y)) + d, Rgb{255, 255, 255}, 255);
    }

    if (scale <= 1) {
        return overlay;
    }

    core::Frame scaled;
    scaled.width = overlay.width * scale;
    scaled.height = overlay.height * scale;
    scaled.rgba.assign(static_cast<std::size_t>(scaled.width) * scaled.height * 4, 0);
    for (int y = 0; y < scaled.height; ++y) {
        for (int x = 0; x < scaled.width; ++x) {
            const std::size_t src = (static_cast<std::size_t>(y / scale) * overlay.width + (x / scale)) * 4;
            const std::size_t dst = (static_cast<std::size_t>(y) * scaled.width + x) * 4;
            scaled.rgba[dst + 0] = overlay.rgba[src + 0];
            scaled.rgba[dst + 1] = overlay.rgba[src + 1];
            scaled.rgba[dst + 2] = overlay.rgba[src + 2];
            scaled.rgba[dst + 3] = overlay.rgba[src + 3];
        }
    }
    return scaled;
}

}  // namespace spratforge::forge
