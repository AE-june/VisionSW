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
//  Fitted plane: z = a*x + b*y + c  (mm)
//  노드 간에 피팅된 평면을 전달하기 위한 모델
// ─────────────────────────────────────────────
struct PlaneModel {
    double a = 0, b = 0, c = 0;   // z = a*x + b*y + c
    bool   valid = false;

    // 점 P=(x,y,z)에서 평면까지의 부호 있는 수직거리 (mm)
    // 평면 위(+z쪽)면 양수, 아래면 음수
    double signedDistance(double x, double y, double z) const {
        return (z - (a * x + b * y + c)) / std::sqrt(1.0 + a * a + b * b);
    }
    // 평면 법선과 z축 사이 기울기 각도 (deg)
    double tiltDeg() const {
        return std::atan(std::sqrt(a * a + b * b)) * 180.0 / 3.14159265358979323846;
    }
};

// ─────────────────────────────────────────────
//  Universal container passed through pipeline
// ─────────────────────────────────────────────
struct VisionData {
    std::shared_ptr<Image2D>      image;
    std::shared_ptr<PointCloud3D> cloud;
    std::shared_ptr<ZMap>         zmap;
    std::shared_ptr<PlaneModel>   plane;        // fitted plane (PlaneFit → HeightFromPlane)
    std::string                   sourceId;     // sensor / file origin
    int64_t                       timestampUs = 0;

    bool hasImage() const { return image && !image->empty(); }
    bool hasCloud() const { return cloud && !cloud->empty(); }
    bool hasZMap()  const { return zmap  && !zmap->empty(); }
    bool hasPlane() const { return plane && plane->valid; }
};

using VisionDataPtr = std::shared_ptr<VisionData>;

} // namespace vision
