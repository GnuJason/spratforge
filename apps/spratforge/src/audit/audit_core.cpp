#include <spratforge/audit/audit_core.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <set>
#include <stdexcept>

#include <spratforge/core/renderer_core.hpp>

namespace spratforge::audit {
namespace {
using Json = nlohmann::json;
void append(Diagnostics& target, const Diagnostics& source) { target.insert(target.end(), source.begin(), source.end()); }
profiles::RigProfile rig_profile(const profiles::ProfilePaths& paths) {
    return profiles::load_rig_profile(paths.rig.empty() ? profiles::default_profile_path("rig/boxer_default.json") : paths.rig);
}
profiles::PaletteProfile palette_profile(const profiles::ProfilePaths& paths) {
    return profiles::load_palette_profile(paths.palette.empty() ? profiles::default_profile_path("palette/boxer_default.json") : paths.palette);
}
core::Frame load_frame(const std::filesystem::path& path) {
    core::Frame frame;
    std::string error;
    if (!core::load_frame_png(path.string(), frame, error)) throw std::runtime_error(error);
    return frame;
}
std::string filename(const Json& value) {
    const auto name = value.get<std::string>();
    if (name.empty() || name == "." || name == ".." || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.") != std::string::npos) throw std::invalid_argument("Unsafe metadata filename");
    return name;
}
void require(bool condition, const std::string& message) { if (!condition) throw std::invalid_argument(message); }
void exception_diagnostics(Diagnostics& diagnostics, const std::string& code, const std::string& path, const std::exception& error) {
    try {
        const auto nested = Json::parse(error.what());
        if (nested.is_object() && nested.count("diagnostics") && nested.at("diagnostics").is_array() && !nested.at("diagnostics").empty()) {
            for (const auto& diagnostic : nested.at("diagnostics")) diagnostics.push_back({diagnostic.at("code"), diagnostic.at("path"), diagnostic.at("message")});
            return;
        }
    } catch (const std::exception&) {}
    diagnostics.push_back({code, path, error.what()});
}
}

Diagnostics validate_source(const core::Frame& source, const profiles::RigProfile& profile, const profiles::PaletteProfile& palette) {
    Diagnostics result;
    if (source.width < 1 || source.height < 1 || source.width > profile.max_width || source.height > profile.max_height ||
        source.rgba.size() != static_cast<std::size_t>(source.width) * source.height * 4) {
        return {{"source_dimensions", "source", "Source dimensions or buffer violate rig profile"}};
    }
    bool transparent = false, visible = false, partial = false, outside = false;
    for (std::size_t offset = 0; offset < source.rgba.size(); offset += 4) {
        const auto alpha = source.rgba[offset + 3];
        transparent = transparent || alpha == 0;
        visible = visible || alpha != 0;
        partial = partial || (alpha != 0 && alpha != 255);
        const core::Color color{source.rgba[offset], source.rgba[offset + 1], source.rgba[offset + 2]};
        if (alpha && !palette.allowed_colors.empty() && std::find(palette.allowed_colors.begin(), palette.allowed_colors.end(), color) == palette.allowed_colors.end()) outside = true;
    }
    if (!visible) result.push_back({"source_empty", "source", "Source has no visible pixels"});
    if (partial) result.push_back({"source_alpha", "source", "Pixel source requires binary alpha"});
    if (profile.require_transparency && !transparent) result.push_back({"source_background", "source", "Source requires transparent background pixels"});
    if (anchor::extract_palette(source).size() > static_cast<std::size_t>(profile.max_colors)) result.push_back({"source_colors", "source", "Source exceeds profile color limit"});
    if (outside) result.push_back({"source_palette", "source", "Source contains colors outside allowed_colors"});
    return result;
}
Diagnostics validate_rig_profile(const rig::RigDefinition& rig, const profiles::RigProfile& profile) {
    auto result = rig::validate_rig(rig);
    if (rig.width > profile.max_width || rig.height > profile.max_height) result.push_back({"rig_dimensions", "rig", "Rig exceeds profile dimensions"});
    for (const auto& joint : rig.joints) if (joint.confidence < profile.min_confidence) result.push_back({"rig_confidence", joint.name, "Joint confidence below profile minimum"});
    for (const auto& name : profile.required_regions) {
        const auto region = std::find_if(rig.regions.begin(), rig.regions.end(), [&](const auto& item) { return item.name == name; });
        if (region == rig.regions.end() || std::none_of(region->pixels.begin(), region->pixels.end(), [](auto pixel) { return pixel != 0; })) result.push_back({"rig_region", name, "Required region has no source pixels"});
    }
    return result;
}
Json validate_rig_file(const std::string& path, const profiles::ProfilePaths& paths) {
    Diagnostics diagnostics;
    try {
        const auto profile = rig_profile(paths);
        auto rig = rig::load_rig(path);
        rig = rig::apply_overrides(rig, profile.overrides);
        if (!paths.rig_override.empty()) rig = rig::load_rig_overrides(rig, paths.rig_override);
        diagnostics = validate_rig_profile(rig, profile);
    } catch (const std::exception& error) { exception_diagnostics(diagnostics, "rig_invalid", "rig", error); }
    return rig::diagnostics_json(diagnostics);
}
Diagnostics validate_metadata(const Json& metadata, const core::Frame& atlas) {
    Diagnostics diagnostics;
    try {
        profiles::bounded_integer(metadata.at("schema_version"), 1, 1);
        require(metadata.at("format") == "ringqueen", "Unknown asset format");
        require(metadata.at("hitbox_space") == "pivot_relative", "Unknown hitbox coordinate convention");
        filename(metadata.at("image"));
        const auto& size = metadata.at("size");
        require(size.is_array() && size.size() == 2, "Invalid atlas size");
        const int width = profiles::bounded_integer(size.at(0), 1, 8192), height = profiles::bounded_integer(size.at(1), 1, 8192);
        require(atlas.width == width && atlas.height == height && atlas.rgba.size() == static_cast<std::size_t>(width) * height * 4, "PNG dimensions do not match metadata");
        const int columns = profiles::bounded_integer(metadata.at("columns"), 1, 256);
        const int rows = profiles::bounded_integer(metadata.at("rows"), 1, 10000);
        const int padding = profiles::bounded_integer(metadata.at("padding"), 0, 64);
        const auto& frames = metadata.at("frames");
        require(frames.is_array() && !frames.empty() && frames.size() <= 10000, "Invalid frames array");
        const int frame_width = profiles::bounded_integer(frames.at(0).at("w"), 1, 8192);
        const int frame_height = profiles::bounded_integer(frames.at(0).at("h"), 1, 8192);
        require(width == columns * frame_width + (columns - 1) * padding && height == rows * frame_height + (rows - 1) * padding, "Atlas grid dimensions mismatch");
        require(rows == (static_cast<int>(frames.size()) + columns - 1) / columns, "Atlas row count mismatch");
        const auto pivot = frames.at(0).at("pivot");
        for (std::size_t index = 0; index < frames.size(); ++index) {
            const auto& frame = frames.at(index);
            require(profiles::bounded_integer(frame.at("index"), 0, 9999) == static_cast<int>(index), "Frame index mismatch");
            require(profiles::bounded_integer(frame.at("w"), 1, 8192) == frame_width && profiles::bounded_integer(frame.at("h"), 1, 8192) == frame_height, "Frame size mismatch");
            const int x = profiles::bounded_integer(frame.at("x"), 0, width - 1), y = profiles::bounded_integer(frame.at("y"), 0, height - 1);
            require(x == static_cast<int>(index % columns) * (frame_width + padding) && y == static_cast<int>(index / columns) * (frame_height + padding), "Frame rectangles overlap or violate grid order");
            profiles::bounded_integer(frame.at("pivot").at("x"), 0, frame_width - 1);
            profiles::bounded_integer(frame.at("pivot").at("y"), 0, frame_height - 1);
            require(frame.at("pivot") == pivot, "Frame pivot is unstable");
            profiles::bounded_integer(frame.at("duration_ms"), 1, 60000);
            if (frame.count("hitboxes")) {
                require(frame.at("hitboxes").is_array(), "Hitboxes must be an array");
                for (const auto& box : frame.at("hitboxes")) {
                    profiles::fields(box, {"x", "y", "w", "h", "kind"});
                    profiles::bounded_integer(box.at("x"), -4096, 4096); profiles::bounded_integer(box.at("y"), -4096, 4096);
                    profiles::bounded_integer(box.at("w"), 1, 4096); profiles::bounded_integer(box.at("h"), 1, 4096); profiles::identifier(box.at("kind"));
                }
            }
            bool visible = false;
            for (int row = y; row < y + frame_height; ++row) for (int column = x; column < x + frame_width; ++column) {
                const auto alpha = atlas.rgba[(static_cast<std::size_t>(row) * width + column) * 4 + 3];
                require(alpha == 0 || alpha == 255, "Atlas has fractional alpha");
                visible = visible || alpha != 0;
            }
            require(visible, "Atlas contains blank animation frame");
        }
        std::size_t next = 0;
        std::set<std::string> names;
        require(metadata.at("animations").is_array() && !metadata.at("animations").empty(), "Missing animation ranges");
        for (const auto& animation : metadata.at("animations")) {
            require(names.insert(profiles::identifier(animation.at("name"))).second, "Duplicate animation name");
            require(profiles::bounded_integer(animation.at("first_frame"), 0, 9999) == static_cast<int>(next), "Animation ranges overlap or have gaps");
            const int count = profiles::bounded_integer(animation.at("frame_count"), 1, 1000);
            profiles::bounded_integer(animation.at("fps"), 1, 1000);
            require(animation.at("loop").is_boolean(), "Loop must be boolean");
            require(animation.at("events").is_array(), "Events must be array");
            for (const auto& event : animation.at("events")) {
                profiles::fields(event, {"frame", "name"});
                profiles::bounded_integer(event.at("frame"), 0, count - 1); profiles::identifier(event.at("name"));
            }
            next += static_cast<std::size_t>(count);
        }
        require(next == frames.size(), "Animation ranges do not cover atlas frames");
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const int column = x / (frame_width + padding), row = y / (frame_height + padding);
            if (x % (frame_width + padding) >= frame_width || y % (frame_height + padding) >= frame_height ||
                static_cast<std::size_t>(row * columns + column) >= frames.size()) {
                require(atlas.rgba[(static_cast<std::size_t>(y) * width + x) * 4 + 3] == 0, "Atlas padding or unused cell contains visible pixels");
            }
        }
        profiles::identifier(metadata.at("variant"));
        profiles::identifier(metadata.at("palette").at("name"));
        const auto& declared = metadata.at("palette").at("colors");
        require(declared.is_array() && !declared.empty(), "Palette colors missing");
        std::set<std::array<int, 3>> allowed;
        for (const auto& color : declared) {
            require(color.is_array() && color.size() == 3, "Invalid palette color");
            require(allowed.insert({profiles::bounded_integer(color.at(0), 0, 255), profiles::bounded_integer(color.at(1), 0, 255), profiles::bounded_integer(color.at(2), 0, 255)}).second, "Duplicate palette color");
        }
        std::set<std::array<int, 3>> actual;
        for (std::size_t offset = 0; offset < atlas.rgba.size(); offset += 4) if (atlas.rgba[offset + 3]) actual.insert({atlas.rgba[offset], atlas.rgba[offset + 1], atlas.rgba[offset + 2]});
        require(actual == allowed, "Atlas palette does not match declared colors");
    } catch (const std::exception& error) { diagnostics.push_back({"metadata_invalid", "atlas", error.what()}); }
    return diagnostics;
}
Json audit_path(const std::string& path, const profiles::ProfilePaths& paths) {
    Diagnostics diagnostics;
    try {
        const auto source_profile = rig_profile(paths);
        if (!std::filesystem::is_directory(path)) {
            require(paths.export_profile.empty(), "--export-profile applies only to output-directory audits");
            const auto source = load_frame(path);
            diagnostics = validate_source(source, source_profile, palette_profile(paths));
            if (!paths.rig_file.empty()) {
                const auto rig = rig::apply_overrides(rig::load_rig(paths.rig_file), source_profile.overrides);
                append(diagnostics, validate_rig_profile(rig, source_profile));
                if (rig.width != source.width || rig.height != source.height || rig.silhouette != anchor::extract_silhouette(source)) diagnostics.push_back({"rig_source", "rig", "Rig does not match source"});
            }
        } else {
            require(paths.rig_file.empty(), "Output audits use the rig referenced by the manifest; --rig applies to source audits");
            const auto output = profiles::load_export_profile(paths.export_profile.empty() ? profiles::default_profile_path("export/ringqueen.json") : paths.export_profile);
            const auto palette = palette_profile(paths);
            const std::filesystem::path directory(path);
            const auto metadata = profiles::read_json((directory / output.atlas_json).string());
            require(filename(metadata.at("image")) == output.atlas_png, "Image filename disagrees with export profile");
            require(metadata.at("schema_version") == output.schema_version && metadata.at("padding") == output.padding &&
                metadata.at("columns") == std::min(output.columns, static_cast<int>(metadata.at("frames").size())), "Atlas layout disagrees with export profile");
            require(metadata.at("palette").at("name") == palette.name, "Palette name disagrees with palette profile");
            const std::string variant = profiles::identifier(metadata.at("variant"));
            require(variant == "default" || palette.variants.count(variant), "Variant absent from palette profile");
            const auto atlas = load_frame(directory / output.atlas_png);
            append(diagnostics, validate_metadata(metadata, atlas));
            const auto manifest = profiles::read_json((directory / output.manifest_json).string());
            for (const std::string key : {"schema_version", "format", "image", "size", "frames", "animations", "palette", "variant", "columns", "rows", "padding", "hitbox_space"}) require(manifest.at(key) == metadata.at(key), "Manifest disagrees with atlas: " + key);
            require(filename(manifest.at("atlas_reference")) == output.atlas_json, "Invalid atlas reference");
            require(filename(manifest.at("rig_reference")) == "rig.json" && filename(manifest.at("anchor_reference")) == "anchor.json", "Invalid source references");
            const auto rig = rig::load_rig((directory / "rig.json").string());
            append(diagnostics, validate_rig_profile(rig, source_profile));
            const auto anchor = anchor::load_anchor_profile((directory / "anchor.json").string());
            require(anchor.width == metadata.at("frames").at(0).at("w") && anchor.height == metadata.at("frames").at(0).at("h"), "Anchor dimensions disagree with frames");
            require(Json({{"x", anchor.pivot.x}, {"y", anchor.pivot.y}}) == metadata.at("frames").at(0).at("pivot"), "Anchor pivot disagrees with frames");
            require(anchor.silhouette.size() == static_cast<std::size_t>(anchor.width) * anchor.height, "Anchor mask size mismatch");
            const long long offset_x = static_cast<long long>(anchor.pivot.x) - rig.pivot.x;
            const long long offset_y = static_cast<long long>(anchor.pivot.y) - rig.pivot.y;
            require(offset_x >= 0 && offset_y >= 0 && offset_x + rig.width <= anchor.width && offset_y + rig.height <= anchor.height, "Rig does not fit anchor canvas");
            std::vector<std::uint8_t> expected(anchor.silhouette.size(), 0);
            for (int y = 0; y < rig.height; ++y) for (int x = 0; x < rig.width; ++x) expected[static_cast<std::size_t>(y + offset_y) * anchor.width + x + offset_x] = rig.silhouette[static_cast<std::size_t>(y) * rig.width + x];
            require(expected == anchor.silhouette, "Anchor silhouette disagrees with rig");
            if (!palette.allowed_colors.empty()) {
                auto allowed = palette.allowed_colors;
                allowed.insert(allowed.end(), palette.locks.begin(), palette.locks.end());
                if (variant != "default") for (const auto& role : palette.variants.at(variant)) allowed.insert(allowed.end(), role.second.begin(), role.second.end());
                const auto base = allowed;
                for (auto style = palette.animation_styles.begin(); style != palette.animation_styles.end(); ++style) {
                    if (style.value().value("desaturate", false)) for (const auto& color : base) {
                        const auto gray = static_cast<std::uint8_t>((77 * color.r + 150 * color.g + 29 * color.b) / 256);
                        allowed.push_back({gray, gray, gray});
                    }
                    if (style.value().count("flash")) {
                        const auto& color = style.value().at("flash");
                        allowed.push_back({color.at(0).get<std::uint8_t>(), color.at(1).get<std::uint8_t>(), color.at(2).get<std::uint8_t>()});
                    }
                }
                for (const auto& color : anchor::extract_palette(atlas)) require(std::find(allowed.begin(), allowed.end(), color) != allowed.end(), "Atlas color violates palette profile");
            }
        }
    } catch (const std::exception& error) { exception_diagnostics(diagnostics, "audit_invalid", "input", error); }
    return rig::diagnostics_json(diagnostics);
}
}  // namespace spratforge::audit