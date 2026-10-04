// Phase 4A regression test: non-empty output directory policy.
//
// spratforge used to either refuse every non-empty directory outright
// (`generate`) or write straight over it (`forge`). Both are now routed
// through spratforge::core::prepare_output_dir:
//
//   Refuse (default) - fail with an actionable message
//   Force            - write in place, delete nothing
//   Clean            - delete ONLY the paths recorded in this tool's own
//                      manifest, then write
//
// The safety property under test is the last one: a file spratforge did not
// write must survive `--clean`, and the run must then still refuse rather
// than silently mixing someone else's work into the output.

#include <spratforge/core/output_dir.hpp>
#include <spratforge/forge/forge_pipeline.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace core = spratforge::core;

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void write_file(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
    check(static_cast<bool>(stream), "Cannot write " + path.string());
}

// Returns the thrown message, or an empty string when the call succeeded.
std::string refusal_for(const fs::path& directory, core::OutputPolicy policy) {
    try {
        core::prepare_output_dir(directory.string(), policy, "forge");
    } catch (const std::exception& error) {
        return error.what();
    }
    return {};
}

void forge_into(const fs::path& out, core::OutputPolicy policy) {
    spratforge::forge::ForgeOptions options;
    options.input_path = "tests/fixtures/neutral_boxer.png";
    options.output_dir = out.string();
    options.only = {"idle"};
    options.emit_debug = false;
    options.output_policy = policy;
    spratforge::forge::run_forge(options);
}

}  // namespace

int main() {
    try {
        const fs::path workspace = fs::temp_directory_path() / "spratforge_output_policy";
        fs::remove_all(workspace);
        fs::create_directories(workspace);

        // --- A missing or empty directory is always accepted. ---------------
        check(refusal_for(workspace / "fresh", core::OutputPolicy::Refuse).empty(),
              "A missing directory must be accepted");
        check(fs::exists(workspace / "fresh"), "prepare_output_dir should create the directory");
        check(refusal_for(workspace / "fresh", core::OutputPolicy::Refuse).empty(),
              "An existing but empty directory must be accepted");

        // --- create_directory=false leaves nothing behind. ------------------
        core::prepare_output_dir((workspace / "not_created").string(), core::OutputPolicy::Refuse, "generate", false);
        check(!fs::exists(workspace / "not_created"),
              "prepare_output_dir(create_directory=false) must not create the directory");

        // --- Refuse names the escapes. --------------------------------------
        const fs::path occupied = workspace / "occupied";
        write_file(occupied / "notes.txt", "hand written\n");
        const auto refusal = refusal_for(occupied, core::OutputPolicy::Refuse);
        check(!refusal.empty(), "A non-empty directory must be refused by default");
        check(refusal.find("--force") != std::string::npos && refusal.find("--clean") != std::string::npos,
              "The refusal must name both escapes: " + refusal);
        check(fs::exists(occupied / "notes.txt"), "Refusing must not delete anything");

        // --- Force writes in place and deletes nothing. ----------------------
        check(refusal_for(occupied, core::OutputPolicy::Force).empty(), "--force must accept a non-empty directory");
        check(fs::exists(occupied / "notes.txt"), "--force must never delete unrelated files");

        // --- Clean with no manifest deletes nothing and still refuses. -------
        const auto clean_without_manifest = refusal_for(occupied, core::OutputPolicy::Clean);
        check(!clean_without_manifest.empty(), "--clean on a directory spratforge never wrote must refuse");
        check(fs::exists(occupied / "notes.txt"), "--clean must never delete a file spratforge did not write");

        // --- The real round trip: forge, re-forge, and protect a user file. --
        const fs::path out = workspace / "run";
        forge_into(out, core::OutputPolicy::Refuse);
        check(fs::exists(out / "metadata.json"), "First forge did not write metadata.json");
        check(fs::exists(out / core::kOutputManifestFileName), "First forge did not write a manifest");

        const auto manifest = core::read_output_manifest(out.string());
        check(!manifest.empty(), "The manifest must list the written files");
        check(std::find(manifest.begin(), manifest.end(), std::string("metadata.json")) != manifest.end(),
              "The manifest must list metadata.json");
        check(std::find(manifest.begin(), manifest.end(), std::string(core::kOutputManifestFileName)) == manifest.end(),
              "The manifest must not list itself");

        // A second default run refuses: the directory is no longer empty.
        bool refused_second_run = false;
        try {
            forge_into(out, core::OutputPolicy::Refuse);
        } catch (const std::exception&) {
            refused_second_run = true;
        }
        check(refused_second_run, "Re-running forge into a populated directory must refuse by default");

        // --clean removes exactly the manifest's paths and re-forges cleanly.
        forge_into(out, core::OutputPolicy::Clean);
        check(fs::exists(out / "metadata.json"), "--clean run did not rewrite metadata.json");

        // Now drop a user file in and prove --clean leaves it alone.
        write_file(out / "my_notes.txt", "do not delete me\n");
        write_file(out / "frames" / "keepme.txt", "nested user file\n");
        bool refused_after_user_file = false;
        try {
            forge_into(out, core::OutputPolicy::Clean);
        } catch (const std::exception& error) {
            refused_after_user_file = true;
            const std::string message = error.what();
            check(message.find("did not write") != std::string::npos,
                  "--clean must explain that foreign files remain: " + message);
        }
        check(refused_after_user_file, "--clean must refuse while foreign files remain");
        check(fs::exists(out / "my_notes.txt"), "--clean deleted a user file at the top level");
        check(fs::exists(out / "frames" / "keepme.txt"), "--clean deleted a nested user file");

        // --force still writes alongside the user's files.
        forge_into(out, core::OutputPolicy::Force);
        check(fs::exists(out / "metadata.json"), "--force run did not write metadata.json");
        check(fs::exists(out / "my_notes.txt"), "--force deleted a user file");

        fs::remove_all(workspace);
        std::cout << "test_output_policy: ok\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << "test_output_policy: " << failure.what() << '\n';
        return 1;
    }
}
