#include <spratforge/core/output_dir.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <system_error>

#include <json/json.hpp>

namespace spratforge::core {
namespace {

namespace fs = std::filesystem;

bool directory_has_entries(const fs::path& dir) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) {
            return false;
        }
        if (entry.path().filename() == kOutputManifestFileName) {
            continue;  // our own bookkeeping never counts as user content
        }
        return true;
    }
    return false;
}

// Rejects anything that could escape the output directory. The manifest is a
// file on disk and could in principle be edited, so it is never trusted
// blindly: only plain relative paths below the output directory are deletable.
bool is_safe_relative(const std::string& relative) {
    if (relative.empty() || relative.front() == '/' || relative.front() == '\\') {
        return false;
    }
    if (relative.find("..") != std::string::npos) {
        return false;
    }
    if (relative.find(':') != std::string::npos) {
        return false;
    }
    return true;
}

void remove_empty_parents(const fs::path& root, fs::path dir) {
    std::error_code ec;
    while (dir != root && dir.has_relative_path()) {
        if (!fs::exists(dir, ec) || !fs::is_empty(dir, ec) || ec) {
            return;
        }
        if (!fs::remove(dir, ec) || ec) {
            return;
        }
        dir = dir.parent_path();
    }
}

}  // namespace

std::vector<std::string> read_output_manifest(const std::string& output_dir) {
    const fs::path manifest_path = fs::path(output_dir) / kOutputManifestFileName;
    std::error_code ec;
    if (!fs::exists(manifest_path, ec) || ec) {
        return {};
    }
    std::ifstream stream(manifest_path);
    if (!stream) {
        return {};
    }
    nlohmann::json document;
    try {
        stream >> document;
    } catch (const std::exception&) {
        return {};
    }
    if (!document.is_object() || !document.contains("files") || !document["files"].is_array()) {
        return {};
    }
    std::vector<std::string> files;
    for (const auto& entry : document["files"]) {
        if (entry.is_string()) {
            files.push_back(entry.get<std::string>());
        }
    }
    return files;
}

void write_output_manifest(const std::string& output_dir,
                           const std::string& subcommand,
                           const std::vector<std::string>& relative_files) {
    nlohmann::json document;
    document["format"] = "spratforge-output-manifest";
    document["version"] = 1;
    document["subcommand"] = subcommand;
    std::vector<std::string> sorted = relative_files;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    document["files"] = sorted;

    const fs::path manifest_path = fs::path(output_dir) / kOutputManifestFileName;
    std::ofstream stream(manifest_path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("spratforge: unable to write " + manifest_path.string());
    }
    // Two-space indent + trailing newline, no timestamps: byte-identical on
    // every re-run with the same inputs.
    stream << document.dump(2) << "\n";
}

void prepare_output_dir(const std::string& output_dir,
                        OutputPolicy policy,
                        const std::string& subcommand,
                        bool create_directory) {
    const fs::path root(output_dir);
    std::error_code ec;

    if (fs::exists(root, ec) && !fs::is_directory(root, ec)) {
        throw std::invalid_argument("spratforge " + subcommand + ": --out '" + output_dir +
                                    "' exists and is not a directory");
    }

    if (policy == OutputPolicy::Clean && fs::exists(root, ec)) {
        const std::vector<std::string> previous = read_output_manifest(output_dir);
        for (const std::string& relative : previous) {
            if (!is_safe_relative(relative)) {
                continue;  // never follow a manifest entry out of the output dir
            }
            const fs::path target = root / relative;
            if (!fs::exists(target, ec) || fs::is_directory(target, ec)) {
                continue;
            }
            fs::remove(target, ec);
            remove_empty_parents(root, target.parent_path());
        }
        fs::remove(root / kOutputManifestFileName, ec);
    }

    const bool blocked = policy == OutputPolicy::Refuse || policy == OutputPolicy::Clean;
    if (blocked && fs::exists(root, ec) && directory_has_entries(root)) {
        const std::string reason =
            policy == OutputPolicy::Clean
                ? "still contains files that spratforge did not write"
                : "is not empty";
        throw std::invalid_argument(
            "spratforge " + subcommand + ": output directory '" + output_dir + "' " + reason +
            ".\n  --clean  removes only the files recorded in " +
            std::string(kOutputManifestFileName) + " from a previous run\n" +
            "  --force  writes over the directory without deleting anything");
    }

    if (!create_directory) {
        return;
    }
    fs::create_directories(root, ec);
    if (ec) {
        throw std::runtime_error("spratforge " + subcommand + ": unable to create '" + output_dir +
                                 "': " + ec.message());
    }
}

}  // namespace spratforge::core
