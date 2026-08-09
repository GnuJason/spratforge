#include "silhouette.hpp"

#include <algorithm>
#include <cstddef>

namespace spratgen {

namespace {

std::size_t pixel_index(int x, int y, int width) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
}

bool is_foreground(const std::vector<std::uint8_t>& buffer, int x, int y, int width, int height) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return false;
    }
    return buffer[pixel_index(x, y, width)] != 0;
}

}  // namespace

Silhouette SilhouetteExtractor::extract(const Image& image) {
    Silhouette silhouette;
    silhouette.width = image.width;
    silhouette.height = image.height;

    thresholdMask(image, silhouette);
    cleanMask(silhouette);
    extractOutline(silhouette);

    return silhouette;
}

std::uint8_t SilhouetteExtractor::toGray(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    const float gray = 0.299f * static_cast<float>(red)
        + 0.587f * static_cast<float>(green)
        + 0.114f * static_cast<float>(blue);
    return static_cast<std::uint8_t>(gray);
}

void SilhouetteExtractor::thresholdMask(const Image& image, Silhouette& out) {
    const std::size_t pixelCount = image.width > 0 && image.height > 0
        ? static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height)
        : 0U;

    out.width = image.width;
    out.height = image.height;
    out.mask.assign(pixelCount, 0);
    out.outline.assign(pixelCount, 0);

    const std::size_t limit = std::min(pixelCount, image.pixels.size());
    bool useAlphaMask = false;
    for (std::size_t index = 0; index < limit; ++index) {
        if (image.pixels[index].alpha < 255) {
            useAlphaMask = true;
            break;
        }
    }

    for (std::size_t index = 0; index < limit; ++index) {
        const Color& color = image.pixels[index];
        if (useAlphaMask) {
            out.mask[index] = color.alpha >= 32 ? 1U : 0U;
            continue;
        }

        const std::uint8_t gray = toGray(color.red, color.green, color.blue);
        out.mask[index] = gray < 200 ? 1U : 0U;
    }
}

void SilhouetteExtractor::cleanMask(Silhouette& out) {
    if (out.width <= 0 || out.height <= 0 || out.mask.empty()) {
        return;
    }

    std::vector<std::uint8_t> eroded = out.mask;
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            const std::size_t index = pixel_index(x, y, out.width);
            if (out.mask[index] == 0) {
                eroded[index] = 0;
                continue;
            }

            const int neighbors = static_cast<int>(is_foreground(out.mask, x - 1, y, out.width, out.height))
                + static_cast<int>(is_foreground(out.mask, x + 1, y, out.width, out.height))
                + static_cast<int>(is_foreground(out.mask, x, y - 1, out.width, out.height))
                + static_cast<int>(is_foreground(out.mask, x, y + 1, out.width, out.height));
            eroded[index] = neighbors == 4 ? 1U : 0U;
        }
    }

    std::vector<std::uint8_t> dilated = eroded;
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            const std::size_t index = pixel_index(x, y, out.width);
            if (eroded[index] != 0) {
                dilated[index] = 1U;
                continue;
            }

            const bool hasForegroundNeighbor = is_foreground(eroded, x - 1, y, out.width, out.height)
                || is_foreground(eroded, x + 1, y, out.width, out.height)
                || is_foreground(eroded, x, y - 1, out.width, out.height)
                || is_foreground(eroded, x, y + 1, out.width, out.height);
            dilated[index] = hasForegroundNeighbor ? 1U : 0U;
        }
    }

    out.mask = std::move(dilated);
}

void SilhouetteExtractor::extractOutline(Silhouette& out) {
    out.outline.assign(out.mask.size(), 0);
    if (out.width <= 0 || out.height <= 0 || out.mask.empty()) {
        return;
    }

    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            const std::size_t index = pixel_index(x, y, out.width);
            if (out.mask[index] == 0) {
                continue;
            }

            const bool touchesBackground = !is_foreground(out.mask, x - 1, y, out.width, out.height)
                || !is_foreground(out.mask, x + 1, y, out.width, out.height)
                || !is_foreground(out.mask, x, y - 1, out.width, out.height)
                || !is_foreground(out.mask, x, y + 1, out.width, out.height);
            out.outline[index] = touchesBackground ? 1U : 0U;
        }
    }
}

}  // namespace spratgen