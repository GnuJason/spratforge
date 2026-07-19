#include "profiles/profile_loader.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <json/json.hpp>

namespace spratforge::profiles {
namespace {

bool valid_frame(const core::Frame& frame) {
    return frame.width > 0 && frame.height > 0 && frame.rgba.size() ==
        static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 4U;
}

core::Frame translate_frame(const core::Frame& source, int offset_x, int offset_y) {
    core::Frame output{.width = source.width,
                       .height = source.height,
                       .rgba = std::vector<std::uint8_t>(source.rgba.size(), 0U)};
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            const int destination_x = x + offset_x;
            const int destination_y = y + offset_y;
            if (destination_x < 0 || destination_y < 0 || destination_x >= source.width || destination_y >= source.height) {
                continue;
            }
            const std::size_t source_index =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(source.width) + x) * 4U;
            const std::size_t destination_index =
                (static_cast<std::size_t>(destination_y) * static_cast<std::size_t>(source.width) + destination_x) * 4U;
            std::copy_n(source.rgba.begin() + static_cast<std::ptrdiff_t>(source_index), 4,
                        output.rgba.begin() + static_cast<std::ptrdiff_t>(destination_index));
        }
    }
    return output;
}

void ensure_valid_profile_name(std::string_view profile_name) {
    if (profile_name.empty() || profile_name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") !=
            std::string_view::npos) {
        throw std::runtime_error("Invalid profile name: " + std::string(profile_name));
    }
}

}  // namespace

bool is_valid_interpolation(std::string_view interpolation) {
    return interpolation == "none" || interpolation == "linear" || interpolation == "ease_in_out";
}

bool is_valid_motion(std::string_view motion) {
    return motion == "none" || motion == "subtle" || motion == "idle" || motion == "walk" || motion == "jab";
}

AnimationProfile load_profile(std::string_view profile_name) {
    ensure_valid_profile_name(profile_name);
    const std::filesystem::path path = std::filesystem::path(SPRATFORGE_PROFILE_DIR) /
                                       (std::string(profile_name) + ".json");
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Unable to open profile: " + path.string());

    nlohmann::json document;
    try {
        input >> document;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error("Invalid JSON in profile '" + std::string(profile_name) + "': " + error.what());
    }

    AnimationProfile profile;
    try {
        profile.name = document.at("name").get<std::string>();
        profile.frame_count = document.at("frames").get<int>();
        profile.interpolation = document.at("interpolation").get<std::string>();
        profile.motion = document.at("motion").get<std::string>();
        profile.palette_override = document.value("palette", std::string{});
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error("Invalid fields in profile '" + std::string(profile_name) + "': " + error.what());
    }

    if (profile.name.empty()) throw std::runtime_error("Profile name cannot be empty: " + std::string(profile_name));
    if (profile.frame_count < 1) throw std::runtime_error("Profile frames must be at least 1: " + std::string(profile_name));
    if (!is_valid_interpolation(profile.interpolation)) {
        throw std::runtime_error("Invalid interpolation '" + profile.interpolation + "' in profile: " + std::string(profile_name));
    }
    if (!is_valid_motion(profile.motion)) {
        throw std::runtime_error("Invalid motion hint '" + profile.motion + "' in profile: " + std::string(profile_name));
    }
    if (!profile.palette_override.empty() && !core::is_supported_palette_mode(profile.palette_override)) {
        throw std::runtime_error("Invalid palette override '" + profile.palette_override + "' in profile: " +
                                 std::string(profile_name));
    }
    return profile;
}

void apply_interpolation(std::vector<core::Frame>& frames, const AnimationProfile& profile) {
    if (frames.size() < 3U || profile.interpolation == "none" ||
        !std::all_of(frames.begin(), frames.end(), valid_frame)) return;
    const core::Frame first = frames.front();
    const core::Frame last = frames.back();
    if (first.width != last.width || first.height != last.height || first.rgba.size() != last.rgba.size()) return;

    const int last_index = static_cast<int>(frames.size() - 1U);
    for (std::size_t frame_index = 1; frame_index + 1U < frames.size(); ++frame_index) {
        const int index = static_cast<int>(frame_index);
        const int denominator = last_index * last_index * last_index;
        const int numerator = profile.interpolation == "ease_in_out"
            ? 3 * index * index * last_index - 2 * index * index * index
            : index * last_index * last_index;
        for (std::size_t pixel = 0; pixel < first.rgba.size(); ++pixel) {
            const int delta = static_cast<int>(last.rgba[pixel]) - first.rgba[pixel];
            frames[frame_index].rgba[pixel] = static_cast<std::uint8_t>(
                static_cast<int>(first.rgba[pixel]) + (delta * numerator) / denominator);
        }
    }
}

void apply_motion_hint(std::vector<core::Frame>& frames, const AnimationProfile& profile) {
    if (frames.empty() || profile.motion == "none" || !std::all_of(frames.begin(), frames.end(), valid_frame)) return;
    for (std::size_t index = 0; index < frames.size(); ++index) {
        int offset_x = 0;
        int offset_y = 0;
        if (profile.motion == "subtle") {
            offset_x = index % 2U == 0U ? -1 : 1;
        } else if (profile.motion == "idle") {
            offset_y = index % 4U == 1U ? -1 : index % 4U == 3U ? 1 : 0;
        } else if (profile.motion == "walk") {
            constexpr int offsets[] = {0, 1, 0, -1};
            offset_x = offsets[index % 4U];
        } else if (profile.motion == "jab" && index == 1U) {
            offset_x = 2;
        }
        if (offset_x != 0 || offset_y != 0) frames[index] = translate_frame(frames[index], offset_x, offset_y);
    }
}

}  // namespace spratforge::profiles