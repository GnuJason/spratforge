#pragma once

#include <cstdint>
#include <vector>

#include "palette.hpp"

namespace spratgen {

struct Silhouette {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> mask;
    std::vector<std::uint8_t> outline;
};

class SilhouetteExtractor {
public:
    Silhouette extract(const Image& image);

private:
    std::uint8_t toGray(std::uint8_t red, std::uint8_t green, std::uint8_t blue);
    void thresholdMask(const Image& image, Silhouette& out);
    void cleanMask(Silhouette& out);
    void extractOutline(Silhouette& out);
};

}  // namespace spratgen