#include <rq/assets/sprite_asset.hpp>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: rq_asset_validate <manifest.json>\n";
        return 2;
    }
    try {
        const auto sprite = rq::assets::SpriteAsset::load(argv[1]);
        std::cout << "Validated " << sprite.animations().size() << " animations, " << sprite.frames().size() << " frames\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Invalid sprite asset: " << error.what() << '\n';
        return 1;
    }
}