#include <rq/assets/sprite_asset.hpp>
#include <json/json.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}
int main() {
    try {
        using Json = nlohmann::json;
        const auto valid = Json::parse(R"({
            "schema_version":1,"format":"ringqueen","hitbox_space":"pivot_relative",
            "image":"atlas.png","size":[1,1],"columns":1,"rows":1,"padding":0,
            "variant":"default","palette":{"name":"source","colors":[[0,0,0]]},
            "frames":[{"index":0,"x":0,"y":0,"w":1,"h":1,"pivot":{"x":0,"y":0},"duration_ms":125}],
            "animations":[{"name":"idle","first_frame":0,"frame_count":1,"fps":8,"loop":true,"events":[]}]
        })");
        check(rq::assets::validate_sprite_schema(valid.dump()).empty(), "Valid atlas rejected");
        auto manifest = valid;
        manifest["atlas_reference"] = "atlas.json";
        manifest["rig_reference"] = "rig.json";
        manifest["anchor_reference"] = "anchor.json";
        check(rq::assets::validate_sprite_schema(manifest.dump()).empty(), "Valid manifest rejected");
        const auto reject = [](const Json& document) {
            check(!rq::assets::validate_sprite_schema(document.dump()).empty(), "Invalid schema input accepted");
        };
        for (auto field = valid.begin(); field != valid.end(); ++field) {
            auto missing = valid; missing.erase(field.key()); reject(missing);
        }
        auto bad = valid; bad["schema_version"] = 2; reject(bad);
        bad = valid; bad["extra"] = true; reject(bad);
        bad = valid; bad["frames"][0]["duration_ms"] = 0; reject(bad);
        bad = valid; bad["frames"][0]["duration_ms"] = 1.5; reject(bad);
        bad = valid; bad["frames"][0]["pivot"]["x"] = "0"; reject(bad);
        bad = valid; bad["animations"][0]["loop"] = 1; reject(bad);
        bad = valid; bad["image"] = "../atlas.png"; reject(bad);
        bad = valid; bad["palette"]["colors"] = {{256,0,0}}; reject(bad);
        bad = valid; bad["frames"][0]["hitboxes"] = {{{"x",0},{"y",0},{"w",0},{"h",1},{"kind","attack"}}}; reject(bad);
        manifest.erase("rig_reference"); reject(manifest);
        check(!rq::assets::validate_sprite_schema("{").empty(), "Malformed JSON accepted");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}