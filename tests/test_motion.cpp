#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

#include "ai/ai_motion_core.hpp"
#include "core/renderer_core.hpp"

namespace {

bool frames_match(const spratforge::core::Frame& left, const spratforge::core::Frame& right) {
    return left.width == right.width && left.height == right.height && left.rgba == right.rgba;
}

}  // namespace

int main() {
    using namespace spratforge;

    const ai::MotionVector parsed = ai::parse_motion("2,-1");
    assert(parsed.dx == 2);
    assert(parsed.dy == -1);
    bool malformed_threw = false;
    try {
        (void)ai::parse_motion("2x-1");
    } catch (const std::invalid_argument&) {
        malformed_threw = true;
    }
    assert(malformed_threw);

    const ai::MotionVector clamped = ai::quantize({.dx = 99, .dy = -42});
    assert(clamped.dx == 8);
    assert(clamped.dy == -8);

    const core::Frame source{
        .width = 4,
        .height = 1,
        .rgba = {255, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    };
    core::Frame shifted = source;
    ai::apply_motion(shifted, {.dx = 1, .dy = 0});
    assert(shifted.rgba[3] == 0U);
    assert(shifted.rgba[7] == 255U);
    assert(shifted.rgba[11] == 0U);

    std::vector<core::Frame> sequence(3, source);
    ai::apply_motion_sequence(sequence, {.dx = 1, .dy = 0});
    assert(frames_match(sequence[0], source));
    assert(sequence[1].rgba[7] == 255U);
    assert(sequence[2].rgba[11] == 255U);
    assert(sequence[2].rgba[3] == 0U);

    std::vector<core::Frame> repeated_sequence(3, source);
    ai::apply_motion_sequence(repeated_sequence, {.dx = 1, .dy = 0});
    for (std::size_t index = 0; index < sequence.size(); ++index) {
        assert(frames_match(sequence[index], repeated_sequence[index]));
    }

    const core::RenderOptions options{.grid_width = 4, .grid_height = 1, .palette_mode = "strict"};
    std::string error;
    const auto frames = core::render_ai_motion_frames(source, ai::parse_motion("1,0"), 3U, options, error);
    assert(error.empty());
    assert(frames.size() == 3U);
    assert(frames_match(frames[0], source));
    assert(frames[2].rgba[11] == 255U);
    return 0;
}