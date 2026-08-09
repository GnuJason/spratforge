#pragma once

#include <string>
#include <vector>

namespace spratgen {

class Config {
public:
    std::vector<std::string> args;
    std::string inputPath;
    std::string outputDir = ".";
    std::string animName;
    int frameCount = 0;

    static Config fromArgs(int argc, char** argv);
};

}  // namespace spratgen