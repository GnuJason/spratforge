#include "export.hpp"

#include "pose_model.hpp"

#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace spratgen {

namespace {

constexpr std::array<unsigned char, 8> pngSignature = {137, 80, 78, 71, 13, 10, 26, 10};

std::uint32_t crc32(const unsigned char* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= static_cast<std::uint32_t>(data[index]);
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = static_cast<std::uint32_t>(-(static_cast<std::int32_t>(crc & 1u)));
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

void appendU32Be(std::vector<unsigned char>& buffer, std::uint32_t value) {
    buffer.push_back(static_cast<unsigned char>((value >> 24u) & 0xFFu));
    buffer.push_back(static_cast<unsigned char>((value >> 16u) & 0xFFu));
    buffer.push_back(static_cast<unsigned char>((value >> 8u) & 0xFFu));
    buffer.push_back(static_cast<unsigned char>(value & 0xFFu));
}

void appendChunk(std::vector<unsigned char>& buffer, const char type[4], const std::vector<unsigned char>& data) {
    appendU32Be(buffer, static_cast<std::uint32_t>(data.size()));
    const std::size_t typeOffset = buffer.size();
    buffer.insert(buffer.end(), type, type + 4);
    buffer.insert(buffer.end(), data.begin(), data.end());
    appendU32Be(buffer, crc32(buffer.data() + typeOffset, 4U + data.size()));
}

std::vector<unsigned char> makeGammaChunk() {
    std::vector<unsigned char> chunk;
    appendU32Be(chunk, 45455u);
    return chunk;
}

std::vector<unsigned char> makeSrgbChunk() {
    return {0};
}

std::vector<unsigned char> makeChrmChunk() {
    std::vector<unsigned char> chunk;
    for (const std::uint32_t value : {31270u, 32900u, 64000u, 33000u, 30000u, 60000u, 15000u, 6000u}) {
        appendU32Be(chunk, value);
    }
    return chunk;
}

std::uint32_t readU32Be(const unsigned char* data, std::size_t offset) {
    return (static_cast<std::uint32_t>(data[offset]) << 24u)
        | (static_cast<std::uint32_t>(data[offset + 1U]) << 16u)
        | (static_cast<std::uint32_t>(data[offset + 2U]) << 8u)
        | static_cast<std::uint32_t>(data[offset + 3U]);
}

std::vector<unsigned char> injectColorChunks(const unsigned char* pngData, int pngSize) {
    if (pngData == nullptr || pngSize <= static_cast<int>(pngSignature.size())) {
        return {};
    }

    std::vector<unsigned char> output;
    output.reserve(static_cast<std::size_t>(pngSize) + 128U);
    output.insert(output.end(), pngSignature.begin(), pngSignature.end());

    std::size_t offset = pngSignature.size();
    bool insertedColorChunks = false;

    while (offset + 12U <= static_cast<std::size_t>(pngSize)) {
        const std::uint32_t length = readU32Be(pngData, offset);
        const std::size_t chunkSize = 12U + static_cast<std::size_t>(length);
        if (offset + chunkSize > static_cast<std::size_t>(pngSize)) {
            return {};
        }

        const unsigned char* chunkStart = pngData + offset;
        const char* chunkType = reinterpret_cast<const char*>(chunkStart + 4U);
        output.insert(output.end(), chunkStart, chunkStart + chunkSize);
        offset += chunkSize;

        if (!insertedColorChunks && std::equal(chunkType, chunkType + 4, "IHDR")) {
            appendChunk(output, "sRGB", makeSrgbChunk());
            appendChunk(output, "gAMA", makeGammaChunk());
            appendChunk(output, "cHRM", makeChrmChunk());
            insertedColorChunks = true;
        }
    }

    return output;
}

std::vector<unsigned char> readFileBytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }

    return std::vector<unsigned char>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

}  // namespace

bool FrameExporter::writePNG(const RenderedFrame& frame, const std::string& path) {
    if (frame.width <= 0 || frame.height <= 0 || frame.rgba.empty()) {
        return false;
    }

    const bool wrotePng = stbi_write_png(
        path.c_str(),
        frame.width,
        frame.height,
        4,
        frame.rgba.data(),
        frame.width * 4) != 0;
    if (!wrotePng) {
        return false;
    }

    const std::vector<unsigned char> originalPng = readFileBytes(path);
    const std::vector<unsigned char> encodedPng = injectColorChunks(
        originalPng.data(),
        static_cast<int>(originalPng.size()));
    if (encodedPng.empty()) {
        return false;
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }

    output.write(
        reinterpret_cast<const char*>(encodedPng.data()),
        static_cast<std::streamsize>(encodedPng.size()));
    return output.good();
}

bool FrameExporter::writeFrame(const RenderedFrame& frame, const std::string& path) {
    return writePNG(frame, path);
}

bool FrameExporter::finalizeMetadata(
    const std::string& directory,
    const std::string& animationName,
    bool loop,
    int frameCount,
    int width,
    int height) {
    std::ofstream output(directory + "/metadata.json");
    if (!output) {
        return false;
    }

    output << "{\n";
    output << "  \"name\": \"spratgen_export\",\n";
    output << "  \"animation\": \"" << animationName << "\",\n";
    output << "  \"loop\": " << (loop ? "true" : "false") << ",\n";
    output << "  \"frameCount\": " << frameCount << ",\n";
    output << "  \"width\": " << width << ",\n";
    output << "  \"height\": " << height << ",\n";
    output << "  \"keyframes\": [";
    const PoseModel poseModel;
    const auto animationNames = poseModel.getAnimationNames();
    if (std::find(animationNames.begin(), animationNames.end(), animationName) != animationNames.end()) {
        const auto& animationTemplate = poseModel.getTemplate(animationName);
        for (std::size_t keyframeIndex = 0; keyframeIndex < animationTemplate.keyframes.size(); ++keyframeIndex) {
            if (keyframeIndex > 0) {
                output << ", ";
            }

            const auto& keyframe = animationTemplate.keyframes[keyframeIndex];
            output << "{"
                   << "\"t\": " << keyframe.t << ", "
                   << "\"curve\": \"" << keyframe.curve << "\""
                   << "}";
        }
    }
    output << "],\n";
    output << "  \"framePaths\": [";
    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        if (frameIndex > 0) {
            output << ", ";
        }
        std::ostringstream name;
        name << animationName << "_frame_" << std::setw(3) << std::setfill('0') << frameIndex << ".png";
        output << '"' << name.str() << '"';
    }
    output << "]\n";
    output << "}\n";
    return true;
}

}  // namespace spratgen
