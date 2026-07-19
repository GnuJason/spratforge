#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <json/json.hpp>

#include "core/palette_core.hpp"

namespace spratforge::atlas {

struct AtlasConfig {
    int columns = 1;
    int rows = 1;
    int padding = 0;
};

struct AtlasResult {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    nlohmann::json metadata;
};

bool is_valid(const AtlasConfig& config);
std::optional<AtlasConfig> parse_atlas_dimensions(std::string_view dimensions);
AtlasResult build_atlas(const std::vector<core::Frame>& frames, const AtlasConfig& config);
void save_atlas_png(const std::string& path, const AtlasResult& atlas);
void save_metadata_json(const std::string& path, const nlohmann::json& metadata);

}  // namespace spratforge::atlas