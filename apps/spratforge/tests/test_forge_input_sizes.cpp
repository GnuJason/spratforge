// Phase 4A regression test: arbitrary neutral-input dimensions.
//
// Before Phase 4A the forge pipeline was only exercised with the small
// 32x48 fixture and silently mis-scaled the rig/templates for anything
// else. The rig and every motion amplitude are now expressed as a fraction
// of the measured body bounds, so a 400x400 or a non-square input produces
// the same relative pose set.
//
// What this asserts:
//   * a 400x400 input forges successfully end to end (the explicit size
//     called out in the Phase 4A brief),
//   * a non-square 200x320 input forges successfully,
//   * motion amplitude really does scale with the input: the forged canvas
//     of the 400x400 run is materially larger than the 100x100 run,
//   * inputs below kMinInputDimension and above kMaxInputDimension are
//     rejected with an actionable message rather than crashing or
//     allocating tens of gigabytes.

#include <spratforge/core/renderer_core.hpp>
#include <spratforge/forge/forge_pipeline.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using spratforge::core::Frame;

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// Nearest-neighbour resample. Keeps alpha strictly binary, which is what the
// forge front-end expects from a neutral input sprite.
Frame resize_nearest(const Frame& source, int width, int height) {
    Frame result{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4, 0)};
    for (int y = 0; y < height; ++y) {
        const int source_y = std::min(source.height - 1, y * source.height / height);
        for (int x = 0; x < width; ++x) {
            const int source_x = std::min(source.width - 1, x * source.width / width);
            const auto from = (static_cast<std::size_t>(source_y) * source.width + source_x) * 4;
            const auto to = (static_cast<std::size_t>(y) * width + x) * 4;
            for (int channel = 0; channel < 4; ++channel) result.rgba[to + channel] = source.rgba[from + channel];
        }
    }
    return result;
}

std::string write_resized(const Frame& source, const fs::path& directory, int width, int height) {
    const auto path = (directory / ("input_" + std::to_string(width) + "x" + std::to_string(height) + ".png")).string();
    std::string error;
    check(spratforge::core::save_frame_png(resize_nearest(source, width, height), path, error), error);
    return path;
}

// Runs forge into a fresh directory and returns the result.
spratforge::forge::ForgeResult forge_into(const std::string& input, const fs::path& out) {
    fs::remove_all(out);
    spratforge::forge::ForgeOptions options;
    options.input_path = input;
    options.output_dir = out.string();
    options.emit_debug = false;
    return spratforge::forge::run_forge(options);
}

// Returns the rejection message, or an empty string when the run succeeded.
std::string rejection_for(const std::string& input, const fs::path& out) {
    try {
        forge_into(input, out);
    } catch (const std::exception& error) {
        return error.what();
    }
    return {};
}

}  // namespace

int main() {
    try {
        const fs::path workspace = fs::temp_directory_path() / "spratforge_input_sizes";
        fs::remove_all(workspace);
        fs::create_directories(workspace);

        Frame fixture;
        std::string error;
        check(spratforge::core::load_frame_png("tests/fixtures/neutral_boxer.png", fixture, error), error);
        check(fixture.width > 0 && fixture.height > 0, "Fixture did not load");

        // --- The headline case: a 400x400 neutral input. -------------------
        const auto square_path = write_resized(fixture, workspace, 400, 400);
        const auto square = forge_into(square_path, workspace / "out_400");
        check(square.frame_width > 0 && square.frame_height > 0, "400x400 input produced an empty canvas");
        check(square.total_frames > 0, "400x400 input produced no frames");
        check(static_cast<int>(square.metadata["animations"].size()) ==
                  static_cast<int>(spratforge::forge::available_animation_names().size()),
              "400x400 input did not emit the full animation set");
        for (const auto& animation : square.metadata["animations"]) {
            for (const auto& frame : animation["frames"]) {
                check(fs::exists(workspace / "out_400" / frame.value("file", std::string{})),
                      "400x400 input is missing a frame file");
            }
        }

        // --- Non-square input. ---------------------------------------------
        const auto tall_path = write_resized(fixture, workspace, 200, 320);
        const auto tall = forge_into(tall_path, workspace / "out_200x320");
        check(tall.frame_width > 0 && tall.frame_height > 0, "Non-square input produced an empty canvas");
        check(tall.total_frames == square.total_frames, "Non-square input emitted a different frame count");
        // 200x320 is 1.6x taller than wide where 400x400 is square, so the
        // forged canvas must be relatively taller too. (It is not necessarily
        // taller than it is wide in absolute terms: the punch clips extend the
        // canvas horizontally well past the body silhouette.)
        const double square_ratio = static_cast<double>(square.frame_height) / square.frame_width;
        const double tall_ratio = static_cast<double>(tall.frame_height) / tall.frame_width;
        check(tall_ratio > square_ratio,
              "A taller input must forge a relatively taller canvas (square " + std::to_string(square_ratio) +
                  " vs tall " + std::to_string(tall_ratio) + ")");

        // --- The rig and motion amplitudes track the input bounds. ----------
        const auto small_path = write_resized(fixture, workspace, 100, 100);
        const auto small = forge_into(small_path, workspace / "out_100");
        check(square.frame_width > small.frame_width * 2 && square.frame_height > small.frame_height * 2,
              "Canvas did not scale with the input bounds: the rig is still using absolute pixel offsets");

        // --- Out-of-range inputs are rejected, not crashed on. --------------
        const auto tiny_path = write_resized(fixture, workspace, 16, 16);
        const auto tiny_message = rejection_for(tiny_path, workspace / "out_tiny");
        check(!tiny_message.empty(), "A 16x16 input must be rejected");
        check(tiny_message.find(std::to_string(spratforge::forge::kMinInputDimension)) != std::string::npos,
              "The too-small message must name the minimum dimension: " + tiny_message);

        const auto huge_path = write_resized(fixture, workspace, spratforge::forge::kMaxInputDimension + 1, 64);
        const auto huge_message = rejection_for(huge_path, workspace / "out_huge");
        check(!huge_message.empty(), "An oversized input must be rejected");
        check(huge_message.find(std::to_string(spratforge::forge::kMaxInputDimension)) != std::string::npos,
              "The too-large message must name the maximum dimension: " + huge_message);

        fs::remove_all(workspace);
        std::cout << "test_forge_input_sizes: ok\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << "test_forge_input_sizes: " << failure.what() << '\n';
        return 1;
    }
}
