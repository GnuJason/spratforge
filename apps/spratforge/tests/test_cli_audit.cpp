#include "cli_test_support.hpp"
#include <iostream>

int main() {
    using cli_test::check;
    try {
        cli_test::Workspace workspace("audit");
        auto source = workspace.source();
        const auto input = workspace.save_source(source);
        check(workspace.run({"audit", "--input", input}) == 0, "Valid source rejected");
        const auto report = workspace.directory / "audit.json";
        check(workspace.run({"audit", "--input", input, "--out", report.string()}) == 0, "Audit report output failed");
        check(spratforge::profiles::read_json(report.string()) == workspace.report(), "Audit report mismatch");
        source.rgba[(2 * 24 + 3) * 4 + 3] = 128;
        workspace.save_source(source);
        check(workspace.run({"audit", "--input", input}) == 5, "Fractional alpha accepted");
        source = workspace.source();
        for (std::size_t offset = 3; offset < source.rgba.size(); offset += 4) source.rgba[offset] = 255;
        workspace.save_source(source);
        check(workspace.run({"audit", "--input", input}) == 5, "Opaque background accepted");
        workspace.save_source();
        auto palette = spratforge::profiles::read_json(spratforge::profiles::default_profile_path("palette/boxer_default.json"));
        palette.erase("variants_file"); palette["allowed_colors"] = {{0,0,0}};
        const auto palette_path = workspace.directory / "palette.json";
        cli_test::write_json(palette_path, palette);
        check(workspace.run({"audit", "--input", input, "--palette-profile", palette_path.string()}) == 5, "Disallowed source palette accepted");
        const auto output = workspace.directory / "out";
        check(workspace.run({"generate", "--input", input, "--out", output.string()}) == 0, "Audit fixture generation failed");
        check(workspace.run({"audit", "--input", output.string()}) == 0, "Valid generated assets rejected");
        const auto atlas_path = output / "atlas.json";
        const auto original = spratforge::profiles::read_json(atlas_path.string());
        const auto reject = [&](const cli_test::Json& metadata) {
            cli_test::write_json(atlas_path, metadata);
            check(workspace.run({"audit", "--input", output.string()}) == 5, "Invalid output metadata accepted");
            check(!workspace.report().at("valid").get<bool>(), "Output failure must be JSON diagnostics");
        };
        auto invalid = original; invalid["schema_version"] = 2; reject(invalid);
        invalid = original; invalid["frames"][0]["duration_ms"] = 0; reject(invalid);
        invalid = original; invalid["frames"][0]["pivot"]["x"] = -1; reject(invalid);
        invalid = original; invalid["frames"][0]["x"] = 1; reject(invalid);
        invalid = original; invalid["animations"][1]["first_frame"] = 0; reject(invalid);
        invalid = original; invalid["palette"]["colors"] = {{0,0,0}}; reject(invalid);
        invalid = original; invalid["image"] = "../input.png"; reject(invalid);
        invalid = original; invalid["hitbox_space"] = "unknown"; reject(invalid);
        cli_test::write_json(atlas_path, original);
        const auto original_anchor = spratforge::profiles::read_json((output / "anchor.json").string());
        auto bad_anchor = original_anchor;
        bad_anchor["silhouette"][0] = 1;
        cli_test::write_json(output / "anchor.json", bad_anchor);
        check(workspace.run({"audit", "--input", output.string()}) == 5, "Mismatched anchor accepted");
        cli_test::write_json(output / "anchor.json", original_anchor);
        auto restrictive = spratforge::profiles::read_json(spratforge::profiles::default_profile_path("export/ringqueen.json"));
        restrictive["padding"] = 3;
        const auto export_path = workspace.directory / "export.json";
        cli_test::write_json(export_path, restrictive);
        check(workspace.run({"audit", "--input", output.string(), "--export-profile", export_path.string()}) == 5, "Explicit export profile ignored");
        check(workspace.run({"audit", "--input", output.string(), "--palette-profile", palette_path.string()}) == 5, "Output palette constraint ignored");
        std::filesystem::remove(output / "atlas.png");
        check(workspace.run({"audit", "--input", output.string()}) == 5, "Missing atlas PNG accepted");
        check(workspace.run({"audit", "--input", (workspace.directory / "missing.png").string()}) == 5, "Missing source accepted");
        check(workspace.run({"audit"}) == 2, "Audit missing input not rejected");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}