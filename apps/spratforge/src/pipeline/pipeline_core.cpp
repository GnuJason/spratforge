#include <spratforge/pipeline/pipeline_core.hpp>

#include <filesystem>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include <spratforge/anchor/anchor_core.hpp>
#include <spratforge/audit/audit_core.hpp>
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

bool generate(const std::string& sprite_path, const std::string& output_directory,
              const profiles::ProfilePaths& paths, std::string& error,
              core::OutputPolicy policy) {
    std::vector<std::string> written;
    const auto record_written = [&](const std::filesystem::path& root, const std::filesystem::path& file) {
        written.push_back(std::filesystem::relative(file, root).generic_string());
    };
    try {
        if (sprite_path.empty() || output_directory.empty()) throw std::invalid_argument("Input and output paths are required");
        // Validate (and, under --clean, prune) the output directory up front,
        // but do NOT create it: `generate` must not leave an empty directory
        // behind when a later profile/motion validation step fails.
        core::prepare_output_dir(output_directory, policy, "generate", false);
        const auto profiles = profiles::load_generation_profiles(paths);
        core::Frame source;
        if (!core::load_frame_png(sprite_path, source, error)) return false;
        auto diagnostics = audit::validate_source(source, profiles.rig, profiles.palette);
        if (!diagnostics.empty()) throw std::invalid_argument(rig::diagnostics_json(diagnostics).dump());
        auto pixel_rig = paths.rig_file.empty() ? rig::build_rig(source) : rig::load_rig(paths.rig_file);
        pixel_rig = rig::apply_overrides(pixel_rig, profiles.rig.overrides);
        if (!paths.rig_override.empty()) pixel_rig = rig::load_rig_overrides(pixel_rig, paths.rig_override);
        diagnostics = audit::validate_rig_profile(pixel_rig, profiles.rig);
        if (!diagnostics.empty()) throw std::invalid_argument(rig::diagnostics_json(diagnostics).dump());
        std::vector<motion::RenderedPose> frames;
        std::size_t stored_pixels = 0;
        int left = 0, top = 0, right = source.width, bottom = source.height;
        for (const auto& animation : profiles.motions) {
            const auto styled = profiles::apply_style(source, pixel_rig, profiles.palette, paths.variant, animation.clip.name);
            for (int index = 0; index < animation.clip.frame_count; ++index) {
                frames.push_back(motion::render_pose(styled, pixel_rig, motion::sample_pose(animation.clip, index)));
                const auto& rendered = frames.back();
                stored_pixels += rendered.frame.rgba.size() / 4;
                left = std::min(left, rendered.origin_x); top = std::min(top, rendered.origin_y);
                right = std::max(right, rendered.origin_x + rendered.frame.width);
                bottom = std::max(bottom, rendered.origin_y + rendered.frame.height);
                if (frames.size() > 10000 || stored_pixels > 16777216 ||
                    static_cast<long long>(right - left) * (bottom - top) * static_cast<long long>(frames.size()) > 16777216) {
                    throw std::invalid_argument("Animation set exceeds frame-buffer budget");
                }
            }
        }
        motion::align_frames(frames);
        auto exported_anchor = anchor::extract_anchor(source);
        const auto original_mask = exported_anchor.silhouette;
        exported_anchor.width = frames.front().frame.width;
        exported_anchor.height = frames.front().frame.height;
        exported_anchor.pivot = frames.front().pivot;
        exported_anchor.bounds.x -= frames.front().origin_x;
        exported_anchor.bounds.y -= frames.front().origin_y;
        exported_anchor.silhouette.assign(static_cast<std::size_t>(exported_anchor.width) * exported_anchor.height, 0);
        for (int y = 0; y < source.height; ++y) for (int x = 0; x < source.width; ++x) {
            exported_anchor.silhouette[static_cast<std::size_t>(y - frames.front().origin_y) * exported_anchor.width + x - frames.front().origin_x] = original_mask[static_cast<std::size_t>(y) * source.width + x];
        }
        std::vector<motion::AnimationRecord> records;
        std::size_t next = 0;
        for (const auto& animation : profiles.motions) {
            motion::AnimationRecord record{animation.clip.name, animation.fps, {}};
            for (int index = 0; index < animation.clip.frame_count; ++index) record.frames.push_back(std::move(frames[next++]));
            records.push_back(std::move(record));
        }
        const auto atlas = atlas::build_animation_atlas(records, profiles.motions, profiles.output, profiles.palette.name, paths.variant);
        diagnostics = audit::validate_metadata(atlas.metadata, {atlas.width, atlas.height, atlas.rgba});
        if (!diagnostics.empty()) throw std::invalid_argument(rig::diagnostics_json(diagnostics).dump());
        const std::filesystem::path directory(output_directory);
        std::filesystem::create_directories(directory);
        anchor::save_anchor_profile((directory / "anchor.json").string(), exported_anchor);
        record_written(directory, directory / "anchor.json");
        rig::save_rig((directory / "rig.json").string(), pixel_rig);
        record_written(directory, directory / "rig.json");
        atlas::save_atlas_png((directory / profiles.output.atlas_png).string(), atlas);
        record_written(directory, directory / profiles.output.atlas_png);
        atlas::save_metadata_json((directory / profiles.output.atlas_json).string(), atlas.metadata);
        record_written(directory, directory / profiles.output.atlas_json);
        atlas::save_metadata_json((directory / profiles.output.manifest_json).string(), manifest::generate_manifest(atlas.metadata, profiles.output.atlas_json));
        record_written(directory, directory / profiles.output.manifest_json);
        for (const auto& record : records) {
            std::vector<core::Frame> sheet_frames;
            for (std::size_t index = 0; index < record.frames.size(); ++index) {
                const auto& frame = record.frames[index].frame;
                if (profiles.output.write_frames) {
                    std::ostringstream filename;
                    filename << "frame_" << std::setw(3) << std::setfill('0') << index << ".png";
                    if (!core::save_frame_png(frame, (directory / record.name / filename.str()).string(), error)) return false;
                    record_written(directory, directory / record.name / filename.str());
                }
                if (profiles.output.write_sheets) sheet_frames.push_back(frame);
            }
            if (profiles.output.write_sheets) {
                const int columns = std::min(profiles.output.columns, static_cast<int>(sheet_frames.size()));
                const auto sheet = atlas::build_atlas(sheet_frames, {columns, (static_cast<int>(sheet_frames.size()) + columns - 1) / columns, profiles.output.padding});
                atlas::save_atlas_png((directory / (record.name + ".png")).string(), sheet);
                record_written(directory, directory / (record.name + ".png"));
            }
        }
        // The manifest is the exhaustive list of paths this run owns and is
        // the only thing a later `--clean` may delete.
        core::write_output_manifest(output_directory, "generate", written);
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
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
