#include "cli_test_support.hpp"
#include <rq/assets/sprite_asset.hpp>
#include <iostream>
#include <limits>

namespace {
using cli_test::check;
using Json = nlohmann::json;
spratforge::core::Frame neutral_boxer() {
    spratforge::core::Frame frame{32, 48, std::vector<std::uint8_t>(32 * 48 * 4, 0)};
    const auto rectangle = [&](int left, int top, int width, int height, std::array<std::uint8_t, 3> color) {
        for (int row = top; row < top + height; ++row) for (int column = left; column < left + width; ++column) {
            const auto offset = (row * frame.width + column) * 4;
            for (std::size_t channel = 0; channel < 3; ++channel) frame.rgba[offset + channel] = color[channel];
            frame.rgba[offset + 3] = 255;
        }
    };
    rectangle(12, 3, 8, 10, {180,120,72});
    rectangle(12, 3, 8, 3, {32,24,24});
    rectangle(14, 12, 4, 3, {180,120,72});
    rectangle(9, 15, 14, 14, {180,120,72});
    rectangle(5, 16, 4, 14, {180,120,72});
    rectangle(23, 16, 4, 14, {180,120,72});
    rectangle(3, 25, 6, 7, {220,32,32});
    rectangle(23, 25, 6, 7, {220,32,32});
    rectangle(9, 29, 14, 7, {32,32,128});
    rectangle(9, 29, 14, 2, {240,240,240});
    rectangle(9, 36, 5, 7, {180,120,72});
    rectangle(18, 36, 5, 7, {180,120,72});
    rectangle(7, 42, 7, 4, {32,24,24});
    rectangle(18, 42, 7, 4, {32,24,24});
    return frame;
}
}
int main(int argc, char** argv) {
    try {
        const std::filesystem::path fixtures(RQ_FIXTURE_DIR);
        const bool update = argc == 2 && std::string(argv[1]) == "--update-fixtures";
        check(argc == 1 || update, "Only --update-fixtures is supported");
        if (update) {
            std::filesystem::create_directories(fixtures);
            std::string error;
            check(spratforge::core::save_frame_png(neutral_boxer(), (fixtures / "neutral_boxer.png").string(), error), error);
        }
        cli_test::Workspace workspace("ringqueen");
        const auto output = workspace.directory / "generated";
        check(workspace.run({"generate", "--input", (fixtures / "neutral_boxer.png").string(), "--out", output.string()}) == 0, "Neutral boxer generation failed");
        auto asset = rq::assets::SpriteAsset::load(output / "manifest.json");
        check(asset.animations().size() == 7 && asset.frames().size() == 45, "Full animation set not loaded");
        for (const auto* name : {"idle", "walk", "jab", "block", "hit", "ko", "specials"}) {
            const auto& animation = asset.animation(name);
            std::uint64_t elapsed = 0;
            for (std::size_t index = animation.first_frame; index < animation.first_frame + animation.frame_count; ++index) {
                check(&asset.frame_at(name, elapsed) == &asset.frames().at(index), "Frame duration boundary incorrect");
                elapsed += asset.frames().at(index).duration_ms;
            }
            const auto end_frame = animation.loop ? animation.first_frame : animation.first_frame + animation.frame_count - 1;
            check(&asset.frame_at(name, elapsed) == &asset.frames().at(end_frame), "Loop or clamp incorrect");
            (void)asset.frame_at(name, std::numeric_limits<std::uint64_t>::max());
        }
        check(!asset.animation("jab").events.empty(), "Animation event binding missing");
        check(asset.variant() == "default" && asset.palette_name() == "boxer_source" && !asset.palette().empty(), "Palette binding missing");
        bool unknown = false;
        try { (void)asset.animation("missing"); } catch (const std::out_of_range&) { unknown = true; }
        check(unknown, "Unknown animation accepted");
        for (const auto* filename : {"atlas.png", "atlas.json", "manifest.json"}) {
            const auto golden = fixtures / (std::string("expected_") + filename);
            if (update) std::filesystem::copy_file(output / filename, golden, std::filesystem::copy_options::overwrite_existing);
            check(cli_test::bytes(output / filename) == cli_test::bytes(golden), "Golden fixture mismatch: " + std::string(filename));
        }
        const auto repeat = workspace.directory / "repeat";
        check(workspace.run({"generate", "--input", (fixtures / "neutral_boxer.png").string(), "--out", repeat.string()}) == 0, "Repeated generation failed");
        for (const auto& entry : std::filesystem::recursive_directory_iterator(output)) if (entry.is_regular_file()) {
            check(cli_test::bytes(entry.path()) == cli_test::bytes(repeat / std::filesystem::relative(entry.path(), output)), "Generation is not byte deterministic");
        }
        const auto motions = workspace.directory / "motions";
        std::filesystem::create_directories(motions);
        auto jab = spratforge::profiles::read_json(spratforge::profiles::default_profile_path("motion/jab.json"));
        jab["durations_ms"] = {10,20,30,40,50,60};
        jab["hitboxes"] = {{{"frame",2},{"x",-3},{"y",-8},{"w",5},{"h",4},{"kind","attack"}}};
        cli_test::write_json(motions / "jab.json", jab);
        const auto custom = workspace.directory / "custom";
        check(workspace.run({"generate", "--input", (fixtures / "neutral_boxer.png").string(), "--out", custom.string(),
            "--variant", "blue", "--motion-dir", motions.string()}) == 0, "Custom generation failed");
        const auto custom_asset = rq::assets::SpriteAsset::load(custom / "manifest.json");
        check(custom_asset.variant() == "blue" && custom_asset.animation("jab").duration_ms == 210, "Variant or explicit timing not bound");
        const auto& strike = custom_asset.frame_at("jab", 30);
        check(strike.duration_ms == 30 && strike.hitboxes.size() == 1 && strike.hitboxes.front().bounds.x == -3 &&
            strike.hitboxes.front().bounds.y == -8 && strike.hitboxes.front().kind == "attack", "Pivot-relative hitbox binding failed");
        const auto atlas_path = output / "atlas.json", manifest_path = output / "manifest.json";
        const auto original = spratforge::profiles::read_json(atlas_path.string());
        const auto original_manifest = spratforge::profiles::read_json(manifest_path.string());
        const auto reject = [&](const Json& metadata) {
            cli_test::write_json(atlas_path, metadata);
            auto manifest = metadata;
            for (const auto* key : {"atlas_reference", "rig_reference", "anchor_reference"}) manifest[key] = original_manifest.at(key);
            cli_test::write_json(manifest_path, manifest);
            bool rejected = false;
            try { (void)rq::assets::SpriteAsset::load(manifest_path); } catch (const std::exception&) { rejected = true; }
            check(rejected, "Corrupt runtime asset accepted");
        };
        auto bad = original; bad["schema_version"] = 2; reject(bad);
        bad = original; bad["animations"][1]["first_frame"] = 0; reject(bad);
        bad = original; bad["animations"][0]["frame_count"] = 1000; reject(bad);
        bad = original; bad["frames"][0]["pivot"]["x"] = 8191; reject(bad);
        bad = original; bad["frames"][0]["x"] = 1; reject(bad);
        bad = original; bad["animations"][0]["events"] = {{{"frame",999},{"name","late"}}}; reject(bad);
        bad = original; bad["palette"]["colors"] = {{0,0,0}}; reject(bad);
        bad = original; bad["image"] = "../outside.png"; reject(bad);
        cli_test::write_json(atlas_path, original);
        cli_test::write_json(manifest_path, original_manifest);
        auto manifest = original_manifest; manifest["variant"] = "mismatch";
        cli_test::write_json(manifest_path, manifest);
        bool rejected = false;
        try { (void)rq::assets::SpriteAsset::load(manifest_path); } catch (const std::exception&) { rejected = true; }
        check(rejected, "Mismatched manifest accepted");
        cli_test::write_json(manifest_path, original_manifest);
        spratforge::core::Frame wrong_png{1, 1, {0,0,0,255}};
        std::string png_error;
        check(spratforge::core::save_frame_png(wrong_png, (output / "atlas.png").string(), png_error), png_error);
        rejected = false;
        try { (void)rq::assets::SpriteAsset::load(manifest_path); } catch (const std::exception&) { rejected = true; }
        check(rejected, "PNG dimension mismatch accepted");
        { std::ofstream stream(output / "atlas.png", std::ios::binary); stream << "not a PNG"; }
        rejected = false;
        try { (void)rq::assets::SpriteAsset::load(manifest_path); } catch (const std::exception&) { rejected = true; }
        check(rejected, "Malformed PNG accepted");
        std::filesystem::remove(output / "atlas.png");
        rejected = false;
        try { (void)rq::assets::SpriteAsset::load(manifest_path); } catch (const std::exception&) { rejected = true; }
        check(rejected, "Missing atlas accepted");
    #ifndef _WIN32
        std::filesystem::create_symlink(repeat / "atlas.png", output / "atlas.png");
        rejected = false;
        try { (void)rq::assets::SpriteAsset::load(manifest_path); } catch (const std::exception&) { rejected = true; }
        check(rejected, "Escaping symlink accepted");
    #endif
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}