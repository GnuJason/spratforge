#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <cstdint>
#include <filesystem>

namespace rq::assets {
struct Diagnostic {
    std::string path;
    std::string message;
};

std::vector<Diagnostic> validate_sprite_schema(std::string_view document);

struct Rect { int x, y, width, height; };
struct Point { int x, y; };
struct Hitbox { Rect bounds; std::string kind; };
struct SpriteFrame {
    Rect rectangle;
    Point pivot;
    int duration_ms;
    std::vector<Hitbox> hitboxes;
};
struct AnimationEvent { std::size_t frame; std::string name; };
struct Animation {
    std::string name;
    std::size_t first_frame, frame_count;
    int fps;
    bool loop;
    std::uint64_t duration_ms;
    std::vector<AnimationEvent> events;
};

class SpriteAsset {
public:
    static SpriteAsset load(const std::filesystem::path& manifest_path);
    int width() const { return width_; }
    int height() const { return height_; }
    const std::vector<std::uint8_t>& rgba() const { return rgba_; }
    const std::vector<SpriteFrame>& frames() const { return frames_; }
    const std::vector<Animation>& animations() const { return animations_; }
    const std::string& variant() const { return variant_; }
    const std::string& palette_name() const { return palette_name_; }
    const std::vector<std::array<std::uint8_t, 3>>& palette() const { return palette_; }
    const Animation& animation(std::string_view name) const;
    const SpriteFrame& frame_at(std::string_view name, std::uint64_t elapsed_ms) const;
private:
    SpriteAsset() = default;
    int width_ = 0, height_ = 0;
    std::vector<std::uint8_t> rgba_;
    std::vector<SpriteFrame> frames_;
    std::vector<Animation> animations_;
    std::string variant_, palette_name_;
    std::vector<std::array<std::uint8_t, 3>> palette_;
};
}