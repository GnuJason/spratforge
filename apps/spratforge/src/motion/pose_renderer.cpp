#include <spratforge/motion/pose.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

#include <pose_model.hpp>


namespace spratforge::motion {
namespace {
constexpr std::int64_t unit = 1024;
int rounded(std::int64_t numerator, std::int64_t denominator) {
    if (denominator < 0) { numerator = -numerator; denominator = -denominator; }
    if (!denominator) throw std::invalid_argument("Singular pixel transform");
    return static_cast<int>(numerator >= 0 ? (numerator + denominator / 2) / denominator
                                          : -((-numerator + denominator / 2) / denominator));
}
struct Matrix {
    std::int64_t xx = unit, xy = 0, yx = 0, yy = unit, tx = 0, ty = 0;
};
struct Point { int x = 0; int y = 0; };
Point mapped(const Matrix& matrix, int x, int y) {
    return {rounded(matrix.xx * x + matrix.xy * y + matrix.tx, unit),
            rounded(matrix.yx * x + matrix.yy * y + matrix.ty, unit)};
}
Matrix compose(const Matrix& parent, const Matrix& local) {
    Matrix result{
        rounded(parent.xx * local.xx + parent.xy * local.yx, unit),
        rounded(parent.xx * local.xy + parent.xy * local.yy, unit),
        rounded(parent.yx * local.xx + parent.yy * local.yx, unit),
        rounded(parent.yx * local.xy + parent.yy * local.yy, unit),
        rounded(parent.xx * local.tx + parent.xy * local.ty, unit) + parent.tx,
        rounded(parent.yx * local.tx + parent.yy * local.ty, unit) + parent.ty};
    if (std::abs(result.xx) > 8 * unit || std::abs(result.xy) > 8 * unit ||
        std::abs(result.yx) > 8 * unit || std::abs(result.yy) > 8 * unit ||
        std::abs(result.tx) > 32768 * unit || std::abs(result.ty) > 32768 * unit) {
        throw std::invalid_argument("Composed transform exceeds pixel canvas limits");
    }
    return result;
}
void validate_transform(const JointTransform& transform) {
    if (transform.joint.empty() || transform.rotation < -360 || transform.rotation > 360 ||
        transform.rotation % 15 != 0 || transform.scale_x < 50 || transform.scale_x > 200 ||
        transform.scale_y < 50 || transform.scale_y > 200 || transform.dx < -4096 || transform.dx > 4096 ||
        transform.dy < -4096 || transform.dy > 4096) {
        throw std::invalid_argument("Transforms require 15-degree steps, 50..200 percent scales and bounded integer offsets");
    }
}
Matrix local_matrix(const rig::Joint& joint, const JointTransform& transform) {
    constexpr std::array<int, 24> sine{0,265,512,724,887,989,1024,989,887,724,512,265,
                                     0,-265,-512,-724,-887,-989,-1024,-989,-887,-724,-512,-265};
    const int angle = (transform.rotation / 15 + 24) % 24;
    Matrix result;
    result.xx = rounded(sine[(angle + 6) % 24] * transform.scale_x, 100);
    result.xy = rounded(-sine[angle] * transform.scale_y, 100);
    result.yx = rounded(sine[angle] * transform.scale_x, 100);
    result.yy = rounded(sine[(angle + 6) % 24] * transform.scale_y, 100);
    result.tx = unit * (joint.x + transform.dx) - result.xx * joint.x - result.xy * joint.y;
    result.ty = unit * (joint.y + transform.dy) - result.yx * joint.x - result.yy * joint.y;
    return result;
}
struct Bounds {
    int left = 0, top = 0, right = 0, bottom = 0;
    void include(Point point) {
        left = std::min(left, point.x); right = std::max(right, point.x);
        top = std::min(top, point.y); bottom = std::max(bottom, point.y);
    }
};
struct Layer {
    const rig::RegionMask* region;
    Matrix linear;
    Point shift;
    Bounds bounds;
};
std::size_t pixel_index(int x, int y, int width) { return static_cast<std::size_t>(y) * width + x; }

PoseDefinition adapted_pose(const spratgen::PoseSkeleton& source) {
    return {{{"torso", source.torso.x, source.torso.y},
        {"head", source.head.x - source.torso.x, source.head.y - source.torso.y},
        {"left_shoulder", source.left_arm.x - source.torso.x, source.left_arm.y - source.torso.y},
        {"right_shoulder", source.right_arm.x - source.torso.x, source.right_arm.y - source.torso.y},
        {"left_hip", source.left_leg.x - source.torso.x, source.left_leg.y - source.torso.y},
        {"right_hip", source.right_leg.x - source.torso.x, source.right_leg.y - source.torso.y}}};
}
Interpolation curve(const std::string& name) {
    if (name == "easeIn") return Interpolation::ease_in;
    if (name == "easeOut") return Interpolation::ease_out;
    if (name == "easeInOut") return Interpolation::ease_in_out;
    if (name == "cubic") return Interpolation::smoothstep;
    return Interpolation::linear;
}
std::map<std::string, JointTransform> transforms(const PoseDefinition& pose) {
    std::map<std::string, JointTransform> result;
    for (const auto& transform : pose.transforms) {
        validate_transform(transform);
        if (!result.emplace(transform.joint, transform).second) throw std::invalid_argument("Duplicate pose joint");
    }
    return result;
}
}

MotionClip default_motion(const std::string& name) {
    const spratgen::PoseModel model;
    const std::string inherited = name == "run" ? "walk" : name == "victory" ? "block" : name;
    const auto& source = model.getTemplate(inherited);
    MotionClip result{name, source.frameCount, source.loop, {}};
    for (const auto& key : source.keyframes) {
        int time = static_cast<int>(key.t * 1000.0f + 0.5f);
        if (source.loop) time = time * static_cast<int>(source.keyframes.size() - 1) / static_cast<int>(source.keyframes.size());
        result.keyframes.push_back({time, adapted_pose(key.pose), curve(key.curve)});
    }
    if (source.loop) result.keyframes.push_back({1000, result.keyframes.front().pose, Interpolation::linear});
    return result;
}

PoseDefinition sample_pose(const MotionClip& clip, int frame_index) {
    if (clip.frame_count < 1 || clip.frame_count > 1000 || frame_index < 0 || frame_index >= clip.frame_count ||
        clip.keyframes.empty() || clip.keyframes.front().time != 0 || clip.keyframes.back().time != 1000) {
        throw std::invalid_argument("Invalid animation timeline");
    }
    int previous = -1;
    for (const auto& key : clip.keyframes) {
        if (key.time <= previous || key.time > 1000) throw std::invalid_argument("Keyframes must be strictly time ordered");
        previous = key.time;
        (void)transforms(key.pose);
    }
    const int time = clip.frame_count == 1 ? 0 : frame_index * 1000 / (clip.loop ? clip.frame_count : clip.frame_count - 1);
    auto after = std::upper_bound(clip.keyframes.begin(), clip.keyframes.end(), time,
                                 [](int value, const auto& key) { return value < key.time; });
    if (after == clip.keyframes.end()) return clip.keyframes.back().pose;
    const auto& before = *(after - 1);
    std::int64_t weight = (time - before.time) * unit / (after->time - before.time);
    if (before.interpolation == Interpolation::ease_in) weight = weight * weight / unit;
    else if (before.interpolation == Interpolation::ease_out) weight = unit - (unit - weight) * (unit - weight) / unit;
    else if (before.interpolation == Interpolation::ease_in_out) weight = weight < unit / 2
        ? 2 * weight * weight / unit : unit - 2 * (unit - weight) * (unit - weight) / unit;
    else if (before.interpolation == Interpolation::smoothstep) weight = weight * weight * (3 * unit - 2 * weight) / (unit * unit);
    const auto first = transforms(before.pose), last = transforms(after->pose);
    std::set<std::string> names;
    for (const auto& entry : first) names.insert(entry.first);
    for (const auto& entry : last) names.insert(entry.first);
    PoseDefinition result;
    for (const auto& name : names) {
        const auto start = first.count(name) ? first.at(name) : JointTransform{name};
        const auto end = last.count(name) ? last.at(name) : JointTransform{name};
        const auto blend = [&](int from, int to) { return from + rounded((to - from) * weight, unit); };
        result.transforms.push_back({name, blend(start.dx, end.dx), blend(start.dy, end.dy),
            blend(start.rotation / 15, end.rotation / 15) * 15,
            blend(start.scale_x, end.scale_x), blend(start.scale_y, end.scale_y)});
    }
    return result;
}

RenderedPose render_pose(const core::Frame& source, const rig::RigDefinition& rig, const PoseDefinition& pose) {
    const auto diagnostics = rig::validate_rig(rig);
    if (!diagnostics.empty()) throw std::invalid_argument(rig::diagnostics_json(diagnostics).dump());
    if (source.width != rig.width || source.height != rig.height || source.rgba.size() != rig.silhouette.size() * 4 ||
        anchor::extract_silhouette(source) != rig.silhouette) throw std::invalid_argument("Source does not match rig silhouette");
    const auto overrides = transforms(pose);
    std::map<std::string, const rig::Joint*> joints;
    for (const auto& joint : rig.joints) joints.emplace(joint.name, &joint);
    for (const auto& entry : overrides) if (!joints.count(entry.first)) throw std::invalid_argument("Unknown pose joint: " + entry.first);
    std::map<std::string, std::string> parents;
    for (const auto& bone : rig.bones) parents.emplace(bone.child, bone.parent);
    std::map<std::string, Matrix> matrices;
    std::function<Matrix(const std::string&)> resolve = [&](const std::string& name) {
        if (matrices.count(name)) return matrices.at(name);
        const auto local = local_matrix(*joints.at(name), overrides.count(name) ? overrides.at(name) : JointTransform{name});
        const auto matrix = compose(parents.count(name) ? resolve(parents.at(name)) : Matrix{}, local);
        matrices.emplace(name, matrix);
        return matrix;
    };
    Bounds bounds{0, 0, source.width - 1, source.height - 1};
    std::vector<Layer> layers;
    for (const auto& region : rig.regions) {
        if (std::none_of(region.pixels.begin(), region.pixels.end(), [](auto pixel) { return pixel != 0; })) continue;
        const auto matrix = resolve(region.joint);
        const auto& joint = *joints.at(region.joint);
        const auto target = mapped(matrix, joint.x, joint.y);
        Matrix linear = matrix;
        linear.tx = unit * joint.x - matrix.xx * joint.x - matrix.xy * joint.y;
        linear.ty = unit * joint.y - matrix.yx * joint.x - matrix.yy * joint.y;
        const Point shift{target.x - joint.x, target.y - joint.y};
        Bounds layer_bounds{joint.x, joint.y, joint.x, joint.y};
        for (int y = 0; y < rig.height; ++y) for (int x = 0; x < rig.width; ++x) {
            if (!region.pixels[pixel_index(x, y, rig.width)]) continue;
            for (int offset_y : {-1, 1}) for (int offset_x : {-1, 1}) layer_bounds.include(mapped(linear, x + offset_x, y + offset_y));
        }
        bounds.include({layer_bounds.left, layer_bounds.top}); bounds.include({layer_bounds.right, layer_bounds.bottom});
        bounds.include({layer_bounds.left + shift.x, layer_bounds.top + shift.y});
        bounds.include({layer_bounds.right + shift.x, layer_bounds.bottom + shift.y});
        layers.push_back({&region, linear, shift, layer_bounds});
    }
    const int width = bounds.right - bounds.left + 1, height = bounds.bottom - bounds.top + 1;
    if (width > 8192 || height > 8192 || static_cast<std::int64_t>(width) * height > 16777216) {
        throw std::invalid_argument("Rendered pose exceeds canvas budget");
    }
    RenderedPose result{{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4, 0)},
                        {rig.pivot.x - bounds.left, rig.pivot.y - bounds.top}, bounds.left, bounds.top};
    std::stable_sort(layers.begin(), layers.end(), [](const auto& left, const auto& right) {
        if (left.region->z_order != right.region->z_order) return left.region->z_order < right.region->z_order;
        return left.region->name < right.region->name;
    });
    std::vector<int> ownership(rig.silhouette.size(), -1);
    for (std::size_t layer = 0; layer < layers.size(); ++layer) for (std::size_t pixel = 0; pixel < ownership.size(); ++pixel) {
        if (layers[layer].region->pixels[pixel]) ownership[pixel] = static_cast<int>(layer);
    }
    const auto ancestor = [&](const std::string& parent, std::string child) {
        while (parents.count(child)) {
            child = parents.at(child);
            if (child == parent) return true;
        }
        return false;
    };
    const auto layer_index_of = [](const std::vector<Layer>& all, const Layer& layer) {
        return static_cast<int>(&layer - all.data());
    };
    const auto destination = [&](const Layer& layer, int x, int y) {
        auto point = mapped(layer.linear, x, y);
        point.x += layer.shift.x; point.y += layer.shift.y;
        return point;
    };
    const auto bridge = [&](Point start, Point end, std::size_t source_pixel) {
        const int distance_x = std::abs(end.x - start.x), distance_y = std::abs(end.y - start.y);
        const int step_x = start.x < end.x ? 1 : -1, step_y = start.y < end.y ? 1 : -1;
        int error = distance_x - distance_y;
        const auto paint = [&](Point point) {
            const auto offset = pixel_index(point.x - bounds.left, point.y - bounds.top, width) * 4;
            if (!result.frame.rgba[offset + 3]) std::copy_n(source.rgba.begin() + static_cast<std::ptrdiff_t>(source_pixel * 4),
                4, result.frame.rgba.begin() + static_cast<std::ptrdiff_t>(offset));
        };
        for (;;) {
            paint(start);
            if (start.x == end.x && start.y == end.y) break;
            const int twice = error * 2;
            if (twice > -distance_y) { error -= distance_y; start.x += step_x; paint(start); }
            if (twice < distance_x) { error += distance_x; start.y += step_y; }
        }
    };
    for (int y = 0; y < rig.height; ++y) for (int x = 0; x < rig.width; ++x) {
        const auto pixel = pixel_index(x, y, rig.width);
        if (ownership[pixel] < 0) continue;
        for (const Point step : {Point{1,0}, Point{0,1}}) {
            const int adjacent_x = x + step.x, adjacent_y = y + step.y;
            if (adjacent_x >= rig.width || adjacent_y >= rig.height) continue;
            const auto adjacent = pixel_index(adjacent_x, adjacent_y, rig.width);
            if (ownership[adjacent] < 0 || ownership[pixel] == ownership[adjacent]) continue;
            const auto& first = layers[ownership[pixel]];
            const auto& second = layers[ownership[adjacent]];
            if (!ancestor(first.region->joint, second.region->joint) && !ancestor(second.region->joint, first.region->joint)) continue;
            bridge(destination(first, x, y), destination(second, adjacent_x, adjacent_y),
                   ownership[pixel] > ownership[adjacent] ? pixel : adjacent);
        }
    }
    // ---------------------------------------------------------------------
    // Layer compositing.
    //
    // PHASE 4A FIX (double transform): this loop used to (1) warp the layer
    // with its own inverse-mapped affine pass into a scratch image, then
    // (2) rebuild a skeleton FROM THAT ALREADY-WARPED image and hand it to
    // spratgen::PixelRenderer::renderFrame(), whose drawBody/drawOutline push
    // every pixel through transform_pixel() a SECOND time. The result was a
    // forward-scattered re-warp on top of a correct backward warp, which
    // re-introduced the tearing and holes the backward pass exists to avoid,
    // and forced spratgen's hardcoded boxer palette onto arbitrary art.
    //
    // The layer transform is now applied exactly once: `layer.shift` is folded
    // into this pass's destination mapping (so the inverse pass walks the
    // final, shifted destination rectangle) and the layer is composited
    // straight into the result frame.
    std::vector<std::uint8_t> layer_mask(static_cast<std::size_t>(width) * height, 0);
    for (const auto& layer : layers) {
        std::fill(layer_mask.begin(), layer_mask.end(), static_cast<std::uint8_t>(0));
        const auto& matrix = layer.linear;
        const auto determinant = matrix.xx * matrix.yy - matrix.xy * matrix.yx;
        if (!determinant) continue;
        const auto copy_pixel = [&](int destination_x, int destination_y, std::size_t source_pixel, bool replace) {
            const int canvas_x = destination_x - bounds.left, canvas_y = destination_y - bounds.top;
            if (canvas_x < 0 || canvas_y < 0 || canvas_x >= width || canvas_y >= height) return;
            const auto target_pixel = pixel_index(canvas_x, canvas_y, width);
            if (!replace && layer_mask[target_pixel]) return;
            if (!source.rgba[source_pixel * 4 + 3]) return;
            std::copy_n(source.rgba.begin() + static_cast<std::ptrdiff_t>(source_pixel * 4), 4,
                        result.frame.rgba.begin() + static_cast<std::ptrdiff_t>(target_pixel * 4));
            layer_mask[target_pixel] = 1;
        };
        // Backward pass over the SHIFTED destination rectangle. Every
        // destination pixel is visited exactly once, so this cannot tear.
        for (int y = layer.bounds.top + layer.shift.y; y <= layer.bounds.bottom + layer.shift.y; ++y)
        for (int x = layer.bounds.left + layer.shift.x; x <= layer.bounds.right + layer.shift.x; ++x) {
            const auto translated_x = unit * (x - layer.shift.x) - matrix.tx;
            const auto translated_y = unit * (y - layer.shift.y) - matrix.ty;
            const int source_x = rounded(matrix.yy * translated_x - matrix.xy * translated_y, determinant);
            const int source_y = rounded(matrix.xx * translated_y - matrix.yx * translated_x, determinant);
            if (source_x < 0 || source_x >= rig.width || source_y < 0 || source_y >= rig.height) continue;
            auto pixel = pixel_index(source_x, source_y, rig.width);
            if (!layer.region->pixels[pixel]) {
                if (ownership[pixel] <= layer_index_of(layers, layer)) continue;
                bool repaired = false;
                for (const Point neighbor : {Point{-1,0}, Point{1,0}, Point{0,-1}, Point{0,1}}) {
                    const int neighbor_x = source_x + neighbor.x, neighbor_y = source_y + neighbor.y;
                    if (neighbor_x < 0 || neighbor_x >= rig.width || neighbor_y < 0 || neighbor_y >= rig.height) continue;
                    const auto adjacent = pixel_index(neighbor_x, neighbor_y, rig.width);
                    if (layer.region->pixels[adjacent]) { pixel = adjacent; repaired = true; break; }
                }
                if (!repaired) continue;
            }
            copy_pixel(x, y, pixel, true);
        }
        // Forward pass closes the sub-pixel gaps the backward pass can leave
        // when a layer is magnified; it never overwrites a backward-pass pixel.
        for (int y = 0; y < rig.height; ++y) for (int x = 0; x < rig.width; ++x) {
            const auto pixel = pixel_index(x, y, rig.width);
            if (!layer.region->pixels[pixel]) continue;
            const auto target = destination(layer, x, y);
            copy_pixel(target.x, target.y, pixel, false);
        }
    }
    return result;
}

void align_frames(std::vector<RenderedPose>& frames) {
    if (frames.empty()) return;
    Bounds bounds{frames.front().origin_x, frames.front().origin_y, frames.front().origin_x, frames.front().origin_y};
    const anchor::Pivot pivot{frames.front().pivot.x + frames.front().origin_x, frames.front().pivot.y + frames.front().origin_y};
    for (const auto& frame : frames) {
        if (frame.frame.width < 1 || frame.frame.height < 1 || frame.frame.rgba.size() !=
            static_cast<std::size_t>(frame.frame.width) * frame.frame.height * 4 ||
            frame.pivot.x + frame.origin_x != pivot.x || frame.pivot.y + frame.origin_y != pivot.y) {
            throw std::invalid_argument("Animation frames must share a valid source-space pivot");
        }
        bounds.include({frame.origin_x, frame.origin_y});
        bounds.include({frame.origin_x + frame.frame.width - 1, frame.origin_y + frame.frame.height - 1});
    }
    const int width = bounds.right - bounds.left + 1, height = bounds.bottom - bounds.top + 1;
    if (width > 8192 || height > 8192 || static_cast<std::int64_t>(width) * height > 16777216) throw std::invalid_argument("Animation exceeds canvas budget");
    for (auto& frame : frames) {
        core::Frame aligned{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4, 0)};
        for (int y = 0; y < frame.frame.height; ++y) {
            const auto destination = pixel_index(frame.origin_x - bounds.left, y + frame.origin_y - bounds.top, width) * 4;
            std::copy_n(frame.frame.rgba.begin() + static_cast<std::ptrdiff_t>(y) * frame.frame.width * 4,
                        static_cast<std::size_t>(frame.frame.width) * 4, aligned.rgba.begin() + static_cast<std::ptrdiff_t>(destination));
        }
        frame = {std::move(aligned), {pivot.x - bounds.left, pivot.y - bounds.top}, bounds.left, bounds.top};
    }
}
}  // namespace spratforge::motion