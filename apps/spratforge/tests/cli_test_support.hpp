#pragma once

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifndef _WIN32
#include <sys/wait.h>
#endif
#include <spratforge/core/renderer_core.hpp>
#include <spratforge/profiles/generation_profiles.hpp>

namespace cli_test {
using Json = nlohmann::json;
inline void check(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
inline void write_json(const std::filesystem::path& path, const Json& value) {
    std::ofstream stream(path); stream << value.dump(2) << '\n'; check(static_cast<bool>(stream), "Cannot write test JSON");
}
inline std::string bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary); check(static_cast<bool>(stream), "Cannot read test output: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
inline std::string quote(const std::string& value) {
#ifdef _WIN32
    check(value.find('"') == std::string::npos, "Unsupported test path");
    return "\"" + value + "\"";
#else
    std::string result = "'";
    for (char character : value) result += character == '\'' ? "'\\''" : std::string(1, character);
    return result + "'";
#endif
}
struct Workspace {
    std::filesystem::path directory;
    explicit Workspace(const std::string& name) : directory(std::filesystem::temp_directory_path() /
        ("spratforge " + name + " " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directories(directory);
    }
    ~Workspace() { std::error_code error; std::filesystem::remove_all(directory, error); }
    int run(const std::vector<std::string>& arguments) const {
        std::string command = quote(SPRATFORGE_CLI_PATH);
        for (const auto& argument : arguments) command += " " + quote(argument);
        command += " > " + quote((directory / "stdout.json").string()) + " 2> " + quote((directory / "stderr.txt").string());
        const int status = std::system(command.c_str());
        check(status != -1, "Failed to launch CLI");
#ifdef _WIN32
        return status;
#else
        check(WIFEXITED(status), "CLI terminated by signal: " + bytes(directory / "stderr.txt"));
        return WEXITSTATUS(status);
#endif
    }
    Json report() const { return spratforge::profiles::read_json((directory / "stdout.json").string()); }
    static spratforge::core::Frame source() {
        spratforge::core::Frame frame{24, 32, std::vector<std::uint8_t>(24 * 32 * 4, 0)};
        for (int y = 2; y < 30; ++y) for (int x = 3; x < 21; ++x) {
            const auto offset = (y * 24 + x) * 4;
            frame.rgba[offset] = y < 8 ? 180 : 220;
            frame.rgba[offset + 1] = y < 8 ? 120 : 32;
            frame.rgba[offset + 2] = 32;
            frame.rgba[offset + 3] = 255;
        }
        return frame;
    }
    std::string save_source(const spratforge::core::Frame& frame = source()) const {
        std::string error;
        const auto path = (directory / "input.png").string();
        check(spratforge::core::save_frame_png(frame, path, error), error);
        return path;
    }
};
}  // namespace cli_test