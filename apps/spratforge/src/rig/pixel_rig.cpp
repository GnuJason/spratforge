#include <spratforge/rig/pixel_rig.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <skeleton.hpp>

namespace spratforge::rig {
namespace {
void require_valid(const RigDefinition& rig) {
    const auto diagnostics = validate_rig(rig);
    if (!diagnostics.empty()) throw std::invalid_argument(diagnostics_json(diagnostics).dump());
}
nlohmann::json read_json(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Unable to read rig: " + path);
    nlohmann::json document;
    input >> document;
    return document;
}
int integer(const nlohmann::json& value) {
    if (!value.is_number_integer() || value < -1000000 || value > 1000000) {
        throw std::invalid_argument("Rig fields must be bounded integers");
    }
    return value.get<int>();
}
std::vector<std::uint8_t> mask(const nlohmann::json& value) {
    if (!value.is_array()) throw std::invalid_argument("Rig mask must be an array");
    std::vector<std::uint8_t> result;
    for (const auto& pixel : value) {
        const int entry = integer(pixel);
        if (entry != 0 && entry != 1) throw std::invalid_argument("Rig masks must be binary");
        result.push_back(static_cast<std::uint8_t>(entry));
    }
    return result;
}
}

RigDefinition build_rig(const core::Frame& source) {
    if (source.width < 1 || source.height < 1 || source.width > 4096 || source.height > 4096) {
        throw std::invalid_argument("Rig dimensions must be between 1 and 4096");
    }
    const auto anchor = anchor::extract_anchor(source);
    if (anchor.bounds.width == 0) throw std::invalid_argument("Rig requires visible source pixels");
    RigDefinition rig{.width = source.width, .height = source.height, .pivot = anchor.pivot,
                      .silhouette = anchor.silhouette, .joints = {}, .bones = {}, .regions = {}};
    const spratgen::Silhouette silhouette{source.width, source.height, anchor.silhouette, {}};
    const auto skeleton = spratgen::SkeletonBuilder{}.build(silhouette);
    const auto& bounds = anchor.bounds;
    const auto position_x = [&](int percent) { return bounds.x + (bounds.width - 1) * percent / 100; };
    const auto position_y = [&](int percent) { return bounds.y + (bounds.height - 1) * percent / 100; };
    const auto add_joint = [&](const std::string& name, int x, int y) {
        x = std::clamp(x, bounds.x, bounds.x + bounds.width - 1);
        y = std::clamp(y, bounds.y, bounds.y + bounds.height - 1);
        const int confidence = rig.silhouette[static_cast<std::size_t>(y) * rig.width + x] ? 750 : 250;
        rig.joints.push_back({name, x, y, confidence});
    };
    add_joint("torso", skeleton.torso.x, skeleton.torso.y);
    add_joint("neck", position_x(50), position_y(20));
    add_joint("head", skeleton.head.x, skeleton.head.y);
    rig.bones = {{"torso", "neck"}, {"neck", "head"}};
    for (const std::string side : {"left", "right"}) {
        const bool left = side == "left";
        add_joint(side + "_shoulder", position_x(left ? 30 : 70), position_y(28));
        add_joint(side + "_elbow", position_x(left ? 20 : 80), position_y(42));
        const auto& arm = left ? skeleton.left_arm : skeleton.right_arm;
        add_joint(side + "_wrist", arm.x, arm.y);
        add_joint(side + "_hip", position_x(left ? 40 : 60), position_y(62));
        add_joint(side + "_knee", position_x(left ? 35 : 65), position_y(79));
        add_joint(side + "_ankle", position_x(left ? 35 : 65), position_y(94));
        rig.bones.insert(rig.bones.end(), {{"torso", side + "_shoulder"},
            {side + "_shoulder", side + "_elbow"}, {side + "_elbow", side + "_wrist"},
            {"torso", side + "_hip"}, {side + "_hip", side + "_knee"},
            {side + "_knee", side + "_ankle"}});
    }
    segment_regions(rig);
    require_valid(rig);
    return rig;
}

nlohmann::json rig_json(const RigDefinition& rig) {
    require_valid(rig);
    nlohmann::json document{{"rig_version", 1}, {"width", rig.width}, {"height", rig.height},
        {"pivot", {{"x", rig.pivot.x}, {"y", rig.pivot.y}}}, {"silhouette", rig.silhouette},
        {"joints", nlohmann::json::array()}, {"bones", nlohmann::json::array()},
        {"regions", nlohmann::json::array()}};
    for (const auto& joint : rig.joints) document["joints"].push_back({{"name", joint.name},
        {"x", joint.x}, {"y", joint.y}, {"confidence", joint.confidence}});
    for (const auto& bone : rig.bones) document["bones"].push_back({{"parent", bone.parent}, {"child", bone.child}});
    for (const auto& region : rig.regions) document["regions"].push_back({{"name", region.name},
        {"joint", region.joint}, {"z_order", region.z_order}, {"pixels", region.pixels}});
    return document;
}

RigDefinition rig_from_json(const nlohmann::json& document) {
    if (integer(document.at("rig_version")) != 1) throw std::invalid_argument("Unsupported rig version");
    for (const std::string field : {"joints", "bones", "regions"}) {
        if (!document.at(field).is_array()) throw std::invalid_argument("Rig " + field + " must be an array");
    }
    RigDefinition rig;
    rig.width = integer(document.at("width")); rig.height = integer(document.at("height"));
    rig.pivot = {integer(document.at("pivot").at("x")), integer(document.at("pivot").at("y"))};
    rig.silhouette = mask(document.at("silhouette"));
    for (const auto& joint : document.at("joints")) rig.joints.push_back({joint.at("name").get<std::string>(),
        integer(joint.at("x")), integer(joint.at("y")), integer(joint.at("confidence"))});
    for (const auto& bone : document.at("bones")) rig.bones.push_back({bone.at("parent"), bone.at("child")});
    for (const auto& region : document.at("regions")) rig.regions.push_back({region.at("name"), region.at("joint"),
        integer(region.at("z_order")), mask(region.at("pixels"))});
    require_valid(rig);
    return rig;
}

RigDefinition apply_overrides(const RigDefinition& rig, const nlohmann::json& overrides) {
    auto document = rig_json(rig);
    if (!overrides.is_object()) throw std::invalid_argument("Rig overrides must be an object");
    for (auto entry = overrides.begin(); entry != overrides.end(); ++entry) {
        if (entry.key() == "joints") {
            if (!entry.value().is_array()) throw std::invalid_argument("Joint overrides must be an array");
            for (const auto& replacement : entry.value()) {
                const std::string name = replacement.at("name");
                auto& joints = document["joints"];
                const auto found = std::find_if(joints.begin(), joints.end(), [&](const auto& joint) { return joint.at("name") == name; });
                if (found == joints.end()) throw std::invalid_argument("Unknown override joint: " + name);
                for (auto field = replacement.begin(); field != replacement.end(); ++field) {
                    if (field.key() != "name" && field.key() != "x" && field.key() != "y" && field.key() != "confidence") {
                        throw std::invalid_argument("Unknown joint override field: " + field.key());
                    }
                    (*found)[field.key()] = field.value();
                }
            }
        } else if (entry.key() == "regions" || entry.key() == "bones" || entry.key() == "pivot") {
            document[entry.key()] = entry.value();
        } else {
            throw std::invalid_argument("Unknown rig override field: " + entry.key());
        }
    }
    return rig_from_json(document);
}
RigDefinition load_rig(const std::string& path) { return rig_from_json(read_json(path)); }
RigDefinition load_rig_overrides(const RigDefinition& rig, const std::string& path) {
    return apply_overrides(rig, read_json(path));
}
void save_rig(const std::string& path, const RigDefinition& rig) {
    const auto document = rig_json(rig);
    const std::filesystem::path output(path);
    if (output.has_parent_path()) std::filesystem::create_directories(output.parent_path());
    std::ofstream stream(path);
    stream << document.dump(2) << '\n';
    if (!stream) throw std::runtime_error("Unable to write rig: " + path);
}
}  // namespace spratforge::rig