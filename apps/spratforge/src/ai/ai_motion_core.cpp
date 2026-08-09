#include <spratforge/ai/ai_motion_core.hpp>

#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace spratforge::ai {
namespace {

int parse_integer(std::string_view value, const std::string& input) {
    int parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        throw std::invalid_argument("Invalid motion vector '" + input + "': expected integer x,y");
    }
    return parsed;
}

}  // namespace

MotionVector parse_motion(const std::string& input) {
    const std::string_view value(input);
    const auto separator = value.find(',');
    if (separator == std::string_view::npos || value.find(',', separator + 1U) != std::string_view::npos) {
        throw std::invalid_argument("Invalid motion vector '" + input + "': expected integer x,y");
    }
    return {.dx = parse_integer(value.substr(0, separator), input),
            .dy = parse_integer(value.substr(separator + 1U), input)};
}

MotionVector quantize(const MotionVector& motion) {
    return {.dx = std::clamp(motion.dx, -8, 8), .dy = std::clamp(motion.dy, -8, 8)};
}

void apply_motion(core::Frame& frame, const MotionVector& motion) {
    if (frame.width <= 0 || frame.height <= 0 || frame.rgba.size() !=
            static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 4U) {
        throw std::invalid_argument("Cannot apply motion to an invalid frame");
    }
    const MotionVector translation = quantize(motion);
    std::vector<std::uint8_t> shifted(frame.rgba.size(), 0U);
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            const int destination_x = x + translation.dx;
            const int destination_y = y + translation.dy;
            if (destination_x < 0 || destination_y < 0 || destination_x >= frame.width || destination_y >= frame.height) {
                continue;
            }
            const std::size_t source =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.width) + x) * 4U;
            const std::size_t destination =
                (static_cast<std::size_t>(destination_y) * static_cast<std::size_t>(frame.width) + destination_x) * 4U;
            std::copy_n(frame.rgba.begin() + static_cast<std::ptrdiff_t>(source), 4,
                        shifted.begin() + static_cast<std::ptrdiff_t>(destination));
        }
    }
    frame.rgba = std::move(shifted);
}

void apply_motion_sequence(std::vector<core::Frame>& frames, const MotionVector& motion) {
    const MotionVector base = quantize(motion);
    for (std::size_t index = 1; index < frames.size(); ++index) {
        const auto scaled_component = [index](int component) {
            const long long scaled = static_cast<long long>(component) * static_cast<long long>(index);
            return static_cast<int>(std::clamp(scaled, -8LL, 8LL));
        };
        apply_motion(frames[index], {.dx = scaled_component(base.dx), .dy = scaled_component(base.dy)});
    }
}

}  // namespace spratforge::ai