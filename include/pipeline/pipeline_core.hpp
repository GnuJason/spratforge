#pragma once

#include <string>

namespace spratforge::pipeline {

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
