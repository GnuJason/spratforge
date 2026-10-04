// End-to-end CLI test for `spratforge forge`.
#include <spratforge/forge/forge_pipeline.hpp>

#include <cstdlib>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifndef _WIN32
#include <sys/wait.h>
#endif

int main() {
    namespace fs = std::filesystem;
    const fs::path out = fs::temp_directory_path() / "spratforge_cli_forge";
    fs::remove_all(out);

    const std::string command = std::string(SPRATFORGE_CLI_PATH) +
                                " forge --input tests/fixtures/neutral_boxer.png --out " + out.string() +
                                " --character neutral --scale 2";
    if (std::system((command + " > /dev/null 2>&1").c_str()) != 0) {
        std::cerr << "FAIL: forge command failed: " << command << '\n';
        return 1;
    }

    const fs::path metadata_path = out / "metadata.json";
    if (!fs::exists(metadata_path)) {
        std::cerr << "FAIL: metadata.json missing\n";
        return 1;
    }
    std::ifstream input(metadata_path);
    nlohmann::json metadata;
    input >> metadata;

    if (metadata.value("schema_version", 0) != 2) {
        std::cerr << "FAIL: unexpected schema_version\n";
        return 1;
    }
    if (metadata.value("character", std::string{}) != "neutral") {
        std::cerr << "FAIL: character not propagated\n";
        return 1;
    }
    for (const char* key : {"frame", "anchor", "animations", "layout", "totals", "source"}) {
        if (!metadata.contains(key)) {
            std::cerr << "FAIL: metadata missing " << key << '\n';
            return 1;
        }
    }
    const int frame_width = metadata["frame"].value("width", 0);
    const int frame_height = metadata["frame"].value("height", 0);
    if (frame_width <= 0 || frame_height <= 0) {
        std::cerr << "FAIL: bad frame size\n";
        return 1;
    }

    bool saw_attack = false;
    bool saw_loop = false;
    bool saw_hold = false;
    for (const auto& animation : metadata["animations"]) {
        const std::string name = animation.value("name", std::string{});
        const int count = animation.value("frame_count", 0);
        if (count <= 0) {
            std::cerr << "FAIL: " << name << " has no frames\n";
            return 1;
        }
        if (animation.value("loop", false)) saw_loop = true;
        if (animation.value("hold_last_frame", false)) saw_hold = true;
        if (animation.value("is_attack", false)) {
            saw_attack = true;
            const int impact = animation.value("impact_frame", -1);
            if (impact < 0 || impact >= count) {
                std::cerr << "FAIL: " << name << " has an out-of-range impact frame\n";
                return 1;
            }
        }
        for (const auto& frame : animation["frames"]) {
            const fs::path file = out / frame.value("file", std::string{});
            if (!fs::exists(file)) {
                std::cerr << "FAIL: missing frame file " << file << '\n';
                return 1;
            }
        }
        const fs::path sheet = out / animation["sheet"].value("file", std::string{});
        if (!fs::exists(sheet)) {
            std::cerr << "FAIL: missing sheet " << sheet << '\n';
            return 1;
        }
    }
    if (!saw_attack || !saw_loop || !saw_hold) {
        std::cerr << "FAIL: expected attack, looping and hold-last animations\n";
        return 1;
    }
    if (!fs::exists(out / "rig.json") || !fs::exists(out / "rig_debug.png")) {
        std::cerr << "FAIL: rig.json / rig_debug.png missing\n";
        return 1;
    }

    // --- Phase 4A: knockdown must exist and be distinct from ko. ----------
    const auto find_animation = [&metadata](const std::string& name) -> nlohmann::json {
        for (const auto& animation : metadata["animations"]) {
            if (animation.value("name", std::string{}) == name) return animation;
        }
        return nlohmann::json{};
    };
    const auto knockdown = find_animation("knockdown");
    const auto knocked_out = find_animation("ko");
    if (knockdown.is_null() || knockdown.empty()) {
        std::cerr << "FAIL: knockdown animation missing\n";
        return 1;
    }
    if (knocked_out.is_null() || knocked_out.empty()) {
        std::cerr << "FAIL: ko animation missing\n";
        return 1;
    }
    for (const auto* entry : {&knockdown, &knocked_out}) {
        const std::string name = entry->value("name", std::string{});
        if (entry->value("loop", true)) {
            std::cerr << "FAIL: " << name << " must not loop\n";
            return 1;
        }
        if (!entry->value("hold_last_frame", false)) {
            std::cerr << "FAIL: " << name << " must hold its last frame\n";
            return 1;
        }
        if (entry->value("is_attack", true)) {
            std::cerr << "FAIL: " << name << " must not be flagged as an attack\n";
            return 1;
        }
    }
    if (knockdown.value("frame_count", 0) == knocked_out.value("frame_count", 0)) {
        std::cerr << "FAIL: knockdown and ko have identical frame counts; they are not distinct clips\n";
        return 1;
    }
    {
        // Distinct pixels, not just distinct metadata.
        const fs::path knockdown_sheet = out / knockdown["sheet"].value("file", std::string{});
        const fs::path ko_sheet = out / knocked_out["sheet"].value("file", std::string{});
        const auto read_all = [](const fs::path& path) {
            std::ifstream stream(path, std::ios::binary);
            return std::string{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        };
        if (read_all(knockdown_sheet).empty() || read_all(knockdown_sheet) == read_all(ko_sheet)) {
            std::cerr << "FAIL: knockdown and ko rendered identical sheets\n";
            return 1;
        }
    }

    // --- Phase 4A: directional walks, with `walk` kept as an alias. --------
    for (const char* name : {"walk", "walk_left", "walk_right", "walk_up", "walk_down"}) {
        const auto animation = find_animation(name);
        if (animation.is_null() || animation.empty()) {
            std::cerr << "FAIL: missing animation " << name << '\n';
            return 1;
        }
        if (!animation.value("loop", false)) {
            std::cerr << "FAIL: " << name << " must loop\n";
            return 1;
        }
    }
    if (find_animation("walk").value("alias_of", std::string{}) != "walk_down") {
        std::cerr << "FAIL: `walk` must be documented as an alias of walk_down\n";
        return 1;
    }
    for (const char* name : {"walk_left", "walk_right", "walk_up", "walk_down"}) {
        if (find_animation(name).value("direction", std::string{}).empty()) {
            std::cerr << "FAIL: " << name << " must carry a direction in metadata\n";
            return 1;
        }
    }
    {
        const auto left = find_animation("walk_left");
        const auto right = find_animation("walk_right");
        const auto read_all = [](const fs::path& path) {
            std::ifstream stream(path, std::ios::binary);
            return std::string{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        };
        const auto left_bytes = read_all(out / left["sheet"].value("file", std::string{}));
        const auto right_bytes = read_all(out / right["sheet"].value("file", std::string{}));
        if (left_bytes.empty() || left_bytes == right_bytes) {
            std::cerr << "FAIL: walk_left and walk_right are not visually distinct\n";
            return 1;
        }
    }

    // --- Phase 4A: CLI ergonomics. -----------------------------------------
    const auto run = [](const std::string& arguments) {
        const int status = std::system((std::string(SPRATFORGE_CLI_PATH) + " " + arguments).c_str());
        return status == -1 ? -1 : WEXITSTATUS(status);
    };
    const auto capture = [](const std::string& arguments) {
        const fs::path log = fs::temp_directory_path() / "spratforge_cli_forge_capture.txt";
        std::system((std::string(SPRATFORGE_CLI_PATH) + " " + arguments + " > " + log.string() + " 2>&1").c_str());
        std::ifstream stream(log);
        return std::string{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    };

    if (run("--version > /dev/null 2>&1") != 0) {
        std::cerr << "FAIL: --version must succeed\n";
        return 1;
    }
    if (capture("--version").find("spratforge") == std::string::npos) {
        std::cerr << "FAIL: --version must print the tool name and version\n";
        return 1;
    }
    for (const char* subcommand : {"forge", "generate", "rig-validate", "audit"}) {
        if (run(std::string(subcommand) + " --help > /dev/null 2>&1") != 0) {
            std::cerr << "FAIL: " << subcommand << " --help must exit 0\n";
            return 1;
        }
        const auto help = capture(std::string(subcommand) + " --help");
        if (help.find(subcommand) == std::string::npos || help.find("Examples:") == std::string::npos) {
            std::cerr << "FAIL: " << subcommand << " --help must name the subcommand and show examples\n";
            return 1;
        }
    }

    // --list-animations lists every clip the library can emit.
    if (run("forge --list-animations > /dev/null 2>&1") != 0) {
        std::cerr << "FAIL: --list-animations must work without --input/--out\n";
        return 1;
    }
    {
        const auto listing = capture("forge --list-animations");
        for (const auto& name : spratforge::forge::available_animation_names()) {
            if (listing.find(name) == std::string::npos) {
                std::cerr << "FAIL: --list-animations omitted " << name << '\n';
                return 1;
            }
        }
    }

    // --only emits exactly the requested clips, in canonical order.
    {
        const fs::path only_out = fs::temp_directory_path() / "spratforge_cli_forge_only";
        fs::remove_all(only_out);
        if (run("forge --input tests/fixtures/neutral_boxer.png --out " + only_out.string() +
                " --only jab,idle --quiet > /dev/null 2>&1") != 0) {
            std::cerr << "FAIL: --only run failed\n";
            return 1;
        }
        std::ifstream only_stream(only_out / "metadata.json");
        nlohmann::json only_metadata;
        only_stream >> only_metadata;
        if (only_metadata["animations"].size() != 2 ||
            only_metadata["animations"][0].value("name", std::string{}) != "idle" ||
            only_metadata["animations"][1].value("name", std::string{}) != "jab") {
            std::cerr << "FAIL: --only must emit exactly the requested clips in canonical order\n";
            return 1;
        }
        fs::remove_all(only_out);
    }

    // Clear, non-zero-exit errors.
    if (run("forge --input x.png > /dev/null 2>&1") == 0) {
        std::cerr << "FAIL: forge without --out should fail\n";
        return 1;
    }
    {
        const auto message = capture("forge --input tests/fixtures/neutral_boxer.png --out " +
                                     (fs::temp_directory_path() / "spratforge_cli_forge_bad").string() +
                                     " --only nope");
        if (message.find("nope") == std::string::npos || message.find("Available") == std::string::npos) {
            std::cerr << "FAIL: an unknown --only clip must be named alongside the available ones\n";
            return 1;
        }
    }
    // Re-running into the populated directory must refuse, and say how to proceed.
    {
        const auto message = capture("forge --input tests/fixtures/neutral_boxer.png --out " + out.string());
        if (message.find("--force") == std::string::npos || message.find("--clean") == std::string::npos) {
            std::cerr << "FAIL: refusing a non-empty output directory must name --force and --clean\n";
            return 1;
        }
    }

    std::cout << "test_cli_forge: ok\n";
    return 0;
}
