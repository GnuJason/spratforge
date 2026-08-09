#include <cassert>
#include <filesystem>

#include <spratforge/anchor/anchor_core.hpp>

int main() {
    const spratforge::core::Frame frame{.width = 3, .height = 2,
        .rgba = {0,0,0,0, 255,0,0,255, 0,0,0,0, 0,0,0,0, 0,255,0,255, 0,0,0,0}};
    const auto first = spratforge::anchor::extract_anchor(frame);
    const auto second = spratforge::anchor::extract_anchor(frame);
    assert(first.silhouette == second.silhouette);
    assert(first.palette == second.palette);
    assert(first.bounds.x == 1 && first.bounds.y == 0 && first.bounds.width == 1 && first.bounds.height == 2);
    assert(first.pivot.x == 1 && first.pivot.y == 1);
    const auto path = std::filesystem::temp_directory_path() / "spratforge_anchor_test.json";
    spratforge::anchor::save_anchor_profile(path.string(), first);
    const auto restored = spratforge::anchor::load_anchor_profile(path.string());
    assert(restored.silhouette == first.silhouette && restored.palette == first.palette);
    std::filesystem::remove(path);
    return 0;
}
