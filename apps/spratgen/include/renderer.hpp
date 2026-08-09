#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "palette.hpp"
#include "pose_interp.hpp"
#include "silhouette.hpp"
#include "skeleton.hpp"

namespace spratgen {

struct RenderedFrame {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

class PixelRenderer {
public:
    RenderedFrame renderFrame(const Image& sourceImage, const Silhouette& silhouette, const PoseSkeleton& posed, const Palette& palette);

private:
    void clear(RenderedFrame& out);
    void drawOutline(RenderedFrame& out, const Image& sourceImage, const Silhouette& silhouette, const Skeleton& baseSkeleton, const PoseSkeleton& posed);
    void drawBody(RenderedFrame& out, const Image& sourceImage, const Silhouette& silhouette, const Skeleton& baseSkeleton, const PoseSkeleton& posed, const Palette& palette);
    void applyPose(RenderedFrame& out, const Skeleton& baseSkeleton, const PoseSkeleton& posed);
    void putPixel(RenderedFrame& out, int x, int y, std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha);
};

}  // namespace spratgen