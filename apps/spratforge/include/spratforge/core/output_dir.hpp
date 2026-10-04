#pragma once

// spratforge::core — output directory policy.
//
// Every generating subcommand writes a manifest (`.spratforge-manifest.json`)
// into its output directory listing exactly the relative paths it created.
// That manifest is the ONLY thing `--clean` is ever allowed to delete, so a
// user who points spratforge at a directory containing their own work can
// never lose it:
//
//   default   refuse to touch a non-empty directory, naming both escapes
//   --force   write over whatever is there, leaving unrelated files alone
//   --clean   delete the files listed in this tool's own manifest, then write
//
// `--clean` on a directory with no manifest is treated as an empty manifest:
// nothing is deleted and, if the directory is non-empty, the run still
// refuses. Deleting files spratforge did not write is never an option.

#include <string>
#include <vector>

namespace spratforge::core {

enum class OutputPolicy {
    Refuse,  // default: fail if the directory exists and is not empty
    Force,   // overwrite in place, do not delete anything
    Clean    // delete this tool's previously written files, then write
};

// Name of the manifest file written into every output directory.
inline constexpr const char* kOutputManifestFileName = ".spratforge-manifest.json";

// Applies `policy` to `output_dir`. `subcommand` is used in the diagnostics
// ("forge", "generate", ...). Throws std::invalid_argument with an actionable
// message when the policy refuses, and std::runtime_error when a filesystem
// operation fails.
//
// `create_directory` is false for callers that validate first and only create
// the directory once they are certain they will write into it, so a failed
// run never leaves an empty directory behind.
void prepare_output_dir(const std::string& output_dir,
                        OutputPolicy policy,
                        const std::string& subcommand,
                        bool create_directory = true);

// Reads the relative paths recorded in the directory's manifest. Returns an
// empty vector when the manifest is absent or unreadable.
std::vector<std::string> read_output_manifest(const std::string& output_dir);

// Writes the manifest. `relative_files` must already be sorted; the manifest
// contains no timestamps so repeated runs stay byte-identical.
void write_output_manifest(const std::string& output_dir,
                           const std::string& subcommand,
                           const std::vector<std::string>& relative_files);

}  // namespace spratforge::core
