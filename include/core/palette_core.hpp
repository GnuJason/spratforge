#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace spratforge::core {

struct Frame {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;
};

bool apply_palette_mode(std::string_view mode, Frame& frame);
bool is_supported_palette_mode(std::string_view mode);

}  // namespace spratforge::core