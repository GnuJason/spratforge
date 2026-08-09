#include "skeleton.hpp"

#include <algorithm>
#include <cstddef>

namespace spratgen {

namespace {

struct Bounds {
    int minX = 0;
    int maxX = 0;
    int minY = 0;
    int maxY = 0;
    bool valid = false;
};

std::size_t pixel_index(int x, int y, int width) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
}

bool is_masked(const Silhouette& silhouette, int x, int y) {
    if (x < 0 || y < 0 || x >= silhouette.width || y >= silhouette.height) {
        return false;
    }
    return silhouette.mask[pixel_index(x, y, silhouette.width)] != 0;
}

Bounds compute_bounds(const Silhouette& silhouette) {
    Bounds bounds;
    for (int y = 0; y < silhouette.height; ++y) {
        for (int x = 0; x < silhouette.width; ++x) {
            if (!is_masked(silhouette, x, y)) {
                continue;
            }
            if (!bounds.valid) {
                bounds.minX = x;
                bounds.maxX = x;
                bounds.minY = y;
                bounds.maxY = y;
                bounds.valid = true;
                continue;
            }
            bounds.minX = std::min(bounds.minX, x);
            bounds.maxX = std::max(bounds.maxX, x);
            bounds.minY = std::min(bounds.minY, y);
            bounds.maxY = std::max(bounds.maxY, y);
        }
    }
    return bounds;
}

Joint centroid_in_region(
    const Silhouette& silhouette,
    int startX,
    int endX,
    int startY,
    int endY,
    bool preferHighestCluster) {
    long long sumX = 0;
    long long sumY = 0;
    int count = 0;
    int highestY = silhouette.height;

    for (int y = startY; y < endY; ++y) {
        bool rowHasPixels = false;
        for (int x = startX; x < endX; ++x) {
            if (!is_masked(silhouette, x, y)) {
                continue;
            }
            rowHasPixels = true;
            sumX += x;
            sumY += y;
            ++count;
        }
        if (preferHighestCluster && rowHasPixels) {
            highestY = std::min(highestY, y);
        }
    }

    if (count == 0) {
        return Joint{};
    }

    if (!preferHighestCluster) {
        return Joint(
            static_cast<int>(sumX / count),
            static_cast<int>(sumY / count));
    }

    long long clusterSumX = 0;
    long long clusterSumY = 0;
    int clusterCount = 0;
    for (int y = highestY; y < endY; ++y) {
        if (y - highestY > 2 && clusterCount > 0) {
            break;
        }
        bool rowHasPixels = false;
        for (int x = startX; x < endX; ++x) {
            if (!is_masked(silhouette, x, y)) {
                continue;
            }
            rowHasPixels = true;
            clusterSumX += x;
            clusterSumY += y;
            ++clusterCount;
        }
        if (!rowHasPixels && clusterCount > 0) {
            break;
        }
    }

    if (clusterCount == 0) {
        return Joint(
            static_cast<int>(sumX / count),
            static_cast<int>(sumY / count));
    }

    return Joint(
        static_cast<int>(clusterSumX / clusterCount),
        static_cast<int>(clusterSumY / clusterCount));
}

void sync_joint_vector(Skeleton& skeleton) {
    skeleton.joints = {
        skeleton.head,
        skeleton.torso,
        skeleton.left_arm,
        skeleton.right_arm,
        skeleton.left_leg,
        skeleton.right_leg,
    };
}

}  // namespace

Skeleton SkeletonBuilder::build(const Silhouette& silhouette) {
    Skeleton skeleton;
    const Bounds bounds = compute_bounds(silhouette);
    if (!bounds.valid) {
        sync_joint_vector(skeleton);
        return skeleton;
    }

    skeleton.head = findHead(silhouette);
    skeleton.head.name = "head";
    skeleton.torso = findTorso(silhouette);
    skeleton.torso.name = "torso";
    skeleton.left_arm = findArm(silhouette, true);
    skeleton.left_arm.name = "left_arm";
    skeleton.right_arm = findArm(silhouette, false);
    skeleton.right_arm.name = "right_arm";
    skeleton.left_leg = findLeg(silhouette, true);
    skeleton.left_leg.name = "left_leg";
    skeleton.right_leg = findLeg(silhouette, false);
    skeleton.right_leg.name = "right_leg";

    sync_joint_vector(skeleton);
    return skeleton;
}

Joint SkeletonBuilder::findHead(const Silhouette& silhouette) {
    const Bounds bounds = compute_bounds(silhouette);
    if (!bounds.valid) {
        return Joint{};
    }

    const int height = bounds.maxY - bounds.minY + 1;
    const int regionEndY = std::min(bounds.maxY + 1, bounds.minY + std::max(1, height * 20 / 100));
    return centroid_in_region(silhouette, bounds.minX, bounds.maxX + 1, bounds.minY, regionEndY, false);
}

Joint SkeletonBuilder::findTorso(const Silhouette& silhouette) {
    const Bounds bounds = compute_bounds(silhouette);
    if (!bounds.valid) {
        return Joint{};
    }

    const int height = bounds.maxY - bounds.minY + 1;
    const int startY = bounds.minY + height * 30 / 100;
    const int endY = std::min(bounds.maxY + 1, startY + std::max(1, height * 40 / 100));
    return centroid_in_region(silhouette, bounds.minX, bounds.maxX + 1, startY, endY, false);
}

Joint SkeletonBuilder::findArm(const Silhouette& silhouette, bool left) {
    const Bounds bounds = compute_bounds(silhouette);
    if (!bounds.valid) {
        return Joint{};
    }

    const int width = bounds.maxX - bounds.minX + 1;
    const int regionWidth = std::max(1, width * 30 / 100);
    const int startX = left ? bounds.minX : std::max(bounds.minX, bounds.maxX + 1 - regionWidth);
    const int endX = left ? std::min(bounds.maxX + 1, bounds.minX + regionWidth) : bounds.maxX + 1;
    return centroid_in_region(silhouette, startX, endX, bounds.minY, bounds.maxY + 1, true);
}

Joint SkeletonBuilder::findLeg(const Silhouette& silhouette, bool left) {
    const Bounds bounds = compute_bounds(silhouette);
    if (!bounds.valid) {
        return Joint{};
    }

    const int height = bounds.maxY - bounds.minY + 1;
    const int startY = std::max(bounds.minY, bounds.maxY + 1 - std::max(1, height * 30 / 100));
    const int midX = bounds.minX + (bounds.maxX - bounds.minX + 1) / 2;
    const int startX = left ? bounds.minX : midX;
    const int endX = left ? midX : bounds.maxX + 1;
    return centroid_in_region(silhouette, startX, endX, startY, bounds.maxY + 1, false);
}

}  // namespace spratgen