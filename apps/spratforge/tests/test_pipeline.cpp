#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

#include <spratforge/core/renderer_core.hpp>
#include <spratforge/pipeline/pipeline_core.hpp>
#include <spratforge/rig/pixel_rig.hpp>

namespace {
void check(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream input(path);
    nlohmann::json document;
    input >> document;
    return document;
}
spratforge::core::Frame read_frame(const std::filesystem::path& path) {
    spratforge::core::Frame frame;
    std::string error;
    check(spratforge::core::load_frame_png(path.string(), frame, error), error);
    return frame;
}
std::set<std::array<std::uint8_t, 4>> colors(const spratforge::core::Frame& frame) {
    std::set<std::array<std::uint8_t, 4>> result;
    for (std::size_t offset = 0; offset < frame.rgba.size(); offset += 4) if (frame.rgba[offset + 3]) {
        result.insert({frame.rgba[offset], frame.rgba[offset + 1], frame.rgba[offset + 2], frame.rgba[offset + 3]});
    }
    return result;
}
}
int main() {
    const auto directory = std::filesystem::temp_directory_path() / "spratforge_pipeline_test";
    try {
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        spratforge::core::Frame source{24, 32, std::vector<std::uint8_t>(24 * 32 * 4, 0)};
        for (int y = 2; y < 30; ++y) for (int x = 3; x < 21; ++x) {
            const bool head = y < 8 && x >= 8 && x < 16;
            const bool body = y >= 8 && y < 20 && x >= 7 && x < 17;
            const bool arm = y >= 9 && y < 17 && (x < 7 || x >= 17);
            const bool leg = y >= 20 && ((x >= 7 && x < 11) || (x >= 13 && x < 17));
            if (!head && !body && !arm && !leg) continue;
            const auto offset = (y * 24 + x) * 4;
            source.rgba[offset] = arm ? 220 : head ? 180 : 32;
            source.rgba[offset + 1] = head ? 120 : 32;
            source.rgba[offset + 2] = leg ? 200 : 32;
            source.rgba[offset + 3] = 255;
        }
        std::string error;
        check(spratforge::core::save_frame_png(source, (directory / "input.png").string(), error), error);
        const auto output = directory / "output";
        check(spratforge::pipeline::Pipeline{}.run((directory / "input.png").string(), output.string(), error), error);
        const auto manifest = read_json(output / "manifest.json");
        const auto metadata = read_json(output / "atlas.json");
        const auto atlas = read_frame(output / "atlas.png");
        check(manifest.at("animations").size() == 10, "Expected all legacy animation names");
        const auto rig = spratforge::rig::load_rig((output / "rig.json").string());
        check(rig.silhouette == spratforge::anchor::extract_silhouette(source), "Persisted rig changed source silhouette");
        const auto palette = colors(source);
        std::size_t next = 0;
        for (const auto& animation : manifest.at("animations")) {
            const std::string name = animation.at("name");
            const int count = animation.at("frames");
            check(count > 1, "Turnkey must generate multiple frames");
            const auto sheet = read_frame(output / (name + ".png"));
            std::vector<std::uint8_t> first;
            bool changed = false;
            for (int index = 0; index < count; ++index) {
                std::ostringstream filename;
                filename << "frame_" << std::setw(3) << std::setfill('0') << index << ".png";
                const auto frame = read_frame(output / name / filename.str());
                check(frame.width * count == sheet.width && frame.height == sheet.height, "Sheet layout mismatch");
                if (index == 0) first = frame.rgba;
                else changed = changed || first != frame.rgba;
                const auto frame_colors = colors(frame);
                check(!frame_colors.empty(), "Animation frame is blank");
                check(std::includes(palette.begin(), palette.end(), frame_colors.begin(), frame_colors.end()), "Animation introduced non-source colors");
                const auto& rectangle = metadata.at("frames").at(next++);
                check(rectangle.at("w") == frame.width && rectangle.at("h") == frame.height, "Atlas frame dimensions mismatch");
                for (int y = 0; y < frame.height; ++y) {
                    const auto atlas_offset = (static_cast<std::size_t>(rectangle.at("y").get<int>() + y) * atlas.width + rectangle.at("x").get<int>()) * 4;
                    const auto sheet_offset = (static_cast<std::size_t>(y) * sheet.width + index * frame.width) * 4;
                    const auto begin = frame.rgba.begin() + static_cast<std::ptrdiff_t>(y) * frame.width * 4;
                    check(std::equal(begin, begin + frame.width * 4, atlas.rgba.begin() + static_cast<std::ptrdiff_t>(atlas_offset)), "Atlas pixels disagree with frame");
                    check(std::equal(begin, begin + frame.width * 4, sheet.rgba.begin() + static_cast<std::ptrdiff_t>(sheet_offset)), "Sheet pixels disagree with frame");
                }
            }
            check(changed, "Animation consists of identical frames: " + name);
        }
        check(next == metadata.at("frames").size(), "Manifest counts disagree with atlas");
        const auto anchor = spratforge::anchor::load_anchor_profile((output / "anchor.json").string());
        check(anchor.width == metadata.at("frames").at(0).at("w") && anchor.height == metadata.at("frames").at(0).at("h"), "Exported anchor has wrong canvas");
        check(anchor.pivot.x >= 0 && anchor.pivot.x < anchor.width && anchor.pivot.y >= 0 && anchor.pivot.y < anchor.height, "Export pivot outside frame");
        spratforge::rig::save_rig((directory / "input.rig.json").string(), rig);
        const auto second = directory / "repeat";
        check(spratforge::pipeline::Pipeline{}.run((directory / "input.png").string(), second.string(), error), error);
        check(read_json(second / "manifest.json") == manifest && read_json(second / "atlas.json") == metadata, "Metadata nondeterminism");
        check(read_frame(second / "atlas.png").rgba == atlas.rgba, "Rig reload changed atlas pixels");
        const auto templates = directory / "templates";
        std::filesystem::create_directories(templates);
        {
            std::ofstream file(templates / "jab.json");
            file << R"({"name":"jab","frame_count":3,"fps":10})";
        }
        {
            std::ofstream file(directory / "input.rig.override.json");
            file << R"({"joints":[{"name":"head","x":12,"confidence":1000}]})";
        }
        const spratforge::pipeline::Pipeline configured({.template_directory = templates.string()});
        check(configured.run((directory / "input.png").string(), (directory / "custom").string(), error), error);
        check(read_json(directory / "custom/manifest.json").at("animations").at(0).at("frames") == 3, "Explicit template count ignored");
        check(spratforge::rig::load_rig((directory / "custom/rig.json").string()).joints.at(2).confidence == 1000, "Pipeline ignored rig override");
        {
            std::ofstream file(directory / "input.rig.override.json");
            file << R"({"joints":[{"name":"head","x":-1}]})";
        }
        check(!configured.run((directory / "input.png").string(), (directory / "invalid").string(), error), "Invalid rig override accepted");
        std::filesystem::remove_all(directory);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        std::filesystem::remove_all(directory);
        return 1;
    }
}
