#pragma once

#include <spratforge/profiles/generation_profiles.hpp>

namespace spratforge::audit {
using Diagnostics = std::vector<rig::Diagnostic>;
Diagnostics validate_source(const core::Frame& source, const profiles::RigProfile& profile,
                            const profiles::PaletteProfile& palette);
Diagnostics validate_rig_profile(const rig::RigDefinition& rig, const profiles::RigProfile& profile);
nlohmann::json validate_rig_file(const std::string& path, const profiles::ProfilePaths& profiles);
nlohmann::json audit_path(const std::string& path, const profiles::ProfilePaths& profiles);
Diagnostics validate_metadata(const nlohmann::json& metadata, const core::Frame& atlas);
}  // namespace spratforge::audit