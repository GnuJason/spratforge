#include <spratforge/forge/forge_pipeline.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

#include <spratforge/atlas/atlas_core.hpp>
#include <spratforge/core/output_dir.hpp>
#include <spratforge/core/renderer_core.hpp>

namespace spratforge::forge {
namespace {

std::string frame_file_name(const std::string& animation, int index) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%03d", index);
    return animation + "_" + buffer + ".png";
}

std::string frame_relative_path(const std::string& animation, int index) {
    return "frames/" + animation + "/" + frame_file_name(animation, index);
}

std::string sheet_relative_path(const std::string& animation) {
    return "sheets/" + animation + ".png";
}

int duration_ms_for(int fps) {
    const int safe_fps = std::max(1, fps);
    return static_cast<int>(std::lround(1000.0 / safe_fps));
}

void write_png(const core::Frame& frame, const std::filesystem::path& path) {
    std::string error;
    if (!core::save_frame_png(frame, path.string(), error)) {
        throw std::runtime_error("forge: unable to write " + path.string() + ": " + error);
    }
}

nlohmann::json box_to_json(const BoxI& box) {
    nlohmann::json out;
    out["min_x"] = box.min_x;
    out["min_y"] = box.min_y;
    out["max_x"] = box.max_x;
    out["max_y"] = box.max_y;
    out["width"] = box.width();
    out["height"] = box.height();
    return out;
}

// Filters the clip library down to `only`, preserving the library order.
std::vector<Clip> select_clips(const std::vector<Clip>& all, const std::vector<std::string>& only) {
    if (only.empty()) {
        return all;
    }
    std::vector<Clip> selected;
    for (const Clip& clip : all) {
        if (std::find(only.begin(), only.end(), clip.name) != only.end()) {
            selected.push_back(clip);
        }
    }
    return selected;
}

}  // namespace

std::vector<std::string> available_animation_names() {
    std::vector<std::string> names;
    for (const Clip& clip : default_boxer_clips()) {
        names.push_back(clip.name);
    }
    return names;
}

void validate_options(const ForgeOptions& options) {
    if (options.input_path.empty()) {
        throw std::invalid_argument("forge: --input is required");
    }
    if (options.output_dir.empty()) {
        throw std::invalid_argument("forge: --out is required");
    }
    if (options.character.empty()) {
        throw std::invalid_argument("forge: --character must not be empty");
    }
    for (const char c : options.character) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '_' || c == '-';
        if (!ok) {
            throw std::invalid_argument(
                "forge: --character may only contain letters, digits, '-' and '_'");
        }
    }
    if (options.scale < 1 || options.scale > 16) {
        throw std::invalid_argument("forge: --scale must be between 1 and 16");
    }
    if (options.fps_override < 0 || options.fps_override > 240) {
        throw std::invalid_argument("forge: --fps must be between 1 and 240");
    }
    if (options.alpha_threshold < 1 || options.alpha_threshold > 255) {
        throw std::invalid_argument("forge: --alpha-threshold must be between 1 and 255");
    }
    if (options.margin < 0 || options.margin > 256) {
        throw std::invalid_argument("forge: --margin must be between 0 and 256");
    }
    if (!options.only.empty()) {
        const std::vector<std::string> known = available_animation_names();
        std::string joined;
        for (const std::string& name : known) {
            joined += (joined.empty() ? "" : ", ") + name;
        }
        for (const std::string& name : options.only) {
            if (std::find(known.begin(), known.end(), name) == known.end()) {
                throw std::invalid_argument("forge: --only: unknown animation '" + name +
                                            "'. Available: " + joined);
            }
        }
    }
}

nlohmann::json build_metadata(const ForgeOptions& options,
                              const Rig& rig,
                              const CanvasLayout& layout,
                              int frame_width,
                              int frame_height,
                              const std::vector<Clip>& clips) {
    const int scale = std::max(1, options.scale);
    const int anchor_x = layout.anchor_x * scale;
    const int anchor_y = layout.anchor_y * scale;

    nlohmann::json document;
    document["schema_version"] = kForgeMetadataSchemaVersion;
    // Additive-only revision marker inside schema_version 2 (Phase 4A).
    document["schema_minor"] = kForgeMetadataSchemaMinor;
    document["format"] = "spratforge-character";
    document["generator"] = {{"tool", "spratforge"}, {"subcommand", "forge"}, {"pipeline", "forge-v2"}};
    document["character"] = options.character;

    nlohmann::json source;
    source["path"] = std::filesystem::path(options.input_path).filename().string();
    source["width"] = rig.width;
    source["height"] = rig.height;
    source["alpha_threshold"] = options.alpha_threshold;
    source["body_bounds"] = box_to_json(rig.body);
    source["rig_override"] = options.rig_override_path.empty()
                                 ? nlohmann::json(nullptr)
                                 : nlohmann::json(std::filesystem::path(options.rig_override_path)
                                                      .filename()
                                                      .string());
    document["source"] = source;

    // Deterministic record of the knobs that shaped the pixels. `seed` is
    // reserved: the pipeline has no RNG, so it never changes the output.
    document["render"] = {{"supersample", options.quality.supersample},
                          {"coverage_threshold", options.quality.coverage_threshold},
                          {"soft_edges", options.quality.soft_edges},
                          {"fill_pinholes", options.quality.fill_pinholes},
                          {"despeckle", options.quality.despeckle},
                          {"seed", options.seed}};

    document["scale"] = scale;
    document["frame"] = {{"width", frame_width}, {"height", frame_height}};
    document["anchor"] = {{"x", anchor_x},
                          {"y", anchor_y},
                          {"space", "frame_local"},
                          {"semantics", "ground_contact"}};
    document["layout"] = {{"frame_path_pattern", "frames/{animation}/{animation}_{index:03d}.png"},
                          {"sheet_path_pattern", "sheets/{animation}.png"},
                          {"sheet_layout", "horizontal_strip"},
                          {"rig", "rig.json"},
                          {"rig_debug", options.emit_debug ? nlohmann::json("rig_debug.png")
                                                           : nlohmann::json(nullptr)}};

    nlohmann::json animations = nlohmann::json::array();
    int total_frames = 0;
    for (const Clip& clip : clips) {
        const int fps = options.fps_override > 0 ? options.fps_override : clip.fps;
        const int duration = duration_ms_for(fps);

        nlohmann::json animation;
        animation["name"] = clip.name;
        animation["frame_count"] = clip.frame_count;
        animation["fps"] = fps;
        animation["frame_duration_ms"] = duration;
        animation["duration_ms"] = duration * clip.frame_count;
        animation["loop"] = clip.loop;
        animation["hold_last_frame"] = clip.hold_last_frame;
        animation["is_attack"] = clip.impact_frame >= 0;
        animation["active_from"] = clip.active_from;
        animation["active_to"] = clip.active_to;
        animation["impact_frame"] = clip.impact_frame;
        // Phase 4A additive fields. `direction` is null for non-directional
        // clips; `alias_of` marks a clip kept purely for compatibility.
        animation["category"] = clip.category;
        animation["direction"] = clip.direction.empty() ? nlohmann::json(nullptr)
                                                        : nlohmann::json(clip.direction);
        animation["alias_of"] = clip.alias_of.empty() ? nlohmann::json(nullptr)
                                                      : nlohmann::json(clip.alias_of);
        animation["ground_clamp"] = clip.ground_clamp;

        if (options.emit_sheets) {
            animation["sheet"] = {{"file", sheet_relative_path(clip.name)},
                                  {"width", frame_width * clip.frame_count},
                                  {"height", frame_height},
                                  {"columns", clip.frame_count},
                                  {"rows", 1}};
        } else {
            animation["sheet"] = nullptr;
        }

        nlohmann::json frames = nlohmann::json::array();
        for (int index = 0; index < clip.frame_count; ++index) {
            const bool active = clip.active_from >= 0 && index >= clip.active_from &&
                                index <= std::max(clip.active_from, clip.active_to);
            nlohmann::json frame;
            frame["index"] = index;
            frame["file"] = frame_relative_path(clip.name, index);
            frame["duration_ms"] = duration;
            frame["active"] = active;
            frame["impact"] = index == clip.impact_frame;
            frame["anchor"] = {{"x", anchor_x}, {"y", anchor_y}};
            frame["sheet_rect"] = {{"x", index * frame_width},
                                   {"y", 0},
                                   {"width", frame_width},
                                   {"height", frame_height}};
            frames.push_back(frame);
        }
        animation["frames"] = frames;
        animations.push_back(animation);
        total_frames += clip.frame_count;
    }

    document["animations"] = animations;
    document["totals"] = {{"animation_count", static_cast<int>(clips.size())},
                          {"frame_count", total_frames}};
    return document;
}

ForgeResult run_forge(const ForgeOptions& options) {
    validate_options(options);

    core::Frame source;
    std::string error;
    if (!core::load_frame_png(options.input_path, source, error)) {
        throw std::runtime_error("forge: unable to read input sprite '" + options.input_path +
                                 "': " + error);
    }
    if (source.width < kMinInputDimension || source.height < kMinInputDimension ||
        source.width > kMaxInputDimension || source.height > kMaxInputDimension) {
        throw std::invalid_argument(
            "forge: input sprite is " + std::to_string(source.width) + "x" +
            std::to_string(source.height) + " px; supported range is " +
            std::to_string(kMinInputDimension) + "x" + std::to_string(kMinInputDimension) +
            " to " + std::to_string(kMaxInputDimension) + "x" +
            std::to_string(kMaxInputDimension) +
            " px (non-square is fine; the rig scales to the measured body bounds)");
    }

    ForgeResult result;
    result.rig = estimate_rig(source, options.alpha_threshold);
    if (!options.rig_override_path.empty()) {
        result.rig = load_rig_override(result.rig, options.rig_override_path);
    }
    result.segmentation = segment_body(source, result.rig, options.alpha_threshold);

    std::vector<Clip> clips = select_clips(default_boxer_clips(), options.only);
    if (clips.empty()) {
        throw std::invalid_argument("forge: --only selected no animations");
    }
    if (options.fps_override > 0) {
        for (Clip& clip : clips) {
            clip.fps = options.fps_override;
        }
    }

    // Pass 1: pose every frame of every clip and union the covered area so all
    // animations share one canvas and one anchor.
    std::vector<std::vector<PosedRig>> posed_clips;
    posed_clips.reserve(clips.size());
    BoxD union_bounds;
    for (const Clip& clip : clips) {
        std::vector<PosedRig> posed_frames;
        posed_frames.reserve(static_cast<std::size_t>(clip.frame_count));
        for (int index = 0; index < clip.frame_count; ++index) {
            PosedRig posed = apply_pose(result.rig, sample_clip(clip, index));
            if (clip.ground_clamp) clamp_to_ground(result.rig, posed);
            const PartTransforms transforms = compute_part_transforms(result.rig, posed);
            union_bounds.extend(posed_bounds(result.segmentation, transforms));
            posed_frames.push_back(posed);
        }
        posed_clips.push_back(std::move(posed_frames));
    }

    result.layout = compute_canvas(result.rig, union_bounds, options.margin);
    const int scale = std::max(1, options.scale);
    result.frame_width = result.layout.width * scale;
    result.frame_height = result.layout.height * scale;

    const std::filesystem::path out_root(options.output_dir);
    core::prepare_output_dir(options.output_dir, options.output_policy, "forge");

    // Pass 2: render, upscale and write.
    for (std::size_t clip_index = 0; clip_index < clips.size(); ++clip_index) {
        const Clip& clip = clips[clip_index];
        std::vector<core::Frame> rendered;
        rendered.reserve(static_cast<std::size_t>(clip.frame_count));
        for (int index = 0; index < clip.frame_count; ++index) {
            core::Frame frame = render_pose(source, result.rig, result.segmentation,
                                            posed_clips[clip_index][static_cast<std::size_t>(index)],
                                            result.layout, options.quality);
            if (scale > 1) {
                frame = upscale_nearest(frame, scale);
            }
            const std::string relative = frame_relative_path(clip.name, index);
            write_png(frame, out_root / relative);
            result.written_files.push_back(relative);
            rendered.push_back(std::move(frame));
        }
        result.total_frames += clip.frame_count;

        if (options.emit_sheets) {
            const core::Frame sheet = build_strip_sheet(rendered);
            const std::string relative = sheet_relative_path(clip.name);
            write_png(sheet, out_root / relative);
            result.written_files.push_back(relative);
        }
    }

    // Rig artefacts.
    atlas::save_metadata_json((out_root / "rig.json").string(), rig_to_json(result.rig));
    result.written_files.emplace_back("rig.json");

    if (options.emit_debug) {
        // Small sprites get magnified so the joint markers stay legible.
        const int overlay_scale = result.rig.body_height() < 96.0 ? 4 : 1;
        const core::Frame overlay =
            render_rig_overlay(source, result.rig, result.segmentation, overlay_scale);
        write_png(overlay, out_root / "rig_debug.png");
        result.written_files.emplace_back("rig_debug.png");
    }

    result.metadata = build_metadata(options, result.rig, result.layout, result.frame_width,
                                     result.frame_height, clips);
    atlas::save_metadata_json((out_root / "metadata.json").string(), result.metadata);
    result.written_files.emplace_back("metadata.json");

    std::sort(result.written_files.begin(), result.written_files.end());
    // The manifest is what makes `--clean` safe: it is the exhaustive list of
    // paths this run owns, and the only thing a later --clean may delete.
    core::write_output_manifest(options.output_dir, "forge", result.written_files);
    return result;
}

}  // namespace spratforge::forge
