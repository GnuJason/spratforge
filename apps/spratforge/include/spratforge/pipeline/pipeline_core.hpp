#pragma once

#include <string>
#include <spratforge/profiles/generation_profiles.hpp>

namespace spratforge::pipeline {
bool generate(const std::string& sprite_path, const std::string& output_directory,
              const profiles::ProfilePaths& paths, std::string& error);

struct PipelineConfig {
    std::string template_directory;
    std::string palette_name = "source";
};

class Pipeline {
public:
    explicit Pipeline(PipelineConfig config = {});
    bool run(const std::string& sprite_path, const std::string& output_directory, std::string& error) const;

private:
    PipelineConfig config_;
};

}  // namespace spratforge::pipeline
