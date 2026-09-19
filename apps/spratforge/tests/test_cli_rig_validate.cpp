#include "cli_test_support.hpp"
#include <iostream>

int main() {
    using cli_test::check;
    try {
        cli_test::Workspace workspace("rig validate");
        const auto rig = spratforge::rig::build_rig(workspace.source());
        const auto path = workspace.directory / "rig.json";
        const auto original = spratforge::rig::rig_json(rig);
        cli_test::write_json(path, original);
        check(workspace.run({"rig-validate", "--rig", path.string()}) == 0, "Valid rig rejected");
        check(workspace.report().at("valid").get<bool>(), "Valid diagnostic missing");
        const auto report = workspace.directory / "report.json";
        check(workspace.run({"rig-validate", "--rig", path.string(), "--out", report.string()}) == 0, "Report file failed");
        check(spratforge::profiles::read_json(report.string()) == workspace.report(), "Report differs from stdout");
        check(workspace.run({"rig-validate", "--rig", path.string(), "--out", path.string()}) != 0, "Report overwrote input rig");
        check(spratforge::profiles::read_json(path.string()) == original, "Input rig changed");
        auto invalid = original;
        invalid["joints"][0]["x"] = -1;
        cli_test::write_json(path, invalid);
        check(workspace.run({"rig-validate", "--rig", path.string()}) == 5, "Joint bounds should fail");
        check(!workspace.report().at("valid").get<bool>() && !workspace.report().at("diagnostics").empty(), "Invalid rig diagnostics missing");
        check(workspace.report().at("diagnostics").at(0).at("code") == "joint_bounds", "Rig diagnostics must not be nested JSON strings");
        invalid = original;
        invalid["regions"][1]["pixels"] = invalid["regions"][0]["pixels"];
        cli_test::write_json(path, invalid);
        check(workspace.run({"rig-validate", "--rig", path.string()}) != 0, "Mask overlap accepted");
        cli_test::write_json(path, original);
        const auto override_path = workspace.directory / "override.json";
        cli_test::write_json(override_path, {{"joints", {{{"name", "head"}, {"x", 12}, {"confidence", 1000}}}}});
        check(workspace.run({"rig-validate", "--rig", path.string(), "--rig-override", override_path.string()}) == 0, "Valid overrides rejected");
        cli_test::write_json(override_path, {{"pivot", {{"x", 0}, {"y", 0}}}});
        check(workspace.run({"rig-validate", "--rig", path.string(), "--rig-override", override_path.string()}) != 0, "Unstable pivot accepted");
        auto profile = spratforge::profiles::read_json(spratforge::profiles::default_profile_path("rig/boxer_default.json"));
        profile["min_confidence"] = 1000;
        const auto profile_path = workspace.directory / "profile.json";
        cli_test::write_json(profile_path, profile);
        check(workspace.run({"rig-validate", "--rig", path.string(), "--rig-profile", profile_path.string()}) != 0, "Confidence constraint ignored");
        check(workspace.run({"rig-validate"}) == 2, "Missing rig not rejected");
        check(workspace.run({"rig-validate", "--rig", (workspace.directory / "missing.json").string()}) == 5, "Missing rig file not rejected");
        { std::ofstream file(path); file << "{"; }
        check(workspace.run({"rig-validate", "--rig", path.string()}) == 5, "Malformed JSON not rejected");
        check(!workspace.report().at("valid").get<bool>(), "Malformed JSON diagnostics not structured");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}