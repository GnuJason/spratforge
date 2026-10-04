#pragma once

// spratforge::forge — turn-key character pipeline.
//
// One call turns a single neutral sprite into a complete, game-ready
// animation set:
//
//   <out>/frames/<animation>/<animation>_NNN.png   individual frames
//   <out>/sheets/<animation>.png                   horizontal strip sheets
//   <out>/metadata.json                            canonical manifest
//   <out>/rig.json                                 the estimated rig
//   <out>/rig_debug.png                            joint + segmentation overlay
//
// The run is fully deterministic: no RNG, no timestamps, no filesystem
// iteration order dependencies. The same input and options always produce
// byte-identical files.

#include <string>
#include <vector>

#include <json/json.hpp>
#include <spratforge/core/output_dir.hpp>
#include <spratforge/forge/forge_motion.hpp>
#include <spratforge/forge/forge_rig.hpp>
#include <spratforge/forge/forge_segment.hpp>
#include <spratforge/forge/forge_synth.hpp>

namespace spratforge::forge {

struct ForgeOptions {
    std::string input_path;
    std::string output_dir;
    std::string character = "boxer";
    std::string rig_override_path;  // empty: use the estimated rig as-is
    int fps_override = 0;           // 0: use each clip's authored fps
    int scale = 1;                  // integer nearest-neighbour magnification
    int alpha_threshold = 16;
    int margin = 2;                 // extra canvas padding, in source pixels
    bool emit_sheets = true;
    bool emit_debug = true;

    // Phase 4A additions.
    // Empty: emit every clip. Otherwise: emit only these clip names, in the
    // library's canonical order (so --only never changes frame ordering).
    std::vector<std::string> only;
    core::OutputPolicy output_policy = core::OutputPolicy::Refuse;
    RenderQuality quality{};
    // Reserved. The pipeline contains no RNG at all, so a seed cannot change
    // the output; it is accepted, recorded in metadata and validated so that
    // build scripts can pass it unconditionally and so a future stochastic
    // stage stays reproducible.
    unsigned int seed = 0;
};

// Accepted input sprite dimensions. Below 32 px there is not enough body to
// estimate a rig from; above 2048 px a single run would allocate tens of GB
// across the frame set. Non-square inputs are fully supported: every rig and
// motion amplitude is expressed as a fraction of the measured body bounds.
inline constexpr int kMinInputDimension = 32;
inline constexpr int kMaxInputDimension = 2048;

struct ForgeResult {
    Rig rig;
    Segmentation segmentation;
    CanvasLayout layout;
    int frame_width = 0;   // after `scale`
    int frame_height = 0;  // after `scale`
    int total_frames = 0;
    nlohmann::json metadata;
    std::vector<std::string> written_files;  // relative to output_dir, sorted
};

// Validates options, throwing std::invalid_argument on bad input.
void validate_options(const ForgeOptions& options);

// Runs the whole pipeline and writes every artefact.
ForgeResult run_forge(const ForgeOptions& options);

// Builds the metadata document without touching the filesystem. Exposed so
// tests can assert on the schema directly.
nlohmann::json build_metadata(const ForgeOptions& options,
                              const Rig& rig,
                              const CanvasLayout& layout,
                              int frame_width,
                              int frame_height,
                              const std::vector<Clip>& clips);

// Canonical metadata schema version emitted by this pipeline. Consumers gate
// on this; it stays at 2 and every Phase 4A field is additive.
inline constexpr int kForgeMetadataSchemaVersion = 2;

// Minor revision within schema_version 2. Bumped whenever fields are ADDED.
// A consumer that only knows minor 0 keeps working unchanged.
inline constexpr int kForgeMetadataSchemaMinor = 1;

// Names of every clip the library can emit, in emission order. Used by
// `--list-animations` and to validate `--only`.
std::vector<std::string> available_animation_names();

}  // namespace spratforge::forge
