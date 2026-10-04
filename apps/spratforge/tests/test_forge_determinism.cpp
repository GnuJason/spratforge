// Determinism test: running the forge pipeline twice with identical inputs
// must produce byte-identical outputs.
#include <spratforge/forge/forge_pipeline.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::map<std::string, std::string> snapshot(const std::filesystem::path& root) {
    std::map<std::string, std::string> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        files[std::filesystem::relative(entry.path(), root).generic_string()] = read_bytes(entry.path());
    }
    return files;
}

}  // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "spratforge_determinism";
    fs::remove_all(root);

    spratforge::forge::ForgeOptions options;
    options.input_path = "tests/fixtures/neutral_boxer.png";
    options.character = "neutral";
    options.scale = 2;

    std::map<std::string, std::string> first;
    for (int run = 0; run < 2; ++run) {
        const fs::path out = root / (run == 0 ? "run_a" : "run_b");
        options.output_dir = out.string();
        try {
            spratforge::forge::run_forge(options);
        } catch (const std::exception& error) {
            std::cerr << "FAIL: run " << run << " threw: " << error.what() << '\n';
            return 1;
        }
        auto files = snapshot(out);
        if (files.empty()) {
            std::cerr << "FAIL: run " << run << " produced no files\n";
            return 1;
        }
        if (run == 0) {
            first = std::move(files);
            continue;
        }
        if (files.size() != first.size()) {
            std::cerr << "FAIL: file count differs: " << first.size() << " vs " << files.size() << '\n';
            return 1;
        }
        for (const auto& [name, bytes] : first) {
            const auto found = files.find(name);
            if (found == files.end()) {
                std::cerr << "FAIL: missing in second run: " << name << '\n';
                return 1;
            }
            if (found->second != bytes) {
                std::cerr << "FAIL: bytes differ for " << name << '\n';
                return 1;
            }
        }
    }

    std::cout << "test_forge_determinism: " << first.size() << " files byte-identical across two runs\n";
    return 0;
}
