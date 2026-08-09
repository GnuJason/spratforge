#include "renderer.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "skeleton.hpp"

namespace spratgen {

namespace {

enum class BodyRegion {
    Head,
    Torso,
    LeftArm,
    RightArm,
    LeftLeg,
    RightLeg,
    Gloves,
    Shorts,
};

struct Bounds {
    int minX = 0;
    int maxX = 0;
    int minY = 0;
    int maxY = 0;
    bool valid = false;
};

std::size_t pixel_offset(int x, int y, int width) {
    return (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U;
}

std::size_t mask_offset(int x, int y, int width) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
}

Bounds compute_bounds(const Silhouette& silhouette) {
    Bounds bounds;
    for (int y = 0; y < silhouette.height; ++y) {
        for (int x = 0; x < silhouette.width; ++x) {
            const std::size_t index = mask_offset(x, y, silhouette.width);
            if (index >= silhouette.mask.size() || silhouette.mask[index] == 0) {
                continue;
            }

            if (!bounds.valid) {
                bounds = Bounds{x, x, y, y, true};
                continue;
            }

            bounds.minX = std::min(bounds.minX, x);
            bounds.maxX = std::max(bounds.maxX, x);
            bounds.minY = std::min(bounds.minY, y);
            bounds.maxY = std::max(bounds.maxY, y);
        }
    }

    return bounds;
}

int squared_distance(int ax, int ay, int bx, int by) {
    const int dx = ax - bx;
    const int dy = ay - by;
    return dx * dx + dy * dy;
}

const PoseJoint& joint_for_region(const PoseSkeleton& posed, BodyRegion region, bool useLeftSide) {
    switch (region) {
    case BodyRegion::Head:
        return posed.head;
    case BodyRegion::Torso:
    case BodyRegion::Shorts:
        return posed.torso;
    case BodyRegion::LeftArm:
        return posed.left_arm;
    case BodyRegion::RightArm:
        return posed.right_arm;
    case BodyRegion::LeftLeg:
        return posed.left_leg;
    case BodyRegion::RightLeg:
        return posed.right_leg;
    case BodyRegion::Gloves:
        return useLeftSide ? posed.left_arm : posed.right_arm;
    }

    return posed.torso;
}

const Joint& base_joint_for_region(const Skeleton& baseSkeleton, BodyRegion region, bool useLeftSide) {
    switch (region) {
    case BodyRegion::Head:
        return baseSkeleton.head;
    case BodyRegion::Torso:
    case BodyRegion::Shorts:
        return baseSkeleton.torso;
    case BodyRegion::LeftArm:
        return baseSkeleton.left_arm;
    case BodyRegion::RightArm:
        return baseSkeleton.right_arm;
    case BodyRegion::LeftLeg:
        return baseSkeleton.left_leg;
    case BodyRegion::RightLeg:
        return baseSkeleton.right_leg;
    case BodyRegion::Gloves:
        return useLeftSide ? baseSkeleton.left_arm : baseSkeleton.right_arm;
    }

    return baseSkeleton.torso;
}

BodyRegion classify_region(int x, int y, const Bounds& bounds, const Skeleton& baseSkeleton) {
    if (!bounds.valid) {
        return BodyRegion::Torso;
    }

    const int width = std::max(1, bounds.maxX - bounds.minX + 1);
    const int height = std::max(1, bounds.maxY - bounds.minY + 1);
    const int torsoHalfWidth = std::max(4, width / 7);
    const int shortsTop = bounds.minY + (height * 54) / 100;
    const int shortsBottom = bounds.minY + (height * 70) / 100;
    const bool isLeftSide = x <= baseSkeleton.torso.x;

    if (y <= bounds.minY + (height * 18) / 100) {
        return BodyRegion::Head;
    }

    const int leftGloveDistance = squared_distance(x, y, baseSkeleton.left_arm.x, baseSkeleton.left_arm.y);
    const int rightGloveDistance = squared_distance(x, y, baseSkeleton.right_arm.x, baseSkeleton.right_arm.y);
    const int gloveRadius = std::max(16, (width * width) / 100);
    if (leftGloveDistance <= gloveRadius || rightGloveDistance <= gloveRadius) {
        return BodyRegion::Gloves;
    }

    if (y >= shortsTop && y <= shortsBottom && std::abs(x - baseSkeleton.torso.x) <= torsoHalfWidth) {
        return BodyRegion::Shorts;
    }

    if (std::abs(x - baseSkeleton.torso.x) <= torsoHalfWidth && y < shortsBottom) {
        return BodyRegion::Torso;
    }

    if (y >= bounds.minY + (height * 68) / 100) {
        return isLeftSide ? BodyRegion::LeftLeg : BodyRegion::RightLeg;
    }

    return isLeftSide ? BodyRegion::LeftArm : BodyRegion::RightArm;
}

Color color_for_region(const Palette& palette, BodyRegion region) {
    switch (region) {
    case BodyRegion::Head:
        return palette.skin;
    case BodyRegion::Torso:
        return palette.base;
    case BodyRegion::LeftArm:
    case BodyRegion::RightArm:
    case BodyRegion::LeftLeg:
    case BodyRegion::RightLeg:
        return palette.accent;
    case BodyRegion::Gloves:
        return palette.gloves;
    case BodyRegion::Shorts:
        return palette.shorts;
    }

    return palette.base;
}

std::pair<int, int> transform_pixel(int x, int y, const Bounds& bounds, const Skeleton& baseSkeleton, const PoseSkeleton& posed) {
    const BodyRegion region = classify_region(x, y, bounds, baseSkeleton);
    const bool useLeftSide = x <= baseSkeleton.torso.x;
    const Joint& baseJoint = base_joint_for_region(baseSkeleton, region, useLeftSide);
    const PoseJoint& posedJoint = joint_for_region(posed, region, useLeftSide);
    return {x + (posedJoint.x - baseJoint.x), y + (posedJoint.y - baseJoint.y)};
}

}  // namespace

RenderedFrame PixelRenderer::renderFrame(const Image& sourceImage, const Silhouette& silhouette, const PoseSkeleton& posed, const Palette& palette) {
    RenderedFrame frame;
    frame.width = silhouette.width;
    frame.height = silhouette.height;
    const std::size_t pixelCount = frame.width > 0 && frame.height > 0
        ? static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height)
        : 0U;
    frame.rgba.resize(pixelCount * 4U, 0U);

    SkeletonBuilder skeletonBuilder;
    const Skeleton baseSkeleton = skeletonBuilder.build(silhouette);

    clear(frame);
    drawBody(frame, sourceImage, silhouette, baseSkeleton, posed, palette);
    drawOutline(frame, sourceImage, silhouette, baseSkeleton, posed);
    applyPose(frame, baseSkeleton, posed);
    return frame;
}

void PixelRenderer::clear(RenderedFrame& out) {
    for (std::size_t offset = 0; offset + 3U < out.rgba.size(); offset += 4U) {
        out.rgba[offset] = 0;
        out.rgba[offset + 1U] = 0;
        out.rgba[offset + 2U] = 0;
        out.rgba[offset + 3U] = 0;
    }
}

void PixelRenderer::drawOutline(RenderedFrame& out, const Image& sourceImage, const Silhouette& silhouette, const Skeleton& baseSkeleton, const PoseSkeleton& posed) {
    if (sourceImage.width == silhouette.width && sourceImage.height == silhouette.height && sourceImage.pixels.size() >= silhouette.outline.size()) {
        return;
    }

    const Bounds bounds = compute_bounds(silhouette);
    for (int y = 0; y < silhouette.height; ++y) {
        for (int x = 0; x < silhouette.width; ++x) {
            const std::size_t index = mask_offset(x, y, silhouette.width);
            if (index >= silhouette.outline.size() || silhouette.outline[index] == 0) {
                continue;
            }

            const auto [destX, destY] = transform_pixel(x, y, bounds, baseSkeleton, posed);
            putPixel(out, destX, destY, 0, 0, 0, 255);
        }
    }
}

void PixelRenderer::drawBody(RenderedFrame& out, const Image& sourceImage, const Silhouette& silhouette, const Skeleton& baseSkeleton, const PoseSkeleton& posed, const Palette& palette) {
    const Bounds bounds = compute_bounds(silhouette);
    for (int y = 0; y < silhouette.height; ++y) {
        for (int x = 0; x < silhouette.width; ++x) {
            const std::size_t index = mask_offset(x, y, silhouette.width);
            if (index >= silhouette.mask.size() || silhouette.mask[index] == 0) {
                continue;
            }

            Color color{};
            bool hasSourceColor = false;
            if (sourceImage.width == silhouette.width && sourceImage.height == silhouette.height && index < sourceImage.pixels.size()) {
                color = sourceImage.pixels[index];
                hasSourceColor = color.alpha > 0;
            }

            if (!hasSourceColor) {
                const BodyRegion region = classify_region(x, y, bounds, baseSkeleton);
                color = color_for_region(palette, region);
                color.alpha = 255;
            }

            const auto [destX, destY] = transform_pixel(x, y, bounds, baseSkeleton, posed);
            putPixel(out, destX, destY, color.red, color.green, color.blue, color.alpha);
        }
    }
}

void PixelRenderer::applyPose(RenderedFrame& out, const Skeleton& baseSkeleton, const PoseSkeleton& posed) {
#ifdef SPRATGEN_RENDER_DEBUG_JOINTS
    const std::array<PoseJoint, 6> joints = {
        posed.head,
        posed.torso,
        posed.left_arm,
        posed.right_arm,
        posed.left_leg,
        posed.right_leg,
    };

    for (const PoseJoint& joint : joints) {
        putPixel(out, joint.x, joint.y, 255, 255, 255, 255);
        putPixel(out, joint.x + 1, joint.y, 255, 255, 255, 255);
    }
#else
    static_cast<void>(out);
    static_cast<void>(baseSkeleton);
    static_cast<void>(posed);
#endif
}

void PixelRenderer::putPixel(RenderedFrame& out, int x, int y, std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha) {
    if (x < 0 || y < 0 || x >= out.width || y >= out.height) {
        return;
    }

    const std::size_t offset = pixel_offset(x, y, out.width);
    if (offset + 3U >= out.rgba.size()) {
        return;
    }

    out.rgba[offset] = red;
    out.rgba[offset + 1U] = green;
    out.rgba[offset + 2U] = blue;
    out.rgba[offset + 3U] = alpha;
}

}  // namespace spratgen