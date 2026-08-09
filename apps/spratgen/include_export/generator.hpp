#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "config.hpp"
#include "export.hpp"
#include "palette.hpp"
#include "pose_interp.hpp"
#include "pose_model.hpp"
#include "renderer.hpp"
#include "silhouette.hpp"
#include "skeleton.hpp"

namespace spratgen {

class Generator {
public:
    explicit Generator(Config config);

    Image loadMasterFrame();
    Palette setupPalette(const Image& image) const;
    Skeleton buildSkeleton(const Image& image);
    void setAnimation(const std::string& name, int overrideCount);
    void setupPoseModel();
    std::vector<RenderedFrame> generateFrames();
    std::vector<RenderedFrame> generateFrames(const Image& masterFrame);
    const Silhouette& silhouette() const;
    const Skeleton& skeleton() const;

private:
    Config config_;
    std::string animName_;
    int frameCountOverride_ = 0;
    Silhouette silhouette_;
    Skeleton skeleton_;
    PaletteManager paletteManager_;
    SilhouetteExtractor silhouetteExtractor_;
    SkeletonBuilder skeletonBuilder_;
    PoseModel poseModel_;
    PoseInterpolator poseInterp_;
    Palette palette_;
    PixelRenderer renderer_;
    FrameExporter exporter_;
};

}  // namespace spratgen