#include <cassert>
#include <filesystem>
#include <fstream>

#include "manifest/manifest_core.hpp"

int main() {
    const spratforge::anchor::AnchorData anchor{.width = 2, .height = 2, .bounds = {0, 0, 2, 2}, .pivot = {0, 1}};
    const std::vector<spratforge::templates::AnimationTemplate> templates{{.name = "idle", .frame_count = 1, .fps = 8}};
    const auto manifest = spratforge::manifest::generate_manifest(anchor, templates, "source");
    const auto path = std::filesystem::temp_directory_path() / "spratforge_manifest_test.json";
    spratforge::manifest::save_manifest_json(path.string(), manifest);
    std::ifstream first(path); const std::string one((std::istreambuf_iterator<char>(first)), {});
    spratforge::manifest::save_manifest_json(path.string(), manifest);
    std::ifstream second(path); const std::string two((std::istreambuf_iterator<char>(second)), {});
    assert(one == two);
    std::filesystem::remove(path);
    return 0;
}
