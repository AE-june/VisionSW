#pragma once

#include "HeightMap.h"
#include "Measurement.h"
#include "Profile.h"
#include "Region.h"
#include "Frame.h"
#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <utility>

namespace vision {

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
    std::string frameId;

    bool empty() const { return points.empty(); }
    std::size_t size() const { return points.size(); }
};

// ─────────────────────────────────────────────
//  Fitted plane: z = a*x + b*y + c  (mm)
// ─────────────────────────────────────────────
struct PlaneModel {
    double a = 0, b = 0, c = 0;
    bool   valid = false;
    std::string frameId;

    double signedDistance(double x, double y, double z) const {
        return (z - (a * x + b * y + c)) / std::sqrt(1.0 + a * a + b * b);
    }
    double tiltDeg() const {
        return std::atan(std::sqrt(a * a + b * b)) * 180.0 / 3.14159265358979323846;
    }
};

// ─────────────────────────────────────────────
//  Fitted 2D line in XY plane (edge/ridge detection result).
//  라인 위 한 점 (cx,cy) + 방향(angleDeg). px와 mm 둘 다 보유.
// ─────────────────────────────────────────────
struct LineModel {
    double cx = 0, cy = 0;        // 라인 위 한 점 (px)
    double cxMm = 0, cyMm = 0;    // 같은 점 (mm)
    double angleDeg = 0;          // XY 평면 방향 (deg, atan2(dy,dx))
    double straightnessMm = 0;    // 피팅 잔차 RMS (수직거리, mm)
    double p0x = 0, p0y = 0, p1x = 0, p1y = 0;             // 검출 라인 시작/끝 (px)
    double p0xMm = 0, p0yMm = 0, p1xMm = 0, p1yMm = 0;     // 같은 끝점 (mm)
    double lengthMm = 0;          // 두 끝점 거리 (mm)
    bool   valid = false;
    std::string frameId;

    // 방향 단위벡터
    double dirX() const { return std::cos(angleDeg * 3.14159265358979323846 / 180.0); }
    double dirY() const { return std::sin(angleDeg * 3.14159265358979323846 / 180.0); }
    // 점 (xMm,yMm)의 라인까지 수직 부호거리 (mm). 좌/우 판별용.
    double signedDistanceMm(double xMm, double yMm) const {
        // 법선 = (-dirY, dirX)
        return (xMm - cxMm) * (-dirY()) + (yMm - cyMm) * dirX();
    }
};

// ─────────────────────────────────────────────
//  Reference point
// ─────────────────────────────────────────────
struct RefPoint {
    double cx = 0, cy = 0;
    double cxMm = 0, cyMm = 0;
    double angleDeg = 0;
    int    roiIndex = -1;
    bool   valid = false;
};

// ─────────────────────────────────────────────
//  Universal container passed through pipeline
//
//  Phase 2 규약:
//  - 툴은 in(port) 계열 헬퍼로만 입력을 읽는다. 직접 슬롯 접근 금지.
//  - inputs[port]는 상류 출력 원본 포인터. 읽기 전용. 절대 수정 금지.
//  - 출력 벡터는 단일 출력도 size 1로 넣는다.
// ─────────────────────────────────────────────
struct VisionData {
    // 포트별 입력 — 인덱스 = 이 노드의 입력 포트 번호. 미연결은 nullptr.
    std::vector<std::shared_ptr<VisionData>> inputs;

    // 이 노드의 출력 (자기 데이터)
    std::vector<std::shared_ptr<HeightMap>>    heightmaps;
    std::vector<std::shared_ptr<PointCloud3D>> clouds;
    std::vector<std::shared_ptr<Region>>       regions;
    std::vector<std::shared_ptr<PlaneModel>>   planes;
    std::vector<std::shared_ptr<LineModel>>    lines;
    std::vector<std::shared_ptr<Profile>>      profiles;
    std::vector<RefPoint>                      points;
    std::vector<Measurement>                   measurements;
    std::vector<Decision>                      decisions;

    // 메타
    std::shared_ptr<FrameRegistry> frames;
    std::vector<Frame>             definedFrames;
    std::vector<Overlay>           overlays;
    std::string                    sourceId;
    int64_t                        timestampUs = 0;

    // 단계별 미리보기 (결과창 드롭다운용)
    std::shared_ptr<std::vector<std::pair<std::string, std::shared_ptr<HeightMap>>>> stages;

    // ── 포트 헬퍼 — 범위 밖/미연결은 nullptr ──────────────────────────
    std::shared_ptr<VisionData> in(std::size_t port) const {
        return port < inputs.size() ? inputs[port] : nullptr;
    }
    std::shared_ptr<HeightMap> inHeightMap(std::size_t port, std::size_t idx = 0) const {
        auto p = in(port);
        return (p && idx < p->heightmaps.size()) ? p->heightmaps[idx] : nullptr;
    }
    std::shared_ptr<PointCloud3D> inCloud(std::size_t port, std::size_t idx = 0) const {
        auto p = in(port);
        return (p && idx < p->clouds.size()) ? p->clouds[idx] : nullptr;
    }
    std::shared_ptr<Region> inRegion(std::size_t port, std::size_t idx = 0) const {
        auto p = in(port);
        return (p && idx < p->regions.size()) ? p->regions[idx] : nullptr;
    }
    std::shared_ptr<PlaneModel> inPlane(std::size_t port, std::size_t idx = 0) const {
        auto p = in(port);
        return (p && idx < p->planes.size()) ? p->planes[idx] : nullptr;
    }
    std::shared_ptr<Profile> inProfile(std::size_t port, std::size_t idx = 0) const {
        auto p = in(port);
        return (p && idx < p->profiles.size()) ? p->profiles[idx] : nullptr;
    }
    // 포트의 Region 배열 전체 반환 (Aurora Sequence<Region> 개념). 없으면 빈 벡터.
    const std::vector<std::shared_ptr<Region>>& inRegions(std::size_t port) const {
        static const std::vector<std::shared_ptr<Region>> kEmpty;
        auto p = in(port);
        return p ? p->regions : kEmpty;
    }

    // points는 vector<RefPoint> — 포트의 전체 목록 반환 (없으면 빈 벡터 ref)
    const std::vector<RefPoint>& inPoints(std::size_t port) const {
        static const std::vector<RefPoint> kEmpty;
        auto p = in(port);
        return p ? p->points : kEmpty;
    }

    // ── 호환 접근자 — 첫 원소(스칼라 관례) 또는 nullptr ───────────────
    const std::shared_ptr<Region>& region0() const {
        static const std::shared_ptr<Region> kNull;
        return regions.empty() ? kNull : regions.front();
    }
    void setRegion(std::shared_ptr<Region> r) {
        regions.clear(); if (r) regions.push_back(std::move(r));
    }
    const std::shared_ptr<PlaneModel>& plane0() const {
        static const std::shared_ptr<PlaneModel> kNull;
        return planes.empty() ? kNull : planes.front();
    }
    void setPlane(std::shared_ptr<PlaneModel> p) {
        planes.clear(); if (p) planes.push_back(std::move(p));
    }
    std::shared_ptr<LineModel> line0() const {
        return lines.empty() ? nullptr : lines.front();
    }
    void setLine(std::shared_ptr<LineModel> l) {
        lines.clear(); if (l) lines.push_back(std::move(l));
    }
    std::shared_ptr<LineModel> inLine(std::size_t port, std::size_t idx = 0) const {
        auto p = in(port);
        return (p && idx < p->lines.size()) ? p->lines[idx] : nullptr;
    }
    std::shared_ptr<HeightMap> heightmap0() const {
        return heightmaps.empty() ? nullptr : heightmaps.front();
    }
    void setHeightMap(std::shared_ptr<HeightMap> hm) {
        heightmaps.clear(); if (hm) heightmaps.push_back(std::move(hm));
    }
    std::shared_ptr<PointCloud3D> cloud0() const {
        return clouds.empty() ? nullptr : clouds.front();
    }
    void setCloud(std::shared_ptr<PointCloud3D> c) {
        clouds.clear(); if (c) clouds.push_back(std::move(c));
    }
    std::shared_ptr<Profile> profile0() const {
        return profiles.empty() ? nullptr : profiles.front();
    }
    void setProfile(std::shared_ptr<Profile> p) {
        profiles.clear(); if (p) profiles.push_back(std::move(p));
    }

    // ── bool 체크 ────────────────────────────────────────────────────
    bool hasHeightMap()  const { return !heightmaps.empty() && heightmaps.front() && !heightmaps.front()->empty(); }
    bool hasCloud()      const { return !clouds.empty() && clouds.front() && !clouds.front()->empty(); }
    bool hasRegion()     const { return !regions.empty() && regions.front() && !regions.front()->empty(); }
    bool hasPlane()      const { return !planes.empty() && planes.front() && planes.front()->valid; }
    bool hasMeasurements() const { return !measurements.empty(); }
    bool hasPoints()     const { return !points.empty(); }
};

using VisionDataPtr = std::shared_ptr<VisionData>;

} // namespace vision
