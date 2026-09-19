#include <spratforge/rig/pixel_rig.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class Action> void rejects(Action action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, "Expected invalid rig to be rejected");
}
}
int main() {
    try {
        spratforge::core::Frame source{24, 32, std::vector<std::uint8_t>(24 * 32 * 4, 0)};
        for (int y = 2; y < 30; ++y) for (int x = 4; x < 20; ++x) {
            source.rgba[(y * 24 + x) * 4] = 220;
            source.rgba[(y * 24 + x) * 4 + 3] = 255;
        }
        const auto rig = spratforge::rig::build_rig(source);
        check(rig.joints.size() == 15, "Expected articulated boxer joints");
        check(rig.regions.size() == 14, "Expected body, limb, glove and foot masks");
        check(spratforge::rig::validate_rig(rig).empty(), "Generated rig must validate");
        const auto json = spratforge::rig::rig_json(rig);
        check(spratforge::rig::rig_json(spratforge::rig::rig_from_json(json)) == json, "Rig JSON round trip");
        const auto changed = spratforge::rig::apply_overrides(rig, {{"joints", {{{"name", "head"}, {"x", 10}, {"confidence", 1000}}}}});
        check(changed.joints[2].x == 10 && changed.joints[2].confidence == 1000, "Joint override not applied");
        check(rig.joints[2].confidence != 1000, "Override mutated original rig");
        rejects([&] { spratforge::rig::apply_overrides(rig, {{"joints", {{{"name", "head"}, {"x", -1}}}}}); });
        rejects([&] { spratforge::rig::apply_overrides(rig, {{"joints", {{{"name", "unknown"}, {"x", 1}}}}}); });
        rejects([&] { auto invalid = json; invalid["width"] = 1.5; spratforge::rig::rig_from_json(invalid); });
        rejects([&] { auto invalid = json; invalid["silhouette"][0] = 256; spratforge::rig::rig_from_json(invalid); });
        auto invalid = rig;
        invalid.regions[1].pixels = invalid.regions[0].pixels;
        check(!spratforge::rig::validate_rig(invalid).empty(), "Overlapping masks must fail");
        invalid = rig; invalid.pivot.y -= 1;
        check(!spratforge::rig::diagnostics_json(spratforge::rig::validate_rig(invalid)).at("valid").get<bool>(), "Unstable pivot must fail");
        invalid = rig; invalid.bones[0].parent = "head";
        check(!spratforge::rig::validate_rig(invalid).empty(), "Cycles must fail");
        invalid = rig; invalid.regions[0].pixels.clear();
        check(!spratforge::rig::validate_rig(invalid).empty(), "Malformed mask must fail");
        invalid = rig; invalid.regions[0].name = "unknown";
        check(!spratforge::rig::validate_rig(invalid).empty(), "Missing required region must fail");
        rejects([&] { auto invalid_json = json; invalid_json["joints"] = nlohmann::json::object(); spratforge::rig::rig_from_json(invalid_json); });
        const auto path = std::filesystem::temp_directory_path() / "spratforge_phase1_rig.json";
        spratforge::rig::save_rig(path.string(), rig);
        check(spratforge::rig::rig_json(spratforge::rig::load_rig(path.string())) == json, "Rig file round trip");
        std::filesystem::remove(path);
        spratforge::core::Frame thin{1, 3, {1,2,3,255, 1,2,3,255, 1,2,3,255}};
        check(spratforge::rig::build_rig(thin).silhouette == std::vector<std::uint8_t>({1,1,1}), "Thin silhouette lost pixels");
        rejects([] { spratforge::rig::build_rig({}); });
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}