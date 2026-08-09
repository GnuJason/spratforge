#include "pipeline/pipeline_core.hpp"

#include <filesystem>
#include <stdexcept>
#include <utility>
#include <vector>

#include "ai/ai_motion_core.hpp"
#include "anchor/anchor_core.hpp"
#include "atlas/atlas_core.hpp"
#include "core/pixel_normalizer.hpp"
#include "core/renderer_core.hpp"
#include "manifest/manifest_core.hpp"
#include "templates/template_engine.hpp"

namespace spratforge::pipeline {
namespace {
core::Palette source_palette(const anchor::AnchorData& anchor) {
    return {.name = "source", .colors = anchor.palette};
}
}  // namespace

Pipeline::Pipeline(PipelineConfig config) : config_(std::move(config)) {
    if (config_.template_directory.empty()) config_.template_directory = SPRATFORGE_TEMPLATE_DIR;
}

bool Pipeline::run(const std::string& sprite_path, const std::string& output_directory, std::string& error) const {
    try {
        if (sprite_path.empty() || output_directory.empty()) throw std::runtime_error("Sprite path and output directory are required");
        core::Frame source;
        if (!core::load_frame_png(sprite_path, source, error)) return false;
        const auto extracted_anchor = anchor::extract_anchor(source);
        if (extracted_anchor.palette.empty()) throw std::runtime_error("Source sprite contains no visible pixels");
        const auto registry = templates::load_template_registry(config_.template_directory);
        if (registry.empty()) throw std::runtime_error("No animation templates found");
        std::filesystem::create_directories(output_directory);
        const auto anchor_path = std::filesystem::path(output_directory) / "anchor.json";
        anchor::save_anchor_profile(anchor_path.string(), extracted_anchor);
        const auto anchor = anchor::load_anchor_profile(anchor_path.string());
        const auto palette = source_palette(anchor);
        std::vector<core::Frame> atlas_frames;
        for (const auto& animation : registry) {
            core::Frame frame = source;
            ai::apply_motion(frame, ai::quantize(animation.motion));
            core::normalize_pixels(frame, palette);
            if (!core::save_frame_png(frame, (std::filesystem::path(output_directory) / (animation.name + ".png")).string(), error)) return false;
            atlas_frames.push_back(std::move(frame));
        }
        const auto atlas = atlas::build_atlas(atlas_frames, {.columns = static_cast<int>(atlas_frames.size()), .rows = 1, .padding = 0});
        atlas::save_atlas_png((std::filesystem::path(output_directory) / "atlas.png").string(), atlas);
        atlas::save_metadata_json((std::filesystem::path(output_directory) / "atlas.json").string(), atlas.metadata);
        manifest::save_manifest_json((std::filesystem::path(output_directory) / "manifest.json").string(),
                                     manifest::generate_manifest(anchor, registry, palette.name));
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}
}  // namespace spratforge::pipeline
