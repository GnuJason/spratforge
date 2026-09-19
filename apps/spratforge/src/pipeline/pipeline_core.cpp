#include <spratforge/pipeline/pipeline_core.hpp>

#include <filesystem>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include <spratforge/anchor/anchor_core.hpp>
#include <spratforge/atlas/atlas_core.hpp>
#include <spratforge/core/renderer_core.hpp>
#include <spratforge/manifest/manifest_core.hpp>
#include <spratforge/motion/pose.hpp>
#include <spratforge/rig/pixel_rig.hpp>
#include <spratforge/templates/template_engine.hpp>

namespace spratforge::pipeline {
namespace {
motion::MotionClip clip_for(const templates::AnimationTemplate& animation) {
    motion::MotionClip clip;
    try {
        clip = motion::default_motion(animation.name);
    } catch (const std::out_of_range&) {
        clip = {animation.name, 2, false, {{0, {}, motion::Interpolation::linear},
            {1000, {{{"torso", animation.motion.dx, animation.motion.dy}}}, motion::Interpolation::linear}}};
    }
    if (animation.frame_count > 1) clip.frame_count = animation.frame_count;
    return clip;
}
void validate_names(const std::vector<templates::AnimationTemplate>& registry) {
    std::string previous;
    for (const auto& animation : registry) {
        if (animation.name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos ||
            animation.name == previous || animation.name == "atlas" || animation.name == "rig" || animation.name == "anchor" ||
            animation.name == "manifest") throw std::invalid_argument("Unsafe or duplicate animation name: " + animation.name);
        previous = animation.name;
    }
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
        auto registry = templates::load_template_registry(config_.template_directory);
        if (registry.empty()) throw std::runtime_error("No animation templates found");
        validate_names(registry);
        auto rig_path = std::filesystem::path(sprite_path);
        rig_path.replace_extension(".rig.json");
        auto rig = std::filesystem::exists(rig_path) ? rig::load_rig(rig_path.string()) : rig::build_rig(source);
        auto override_path = std::filesystem::path(sprite_path);
        override_path.replace_extension(".rig.override.json");
        if (std::filesystem::exists(override_path)) rig = rig::load_rig_overrides(rig, override_path.string());
        std::vector<motion::AnimationRecord> records;
        std::vector<motion::RenderedPose> poses;
        for (auto& animation : registry) {
            const auto clip = clip_for(animation);
            if (clip.frame_count < 1 || clip.frame_count > 1000) throw std::invalid_argument("Animation frame count must be between 1 and 1000");
            motion::AnimationRecord record{animation.name, animation.fps, {}};
            for (int index = 0; index < clip.frame_count; ++index) {
                poses.push_back(motion::render_pose(source, rig, motion::sample_pose(clip, index)));
            }
            animation.frame_count = clip.frame_count;
            records.push_back(std::move(record));
        }
        motion::align_frames(poses);
        auto exported_anchor = extracted_anchor;
        exported_anchor.width = poses.front().frame.width;
        exported_anchor.height = poses.front().frame.height;
        exported_anchor.pivot = poses.front().pivot;
        exported_anchor.bounds.x -= poses.front().origin_x;
        exported_anchor.bounds.y -= poses.front().origin_y;
        exported_anchor.silhouette.assign(static_cast<std::size_t>(exported_anchor.width) * exported_anchor.height, 0);
        for (int y = 0; y < source.height; ++y) for (int x = 0; x < source.width; ++x) {
            exported_anchor.silhouette[static_cast<std::size_t>(y - poses.front().origin_y) * exported_anchor.width + x - poses.front().origin_x] =
                extracted_anchor.silhouette[static_cast<std::size_t>(y) * source.width + x];
        }
        std::filesystem::create_directories(output_directory);
        const auto anchor_path = std::filesystem::path(output_directory) / "anchor.json";
        anchor::save_anchor_profile(anchor_path.string(), exported_anchor);
        rig::save_rig((std::filesystem::path(output_directory) / "rig.json").string(), rig);
        std::vector<core::Frame> atlas_frames;
        std::size_t next = 0;
        for (std::size_t animation = 0; animation < records.size(); ++animation) {
            auto& record = records[animation];
            std::vector<core::Frame> sheet_frames;
            for (int index = 0; index < registry[animation].frame_count; ++index) {
                record.frames.push_back(std::move(poses[next++]));
                const auto& frame = record.frames.back().frame;
                std::ostringstream filename;
                filename << "frame_" << std::setw(3) << std::setfill('0') << index << ".png";
                if (!core::save_frame_png(frame, (std::filesystem::path(output_directory) / record.name / filename.str()).string(), error)) return false;
                sheet_frames.push_back(frame);
                atlas_frames.push_back(frame);
            }
            const auto sheet = atlas::build_atlas(sheet_frames, {.columns = static_cast<int>(sheet_frames.size()), .rows = 1, .padding = 0});
            atlas::save_atlas_png((std::filesystem::path(output_directory) / (record.name + ".png")).string(), sheet);
        }
        const int columns = std::min(8, static_cast<int>(atlas_frames.size()));
        const auto atlas = atlas::build_atlas(atlas_frames,
            {.columns = columns, .rows = (static_cast<int>(atlas_frames.size()) + columns - 1) / columns, .padding = 0});
        atlas::save_atlas_png((std::filesystem::path(output_directory) / "atlas.png").string(), atlas);
        atlas::save_metadata_json((std::filesystem::path(output_directory) / "atlas.json").string(), atlas.metadata);
        manifest::save_manifest_json((std::filesystem::path(output_directory) / "manifest.json").string(),
                                     manifest::generate_manifest(exported_anchor, registry, "source"));
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}
}  // namespace spratforge::pipeline
