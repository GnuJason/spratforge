#pragma once

#include <string>
#include <spratforge/core/output_dir.hpp>
#include <spratforge/profiles/generation_profiles.hpp>

namespace spratforge::pipeline {
// `policy` controls what happens when the output directory is not empty; see
// spratforge/core/output_dir.hpp. The default (Refuse) preserves the previous
// "Generate requires an empty output directory" behaviour, with a message
// that now names the two escapes.
bool generate(const std::string& sprite_path, const std::string& output_directory,
              const profiles::ProfilePaths& paths, std::string& error,
              core::OutputPolicy policy = core::OutputPolicy::Refuse);

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
