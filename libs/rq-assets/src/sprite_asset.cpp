#include <rq/assets/sprite_asset.hpp>
#include "sprite_schema.hpp"

#include <json/json.hpp>
#include <valijson/adapters/nlohmann_json_adapter.hpp>
#include <valijson/schema.hpp>
#include <valijson/schema_parser.hpp>
#include <valijson/validator.hpp>

#include <algorithm>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

namespace rq::assets {
namespace {
using Json = nlohmann::json;
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
std::string read_bytes(const std::filesystem::path& path, std::uintmax_t limit) {
    require(std::filesystem::is_regular_file(path), "Missing asset: " + path.string());
    const auto size = std::filesystem::file_size(path);
    require(size > 0 && size <= limit, "Asset size exceeds limit: " + path.string());
    std::ifstream stream(path, std::ios::binary);
    std::string content(static_cast<std::size_t>(size), '\0');
    require(static_cast<bool>(stream.read(content.data(), static_cast<std::streamsize>(size))), "Unable to read asset: " + path.string());
    return content;
}
Json read_metadata(const std::filesystem::path& path) {
    const auto content = read_bytes(path, 32 * 1024 * 1024);
    const auto diagnostics = validate_sprite_schema(content);
    require(diagnostics.empty(), diagnostics.empty() ? "" : diagnostics.front().path + ": " + diagnostics.front().message);
    return Json::parse(content);
}
std::filesystem::path reference(const std::filesystem::path& directory, const std::string& filename) {
    const auto path = std::filesystem::canonical(directory / filename);
    require(path.parent_path() == directory && std::filesystem::is_regular_file(path), "Asset reference escapes its directory: " + filename);
    return path;
}
}
std::vector<Diagnostic> validate_sprite_schema(std::string_view document) {
    std::vector<Diagnostic> diagnostics;
    try {
        const auto schema_json = nlohmann::json::parse(sprite_schema);
        const auto metadata = nlohmann::json::parse(document.begin(), document.end());
        valijson::Schema schema;
        valijson::SchemaParser parser(valijson::SchemaParser::kDraft7);
        parser.populateSchema(valijson::adapters::NlohmannJsonAdapter(schema_json), schema);
        valijson::Validator validator(valijson::Validator::kStrongTypes);
        valijson::ValidationResults results;
        if (!validator.validate(schema, valijson::adapters::NlohmannJsonAdapter(metadata), &results)) {
            valijson::ValidationResults::Error error;
            while (results.popError(error)) {
                std::string path;
                for (const auto& component : error.context) path += component;
                diagnostics.push_back({path, error.description});
            }
        }
    } catch (const std::exception& error) {
        diagnostics.push_back({"$", error.what()});
    }
    return diagnostics;
}

SpriteAsset SpriteAsset::load(const std::filesystem::path& manifest_path) {
    const auto path = std::filesystem::canonical(manifest_path);
    const auto directory = path.parent_path();
    auto manifest = read_metadata(path);
    require(manifest.count("atlas_reference") != 0, "Runtime loading requires a manifest");
    const auto metadata = read_metadata(reference(directory, manifest.at("atlas_reference").get<std::string>()));
    reference(directory, manifest.at("rig_reference").get<std::string>());
    reference(directory, manifest.at("anchor_reference").get<std::string>());
    for (const auto* key : {"atlas_reference", "rig_reference", "anchor_reference"}) manifest.erase(key);
    require(manifest == metadata, "Manifest and atlas metadata disagree");
    SpriteAsset asset;
    asset.width_ = metadata.at("size").at(0);
    asset.height_ = metadata.at("size").at(1);
    require(static_cast<std::uint64_t>(asset.width_) * asset.height_ <= 16777216, "Atlas exceeds pixel budget");
    const int columns = metadata.at("columns"), rows = metadata.at("rows"), padding = metadata.at("padding");
    const auto& frames = metadata.at("frames");
    const int frame_width = frames.at(0).at("w"), frame_height = frames.at(0).at("h");
    const Point pivot{frames.at(0).at("pivot").at("x"), frames.at(0).at("pivot").at("y")};
    require(columns <= static_cast<int>(frames.size()) && rows == (static_cast<int>(frames.size()) + columns - 1) / columns, "Invalid atlas grid");
    require(asset.width_ == columns * frame_width + (columns - 1) * padding &&
        asset.height_ == rows * frame_height + (rows - 1) * padding, "Atlas dimensions disagree with grid");
    for (std::size_t index = 0; index < frames.size(); ++index) {
        const auto& frame = frames.at(index);
        SpriteFrame value{{frame.at("x"), frame.at("y"), frame.at("w"), frame.at("h")},
            {frame.at("pivot").at("x"), frame.at("pivot").at("y")}, frame.at("duration_ms"), {}};
        require(frame.at("index") == index && value.rectangle.width == frame_width && value.rectangle.height == frame_height,
            "Invalid frame index or canvas size");
        require(value.rectangle.x == static_cast<int>(index % columns) * (frame_width + padding) &&
            value.rectangle.y == static_cast<int>(index / columns) * (frame_height + padding), "Frame rectangle violates grid order");
        require(value.pivot.x == pivot.x && value.pivot.y == pivot.y && pivot.x < frame_width && pivot.y < frame_height, "Invalid or unstable frame pivot");
        if (frame.count("hitboxes")) for (const auto& box : frame.at("hitboxes")) {
            value.hitboxes.push_back({{box.at("x"), box.at("y"), box.at("w"), box.at("h")}, box.at("kind")});
        }
        asset.frames_.push_back(std::move(value));
    }
    std::size_t next = 0;
    std::set<std::string> names;
    for (const auto& animation : metadata.at("animations")) {
        Animation value{animation.at("name"), animation.at("first_frame"), animation.at("frame_count"),
            animation.at("fps"), animation.at("loop"), 0, {}};
        require(names.insert(value.name).second && value.first_frame == next && value.frame_count <= frames.size() - next,
            "Animation ranges overlap, have gaps, or exceed frames");
        for (const auto& event : animation.at("events")) {
            const std::size_t event_frame = event.at("frame");
            require(event_frame < value.frame_count, "Animation event exceeds its range");
            value.events.push_back({event_frame, event.at("name")});
        }
        for (std::size_t index = next; index < next + value.frame_count; ++index) value.duration_ms += asset.frames_.at(index).duration_ms;
        next += value.frame_count;
        asset.animations_.push_back(std::move(value));
    }
    require(next == frames.size(), "Animation ranges do not cover all frames");
    const auto png = read_bytes(reference(directory, metadata.at("image").get<std::string>()), 128 * 1024 * 1024);
    const std::array<unsigned char, 8> signature{137,80,78,71,13,10,26,10};
    require(png.size() >= signature.size() && std::equal(signature.begin(), signature.end(), reinterpret_cast<const unsigned char*>(png.data())), "Atlas is not a PNG");
    int width = 0, height = 0, channels = 0;
    const auto* bytes = reinterpret_cast<const stbi_uc*>(png.data());
    require(stbi_info_from_memory(bytes, static_cast<int>(png.size()), &width, &height, &channels) != 0 &&
        width == asset.width_ && height == asset.height_, "PNG dimensions disagree with metadata");
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decoded(
        stbi_load_from_memory(bytes, static_cast<int>(png.size()), &width, &height, &channels, 4), stbi_image_free);
    require(decoded != nullptr, "Cannot decode atlas PNG");
    asset.rgba_.assign(decoded.get(), decoded.get() + static_cast<std::size_t>(width) * height * 4);
    std::set<std::array<std::uint8_t, 3>> actual, declared;
    std::vector<bool> visible(frames.size(), false);
    for (int row = 0; row < height; ++row) for (int column = 0; column < width; ++column) {
        const auto offset = (static_cast<std::size_t>(row) * width + column) * 4;
        const auto alpha = asset.rgba_[offset + 3];
        require(alpha == 0 || alpha == 255, "Atlas has fractional alpha");
        if (!alpha) continue;
        const auto cell = static_cast<std::size_t>(row / (frame_height + padding) * columns + column / (frame_width + padding));
        require(cell < frames.size() && row % (frame_height + padding) < frame_height && column % (frame_width + padding) < frame_width,
            "Atlas padding or unused cell contains visible pixels");
        visible[cell] = true;
        actual.insert({asset.rgba_[offset], asset.rgba_[offset + 1], asset.rgba_[offset + 2]});
    }
    require(std::all_of(visible.begin(), visible.end(), [](bool value) { return value; }), "Atlas contains an empty frame");
    for (const auto& color : metadata.at("palette").at("colors")) {
        const std::array<std::uint8_t, 3> value{color.at(0), color.at(1), color.at(2)};
        declared.insert(value);
        asset.palette_.push_back(value);
    }
    require(actual == declared, "PNG palette disagrees with metadata");
    asset.variant_ = metadata.at("variant").get<std::string>();
    asset.palette_name_ = metadata.at("palette").at("name").get<std::string>();
    return asset;
}

const Animation& SpriteAsset::animation(std::string_view name) const {
    const auto found = std::find_if(animations_.begin(), animations_.end(), [&](const auto& value) { return value.name == name; });
    if (found == animations_.end()) throw std::out_of_range("Unknown animation: " + std::string(name));
    return *found;
}
const SpriteFrame& SpriteAsset::frame_at(std::string_view name, std::uint64_t elapsed_ms) const {
    const auto& clip = animation(name);
    auto remaining = clip.loop ? elapsed_ms % clip.duration_ms : std::min(elapsed_ms, clip.duration_ms - 1);
    for (std::size_t index = clip.first_frame; index < clip.first_frame + clip.frame_count; ++index) {
        const auto& frame = frames_.at(index);
        if (remaining < static_cast<std::uint64_t>(frame.duration_ms)) return frame;
        remaining -= frame.duration_ms;
    }
    throw std::logic_error("Invalid animation duration");
}
}