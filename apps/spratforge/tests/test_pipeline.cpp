#include <cassert>
#include <filesystem>

#include <spratforge/core/renderer_core.hpp>
#include <spratforge/pipeline/pipeline_core.hpp>

int main() {
    const auto directory = std::filesystem::temp_directory_path() / "spratforge_pipeline_test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const spratforge::core::Frame source{.width = 2, .height = 2, .rgba = {255,0,0,255, 0,0,0,0, 0,0,0,0, 0,255,0,255}};
    std::string error;
    assert(spratforge::core::save_frame_png(source, (directory / "input.png").string(), error));
    assert(spratforge::pipeline::Pipeline{}.run((directory / "input.png").string(), (directory / "output").string(), error));
    for (const char* name : {"idle.png", "walk.png", "run.png", "jab.png", "hook.png", "uppercut.png", "block.png", "hit.png", "ko.png", "victory.png", "atlas.png", "atlas.json", "manifest.json", "anchor.json"}) {
        assert(std::filesystem::exists(directory / "output" / name));
    }
    std::filesystem::remove_all(directory);
    return 0;
}
