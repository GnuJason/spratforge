#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace spratforge::atlas {

struct AtlasConfig {
    int columns = 0;
    int rows = 0;
};

struct AtlasLayout {
    AtlasConfig config;
    int frame_slots = 0;
    std::string metadata;
};

std::optional<AtlasConfig> parse_atlas_dimensions(std::string_view dimensions);
AtlasLayout build_atlas(const AtlasConfig& config);

}  // namespace spratforge::atlas