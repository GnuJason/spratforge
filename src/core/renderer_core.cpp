#include "core/renderer_core.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include <stb_image.h>

#include <export.hpp>

namespace spratforge::core {
namespace {

bool load_png(std::string_view input_path, Frame& frame, std::string& error) {
    if (input_path.empty()) {
        error = "An input PNG path is required";
        return false;
    }

    int width = 0;
    int height = 0;
    int source_channels = 0;
    stbi_uc* pixels = stbi_load(std::string(input_path).c_str(), &width, &height, &source_channels, STBI_rgb_alpha);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        error = "Unable to load PNG '" + std::string(input_path) + "': " +
                (stbi_failure_reason() == nullptr ? "unknown error" : stbi_failure_reason());
        if (pixels != nullptr) stbi_image_free(pixels);
        return false;
    }

    const std::size_t byte_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    frame = Frame{.width = width, .height = height, .rgba = std::vector<std::uint8_t>(pixels, pixels + byte_count)};
    stbi_image_free(pixels);
    return true;
}

bool write_png(const Frame& frame, std::string_view output_path, std::string& error) {
    if (frame.width <= 0 || frame.height <= 0 || frame.rgba.size() !=
            static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 4U) {
        error = "Renderer produced an invalid RGBA frame";
        return false;
    }

    const std::filesystem::path path(output_path);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    const spratgen::RenderedFrame rendered{.width = frame.width, .height = frame.height, .rgba = frame.rgba};
    if (!spratgen::FrameExporter{}.writeFrame(rendered, path.string())) {
        error = "Unable to write PNG '" + path.string() + "'";
        return false;
    }
    return true;
}

bool render_base_frame(const RenderOptions& options, Frame& frame, std::string& error) {
    if (options.grid_width <= 0 || options.grid_height <= 0) {
        error = "Grid dimensions must be positive";
        return false;
    }

    Frame source;
    if (!load_png(options.input_path, source, error)) return false;
    frame = downsample_nearest(source, options.grid_width, options.grid_height);
    if (options.palette_mode && !apply_palette_mode(*options.palette_mode, frame)) {
        error = "Invalid palette mode or RGBA frame: " + *options.palette_mode;
        return false;
    }
    return true;
}

void enforce_palette_consistency(std::vector<Frame>& frames, const std::optional<std::string>& palette_mode) {
    if (!palette_mode || *palette_mode == "strict") return;
    enforce_consistency(frames, load_palette(*palette_mode == "gb" ? "gb" : "nes"));
}

}  // namespace

bool load_frame_png(std::string_view input_path, Frame& frame, std::string& error) {
    return load_png(input_path, frame, error);
}

bool save_frame_png(const Frame& frame, std::string_view output_path, std::string& error) {
    return write_png(frame, output_path, error);
}

Frame downsample_nearest(const Frame& source, int width, int height) {
    if (source.width <= 0 || source.height <= 0 || width <= 0 || height <= 0 || source.rgba.size() !=
            static_cast<std::size_t>(source.width) * static_cast<std::size_t>(source.height) * 4U) {
        return {};
    }

    Frame output{.width = width, .height = height, .rgba = std::vector<std::uint8_t>(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U)};
    for (int y = 0; y < height; ++y) {
        const int source_y = std::min(source.height - 1, (y * source.height) / height);
        for (int x = 0; x < width; ++x) {
            const int source_x = std::min(source.width - 1, (x * source.width) / width);
            const std::size_t source_offset =
                (static_cast<std::size_t>(source_y) * static_cast<std::size_t>(source.width) + source_x) * 4U;
            const std::size_t output_offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4U;
            std::copy_n(source.rgba.begin() + static_cast<std::ptrdiff_t>(source_offset), 4,
                        output.rgba.begin() + static_cast<std::ptrdiff_t>(output_offset));
        }
    }
    return output;
}

std::vector<Frame> render_profile_frames(const profiles::AnimationProfile& profile, const RenderOptions& options,
                                         std::string& error) {
    Frame source;
    if (!load_png(options.input_path, source, error)) return {};
    return render_profile_frames(source, profile, options, error);
}

bool render_single_frame(const RenderOptions& options, std::string_view output_path, std::string& error) {
    if (output_path.empty()) {
        error = "An output path is required";
        return false;
    }

    Frame frame;
    return render_base_frame(options, frame, error) && write_png(frame, output_path, error);
}

std::vector<Frame> render_profile_frames(const Frame& source, const profiles::AnimationProfile& profile,
                                         const RenderOptions& options, std::string& error) {
    if (profile.frame_count <= 0) {
        error = "Profile must request at least one frame";
        return {};
    }

    RenderOptions effective_options = options;
    if (!profile.palette_override.empty()) effective_options.palette_mode = profile.palette_override;
    Frame base = downsample_nearest(source, effective_options.grid_width, effective_options.grid_height);
    if (base.rgba.empty()) {
        error = "Unable to downsample profile source frame";
        return {};
    }
    if (effective_options.palette_mode && !apply_palette_mode(*effective_options.palette_mode, base)) {
        error = "Invalid palette mode or RGBA frame: " + *effective_options.palette_mode;
        return {};
    }

    std::vector<Frame> frames(static_cast<std::size_t>(profile.frame_count), base);
    profiles::apply_interpolation(frames, profile);
    profiles::apply_motion_hint(frames, profile);
    enforce_palette_consistency(frames, effective_options.palette_mode);
    return frames;
}

bool render_profile(const profiles::AnimationProfile& profile, const RenderOptions& options,
                    std::string_view output_directory, std::string& error) {
    if (profile.frame_count <= 0 || output_directory.empty()) {
        error = "A valid profile and output directory are required";
        return false;
    }

    const std::vector<Frame> frames = render_profile_frames(profile, options, error);
    if (frames.empty()) return false;

    for (std::size_t index = 0; index < frames.size(); ++index) {
        std::ostringstream filename;
        filename << "frame_" << std::setw(3) << std::setfill('0') << index << ".png";
        const std::filesystem::path output_path = std::filesystem::path(output_directory) / filename.str();
        if (!write_png(frames[index], output_path.string(), error)) return false;
    }
    return true;
}

std::vector<Frame> render_ai_motion_frames(const Frame& source, const ai::MotionVector& motion, std::size_t frame_count,
                                           const RenderOptions& options, std::string& error) {
    if (frame_count == 0U) {
        error = "AI motion requires at least one frame";
        return {};
    }
    Frame base = downsample_nearest(source, options.grid_width, options.grid_height);
    if (base.rgba.empty()) {
        error = "Unable to downsample motion source frame";
        return {};
    }

    if (options.palette_mode && !apply_palette_mode(*options.palette_mode, base)) {
        error = "Invalid palette mode or RGBA frame: " + *options.palette_mode;
        return {};
    }
    std::vector<Frame> frames(frame_count, base);
    ai::apply_motion_sequence(frames, ai::quantize(motion));
    enforce_palette_consistency(frames, options.palette_mode);
    return frames;
}

bool render_ai_motion(const RenderOptions& options, const ai::MotionVector& motion, std::size_t frame_count,
                      std::string_view output_path, std::string& error) {
    Frame source;
    if (!load_png(options.input_path, source, error)) return false;
    const std::vector<Frame> frames = render_ai_motion_frames(source, motion, frame_count, options, error);
    return !frames.empty() && write_png(frames.front(), output_path, error);
}

}  // namespace spratforge::core