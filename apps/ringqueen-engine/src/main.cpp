#include <rq/assets/sprite_asset.hpp>
#include <iostream>

int main(int argc, char** argv) {
    if (argc > 2) {
        std::cerr << "Usage: ringqueen-engine [manifest.json]\n";
        return 2;
    }
    try {
        const auto boxer = rq::assets::SpriteAsset::load(argc == 2 ? argv[1] : RQ_DEFAULT_MANIFEST);
        const auto& idle = boxer.frame_at("idle", 0);
        std::cout << "RingQueen loaded " << boxer.animations().size() << " animations, " << boxer.frames().size()
                  << " frames; idle canvas " << idle.rectangle.width << 'x' << idle.rectangle.height << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RingQueen asset load failed: " << error.what() << '\n';
        return 1;
    }
}