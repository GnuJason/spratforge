#include "atlas/atlas_core.hpp"

#include <charconv>

namespace spratforge::atlas {
namespace {

std::optional<int> parse_positive_int(std::string_view value) {
    int parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed <= 0) return std::nullopt;
    return parsed;
}

}  // namespace

std::optional<AtlasConfig> parse_atlas_dimensions(std::string_view dimensions) {
    const auto separator = dimensions.find('x');
    if (separator == std::string_view::npos) return std::nullopt;
    const auto columns = parse_positive_int(dimensions.substr(0, separator));
    const auto rows = parse_positive_int(dimensions.substr(separator + 1));
    if (!columns || !rows) return std::nullopt;
    return AtlasConfig{.columns = *columns, .rows = *rows};
}

AtlasLayout build_atlas(const AtlasConfig& config) {
    // TODO: Calculate sprite dimensions, placement, and serializable metadata.
    return AtlasLayout{
        .config = config,
        .frame_slots = config.columns * config.rows,
        .metadata = "phase-1 layout",
    };
}

}  // namespace spratforge::atlas