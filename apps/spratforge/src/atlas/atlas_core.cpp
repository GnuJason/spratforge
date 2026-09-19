#include <spratforge/atlas/atlas_core.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

#include <export.hpp>

namespace spratforge::atlas {
namespace {

std::optional<int> parse_positive_int(std::string_view value) {
    int parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed <= 0) return std::nullopt;
    return parsed;
}

bool is_valid_frame(const core::Frame& frame) {
    return frame.width > 0 && frame.height > 0 && frame.rgba.size() ==
        static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 4U;
}

}  // namespace

bool is_valid(const AtlasConfig& config) {
    return config.columns >= 1 && config.rows >= 1 && config.padding >= 0;
}

std::optional<AtlasConfig> parse_atlas_dimensions(std::string_view dimensions) {
    const auto separator = dimensions.find('x');
    if (separator == std::string_view::npos) return std::nullopt;
    const auto columns = parse_positive_int(dimensions.substr(0, separator));
    const auto rows = parse_positive_int(dimensions.substr(separator + 1));
    if (!columns || !rows) return std::nullopt;
    return AtlasConfig{.columns = *columns, .rows = *rows, .padding = 0};
}

AtlasResult build_atlas(const std::vector<core::Frame>& frames, const AtlasConfig& config) {
    if (!is_valid(config)) throw std::runtime_error("Invalid atlas configuration");
    if (frames.empty()) throw std::runtime_error("Atlas requires at least one frame");
    if (!is_valid_frame(frames.front())) throw std::runtime_error("Atlas frame has invalid RGBA data");

    const std::size_t slots = static_cast<std::size_t>(config.columns) * static_cast<std::size_t>(config.rows);
    if (frames.size() > slots) throw std::runtime_error("Atlas grid too small for frame count");

    const int frame_width = frames.front().width;
    const int frame_height = frames.front().height;
    const long long width = static_cast<long long>(config.columns) * frame_width +
                            static_cast<long long>(config.columns - 1) * config.padding;
    const long long height = static_cast<long long>(config.rows) * frame_height +
                             static_cast<long long>(config.rows - 1) * config.padding;
    if (width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max() ||
        static_cast<unsigned long long>(width) * static_cast<unsigned long long>(height) >
            std::numeric_limits<std::size_t>::max() / 4U) {
        throw std::runtime_error("Atlas dimensions are too large");
    }

    AtlasResult atlas{
        .width = static_cast<int>(width),
        .height = static_cast<int>(height),
        .rgba = std::vector<std::uint8_t>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U, 0U),
        .metadata = {{"frames", nlohmann::json::array()},
                     {"columns", config.columns},
                     {"rows", config.rows},
                     {"padding", config.padding}},
    };

    for (std::size_t index = 0; index < frames.size(); ++index) {
        const core::Frame& frame = frames[index];
        if (!is_valid_frame(frame) || frame.width != frame_width || frame.height != frame_height) {
            throw std::runtime_error("All atlas frames must have identical valid dimensions");
        }
        const int column = static_cast<int>(index % static_cast<std::size_t>(config.columns));
        const int row = static_cast<int>(index / static_cast<std::size_t>(config.columns));
        const int offset_x = column * (frame_width + config.padding);
        const int offset_y = row * (frame_height + config.padding);
        for (int y = 0; y < frame_height; ++y) {
            for (int x = 0; x < frame_width; ++x) {
                const std::size_t source =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(frame_width) + x) * 4U;
                const std::size_t destination =
                    (static_cast<std::size_t>(offset_y + y) * static_cast<std::size_t>(atlas.width) + offset_x + x) * 4U;
                std::copy_n(frame.rgba.begin() + static_cast<std::ptrdiff_t>(source), 4,
                            atlas.rgba.begin() + static_cast<std::ptrdiff_t>(destination));
            }
        }
        atlas.metadata["frames"].push_back({{"index", index}, {"x", offset_x}, {"y", offset_y},
                                              {"w", frame_width}, {"h", frame_height}});
    }
    return atlas;
}

void save_atlas_png(const std::string& path, const AtlasResult& atlas) {
    if (path.empty() || atlas.width <= 0 || atlas.height <= 0 || atlas.rgba.size() !=
            static_cast<std::size_t>(atlas.width) * static_cast<std::size_t>(atlas.height) * 4U) {
        throw std::runtime_error("Cannot save an invalid atlas PNG");
    }
    const std::filesystem::path output_path(path);
    if (output_path.has_parent_path()) std::filesystem::create_directories(output_path.parent_path());
    const spratgen::RenderedFrame frame{.width = atlas.width, .height = atlas.height, .rgba = atlas.rgba};
    if (!spratgen::FrameExporter{}.writeFrame(frame, output_path.string())) {
        throw std::runtime_error("Unable to write atlas PNG: " + output_path.string());
    }
}

AtlasResult build_animation_atlas(const std::vector<motion::AnimationRecord>& records,
    const std::vector<profiles::MotionProfile>& motions, const profiles::ExportProfile& output,
    const std::string& palette_name, const std::string& variant) {
    if (records.empty() || records.size() != motions.size()) throw std::invalid_argument("Animation/profile count mismatch");
    std::vector<core::Frame> frames;
    for (std::size_t animation = 0; animation < records.size(); ++animation) {
        if (records[animation].name != motions[animation].clip.name || records[animation].frames.size() != static_cast<std::size_t>(motions[animation].clip.frame_count) ||
            motions[animation].durations.size() != records[animation].frames.size()) throw std::invalid_argument("Animation/profile frame mismatch");
        for (const auto& frame : records[animation].frames) frames.push_back(frame.frame);
    }
    if (frames.size() > 10000 || output.columns < 1 || output.columns > 256 || output.padding < 0 || output.padding > 64) throw std::invalid_argument("Export exceeds frame/layout limits");
    const int columns = std::min(output.columns, static_cast<int>(frames.size()));
    const int rows = (static_cast<int>(frames.size()) + columns - 1) / columns;
    const long long width = static_cast<long long>(columns) * frames.front().width + (columns - 1) * output.padding;
    const long long height = static_cast<long long>(rows) * frames.front().height + (rows - 1) * output.padding;
    if (width > 8192 || height > 8192 || width * height > 16777216) throw std::invalid_argument("Atlas exceeds pixel budget");
    auto result = build_atlas(frames, {columns, rows, output.padding});
    result.metadata["schema_version"] = output.schema_version;
    result.metadata["format"] = "ringqueen";
    result.metadata["hitbox_space"] = "pivot_relative";
    result.metadata["image"] = output.atlas_png;
    result.metadata["size"] = {result.width, result.height};
    result.metadata["variant"] = variant;
    result.metadata["animations"] = nlohmann::json::array();
    std::size_t next = 0;
    for (std::size_t animation = 0; animation < records.size(); ++animation) {
        const auto& record = records[animation];
        const auto& profile = motions[animation];
        result.metadata["animations"].push_back({{"name", record.name}, {"first_frame", next},
            {"frame_count", record.frames.size()}, {"fps", record.fps}, {"loop", profile.clip.loop}, {"events", profile.events}});
        for (std::size_t index = 0; index < record.frames.size(); ++index) {
            auto& frame = result.metadata["frames"][next++];
            frame["pivot"] = {{"x", record.frames[index].pivot.x}, {"y", record.frames[index].pivot.y}};
            frame["duration_ms"] = profile.durations.at(index);
            for (auto box : profile.hitboxes) if (box.at("frame") == index) {
                box.erase("frame");
                if (!frame.count("hitboxes")) frame["hitboxes"] = nlohmann::json::array();
                frame["hitboxes"].push_back(std::move(box));
            }
        }
    }
    const core::Frame atlas_frame{result.width, result.height, result.rgba};
    result.metadata["palette"] = {{"name", palette_name}, {"colors", nlohmann::json::array()}};
    for (const auto& color : anchor::extract_palette(atlas_frame)) result.metadata["palette"]["colors"].push_back({color.r, color.g, color.b});
    return result;
}

void save_metadata_json(const std::string& path, const nlohmann::json& metadata) {
    if (path.empty()) throw std::runtime_error("Metadata output path is required");
    const std::filesystem::path output_path(path);
    if (output_path.has_parent_path()) std::filesystem::create_directories(output_path.parent_path());
    std::ofstream output(output_path);
    if (!output) throw std::runtime_error("Unable to write atlas metadata: " + output_path.string());
    output << metadata.dump(2) << '\n';
    if (!output) throw std::runtime_error("Unable to write atlas metadata: " + output_path.string());
}

}  // namespace spratforge::atlas