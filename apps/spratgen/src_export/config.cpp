#include "config.hpp"

#include "pose_model.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <utility>

namespace spratgen {

namespace {

[[noreturn]] void fail_parse(std::string_view message) {
    std::cerr << "spratgen: " << message << '\n';
    std::exit(1);
}

bool is_valid_animation(const std::string& name) {
    const PoseModel poseModel;
    const std::vector<std::string> names = poseModel.getAnimationNames();
    return std::find(names.begin(), names.end(), name) != names.end();
}

int parse_frame_count(const std::string& rawValue) {
    try {
        const int parsed = std::stoi(rawValue);
        if (parsed <= 0) {
            fail_parse("--frames must be greater than zero");
        }
        return parsed;
    } catch (...) {
        fail_parse("--frames expects an integer value");
    }
}

std::string require_value(int argc, char** argv, int& index, std::string_view flag) {
    if (index + 1 >= argc || argv[index + 1] == nullptr) {
        fail_parse(std::string(flag) + " expects a value");
    }
    ++index;
    return argv[index];
}

}  // namespace

Config Config::fromArgs(int argc, char** argv) {
    Config config;
    config.args.reserve(argc > 0 ? static_cast<std::size_t>(argc) : 0U);
    for (int index = 0; index < argc; ++index) {
        config.args.emplace_back(argv[index] != nullptr ? argv[index] : "");
    }

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] != nullptr ? argv[index] : "";
        if (argument == "--input") {
            config.inputPath = require_value(argc, argv, index, "--input");
            continue;
        }

        if (argument == "--output") {
            config.outputDir = require_value(argc, argv, index, "--output");
            continue;
        }

        if (argument == "--anim") {
            config.animName = require_value(argc, argv, index, "--anim");
            if (!is_valid_animation(config.animName)) {
                fail_parse("invalid animation template: " + config.animName);
            }
            continue;
        }

        if (argument == "--frames") {
            config.frameCount = parse_frame_count(require_value(argc, argv, index, "--frames"));
            continue;
        }
    }

    return config;
}

}  // namespace spratgen