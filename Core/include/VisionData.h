#pragma once

#include "ZMap.h"
#include <cstdint>
#include <vector>
#include <string>
#include <memory>

namespace vision {

// ─────────────────────────────────────────────
//  2D Image
// ─────────────────────────────────────────────
struct Image2D {
    int width  = 0;
    int height = 0;
    int channels = 1;       // 1=Gray, 3=BGR, 4=BGRA
    std::vector<uint8_t> data;

    bool empty() const { return data.empty(); }
};

// ─────────────────────────────────────────────
//  3D Point
// ─────────────────────────────────────────────
struct Point3f {
    float x = 0.f, y = 0.f, z = 0.f;
};

// ─────────────────────────────────────────────
//  3D Point Cloud
// ─────────────────────────────────────────────
struct PointCloud3D {
    std::vector<Point3f> points;
    std::string frameId;    // coordinate frame label

    bool empty() const { return points.empty(); }
    std::size_t size() const { return points.size(); }
};

// ─────────────────────────────────────────────
//  Universal container passed through pipeline
// ─────────────────────────────────────────────
struct VisionData {
    std::shared_ptr<Image2D>      image;
    std::shared_ptr<PointCloud3D> cloud;
    std::shared_ptr<ZMap>         zmap;
    std::string                   sourceId;     // sensor / file origin
    int64_t                       timestampUs = 0;

    bool hasImage() const { return image && !image->empty(); }
    bool hasCloud() const { return cloud && !cloud->empty(); }
    bool hasZMap()  const { return zmap  && !zmap->empty(); }
};

using VisionDataPtr = std::shared_ptr<VisionData>;

} // namespace vision
