#include <spratforge/profiles/generation_profiles.hpp>
#include <spratforge/pipeline/pipeline_core.hpp>
#include <spratforge/core/renderer_core.hpp>
#include <spratforge/audit/audit_core.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "cli_test_support.hpp"

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
}
int main() {
    try {
        const auto profiles = spratforge::profiles::load_generation_profiles({});
        check(profiles.motions.size() == 7, "Expected seven motion profiles");
        for (const auto& profile : profiles.motions) {
            check(profile.durations.size() == static_cast<std::size_t>(profile.clip.frame_count), "Duration count mismatch");
            for (int frame = 0; frame < profile.clip.frame_count; ++frame) (void)spratforge::motion::sample_pose(profile.clip, frame);
        }
        const auto path = std::filesystem::temp_directory_path() / "spratforge_bad_motion.json";
        auto document = spratforge::profiles::read_json(spratforge::profiles::default_profile_path("motion/jab.json"));
        document["frames"] = 1.5;
        { std::ofstream file(path); file << document; }
        bool rejected = false;
        try { (void)spratforge::profiles::load_motion_profile(path.string()); } catch (const std::exception&) { rejected = true; }
        std::filesystem::remove(path);
        check(rejected, "Fractional frame counts must fail");
        const auto directory = std::filesystem::temp_directory_path() / "spratforge_phase2_generate";
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        spratforge::core::Frame source{24, 32, std::vector<std::uint8_t>(24 * 32 * 4, 0)};
        for (int y = 2; y < 30; ++y) for (int x = 4; x < 20; ++x) {
            source.rgba[(y * 24 + x) * 4] = 220;
            source.rgba[(y * 24 + x) * 4 + 3] = 255;
        }
        std::string error;
        check(spratforge::core::save_frame_png(source, (directory / "input.png").string(), error), error.c_str());
        check(spratforge::pipeline::generate((directory / "input.png").string(), (directory / "out").string(), {}, error), error.c_str());
        const auto audit = spratforge::audit::audit_path((directory / "out").string(), {});
        check(audit.at("valid").get<bool>(), audit.dump().c_str());
        const auto atlas = spratforge::profiles::read_json((directory / "out/atlas.json").string());
        check(atlas.at("frames").size() == 45 && atlas.at("animations").size() == 7, "Profile generation count mismatch");
        std::filesystem::remove_all(directory);
        cli_test::Workspace workspace("generate");
        const auto input = workspace.save_source();
        const auto output = (workspace.directory / "out").string();
        check(workspace.run({"generate", "--input", input, "--out", output}) == 0, "Generate CLI failed");
        check(workspace.report().at("valid").get<bool>(), "Generate report must be valid");
        const auto second = workspace.directory / "repeat";
        check(workspace.run({"generate", "--input", input, "--out", second.string()}) == 0, "Repeated generation failed");
        for (const auto& entry : std::filesystem::recursive_directory_iterator(output)) if (entry.is_regular_file()) {
            const auto relative = std::filesystem::relative(entry.path(), output);
            cli_test::check(cli_test::bytes(entry.path()) == cli_test::bytes(second / relative), "Nondeterministic file: " + relative.string());
        }
        check(workspace.run({"generate", "--input", input, "--out", output}) != 0, "Nonempty output must not be overwritten");
        check(!workspace.report().at("valid").get<bool>(), "Failure must be JSON");
        check(workspace.run({"generate", "--input", input}) == 2, "Missing output must fail argument validation");
        check(workspace.run({"generate", "--input", input, "--out", output, "--mode", "single"}) == 2, "Legacy flags must not leak into generate");
        check(workspace.run({"generate", "--input", input, "--out", output, "--input", input}) == 2, "Duplicate flags must fail");
        const auto blue = workspace.directory / "blue";
        check(workspace.run({"generate", "--input", input, "--out", blue.string(), "--variant", "blue"}) == 0, "Variant generation failed");
        check(cli_test::bytes(blue / "atlas.png") != cli_test::bytes(std::filesystem::path(output) / "atlas.png"), "Variant failed to change pixels");
        check(spratforge::profiles::read_json((blue / "atlas.json").string()).at("variant") == "blue", "Variant metadata missing");
        const auto motions = workspace.directory / "motions";
        std::filesystem::create_directories(motions);
        auto jab = spratforge::profiles::read_json(spratforge::profiles::default_profile_path("motion/jab.json"));
        jab["durations_ms"] = {10, 20, 30, 40, 50, 60};
        jab["hitboxes"] = {{{"frame", 2}, {"x", 1}, {"y", -4}, {"w", 3}, {"h", 2}, {"kind", "attack"}}};
        cli_test::write_json(motions / "jab.json", jab);
        const auto custom = workspace.directory / "custom";
        check(workspace.run({"generate", "--input", input, "--out", custom.string(), "--motion-dir", motions.string()}) == 0, "Custom motion generation failed");
        const auto custom_metadata = spratforge::profiles::read_json((custom / "atlas.json").string());
        check(custom_metadata.at("frames").size() == 6 && custom_metadata.at("frames").at(2).at("duration_ms") == 30, "Custom durations ignored");
        check(custom_metadata.at("frames").at(2).at("hitboxes").at(0).at("kind") == "attack", "Hitboxes missing");
        const auto bad = workspace.directory / "bad";
        jab["keyframes"][1]["transforms"][0]["rotation"] = 13;
        cli_test::write_json(motions / "jab.json", jab);
        check(workspace.run({"generate", "--input", input, "--out", bad.string(), "--motion-dir", motions.string()}) != 0, "Invalid rotation accepted");
        check(!std::filesystem::exists(bad), "Profile failure left partial outputs");
        auto export_profile = spratforge::profiles::read_json(spratforge::profiles::default_profile_path("export/ringqueen.json"));
        export_profile["columns"] = 3; export_profile["padding"] = 2;
        export_profile["atlas_png"] = "sprites.png"; export_profile["atlas_json"] = "sprites.json"; export_profile["manifest_json"] = "assets.json";
        export_profile["write_frames"] = false; export_profile["write_sheets"] = false;
        const auto export_path = workspace.directory / "export.json";
        cli_test::write_json(export_path, export_profile);
        const auto compact = workspace.directory / "compact";
        check(workspace.run({"generate", "--input", input, "--out", compact.string(), "--export-profile", export_path.string()}) == 0, "Custom export failed");
        check(std::filesystem::exists(compact / "sprites.png") && !std::filesystem::exists(compact / "idle"), "Export options ignored");
        check(workspace.run({"audit", "--input", compact.string(), "--export-profile", export_path.string()}) == 0, "Custom export audit failed");
        auto style = spratforge::profiles::read_json(spratforge::profiles::default_profile_path("palette/boxer_default.json"));
        style.erase("variants_file");
        style["locks"] = {{180,120,32}};
        style["animation_styles"] = {{"hit", {{"flash", {255,255,255}}}}, {"ko", {{"desaturate", true}}}};
        const auto style_path = workspace.directory / "style.json";
        cli_test::write_json(style_path, style);
        const auto styled_profile = spratforge::profiles::load_palette_profile(style_path.string());
        const auto source_rig = spratforge::rig::build_rig(workspace.source());
        const auto flashed = spratforge::profiles::apply_style(workspace.source(), source_rig, styled_profile, "default", "hit");
        check(flashed.rgba[(3 * 24 + 5) * 4] == 180, "Locked color changed");
        check(flashed.rgba[(10 * 24 + 10) * 4] == 255 && flashed.rgba[(10 * 24 + 10) * 4 + 1] == 255, "Hit flash not applied");
        const auto gray = spratforge::profiles::apply_style(workspace.source(), source_rig, styled_profile, "default", "ko");
        check(gray.rgba[(10 * 24 + 10) * 4] == gray.rgba[(10 * 24 + 10) * 4 + 1], "Desaturation not applied");
        const auto styled_output = workspace.directory / "styled";
        check(workspace.run({"generate", "--input", input, "--out", styled_output.string(), "--palette-profile", style_path.string()}) == 0, "Style profile CLI failed");
        check(workspace.run({"audit", "--input", styled_output.string(), "--palette-profile", style_path.string()}) == 0, "Styled audit failed");
        export_profile["atlas_png"] = "../escape.png";
        cli_test::write_json(export_path, export_profile);
        check(workspace.run({"generate", "--input", input, "--out", bad.string(), "--export-profile", export_path.string()}) != 0, "Escaping export path accepted");
        check(workspace.run({"--mode", "single", "--input", input, "--out", (workspace.directory / "single.png").string()}) == 0, "Legacy single mode regressed");
        check(workspace.run({"--mode", "profile", "--input", input, "--profile", "idle_6", "--out", (workspace.directory / "legacy").string()}) == 0, "Legacy profile mode regressed");
        check(workspace.run({"--mode", "atlas", "--input", input, "--profile", "idle_6", "--atlas", "3x2", "--out", (workspace.directory / "legacy/atlas.png").string()}) == 0, "Legacy atlas mode regressed");
        check(workspace.run({"--mode", "ai-motion", "--input", input, "--motion", "1,0", "--out", (workspace.directory / "legacy/motion.png").string()}) == 0, "Legacy motion mode regressed");
        check(workspace.run({"--turnkey", input, "--out", (workspace.directory / "turnkey").string()}) == 0, "Legacy turnkey mode regressed");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}