#pragma once

#include "HeightMap.h"
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
    std::string frameId;           // a,b,c가 유효한 좌표계 프레임 id. "" = 미지정

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
//  Reference point — 라인/엣지 검출 결과 (좌표 정렬용)
//  노드 간에 검출된 기준점(x,y)을 전달하기 위한 모델
// ─────────────────────────────────────────────
struct RefPoint {
    double cx = 0, cy = 0;       // 픽셀 단위 (col, row)
    double cxMm = 0, cyMm = 0;   // mm 단위
    double angleDeg = 0;         // 라인 방향 (주축 각도)
    int    roiIndex = -1;        // 어느 검색 ROI에서 나왔는지
    bool   valid = false;
};

// ─────────────────────────────────────────────
//  선택된 출력 좌표 — LineCenter 결과창에서 고른
//  X(어느 라인의 x) / Y(어느 라인의 y). 좌표정렬 툴의 입력.
// ─────────────────────────────────────────────
struct OriginCoord {
    double xPx = 0, yPx = 0;     // 픽셀
    double xMm = 0, yMm = 0;     // mm
    bool   hasX = false, hasY = false;
};

// ─────────────────────────────────────────────
//  Universal container passed through pipeline
// ─────────────────────────────────────────────
struct VisionData {
    std::shared_ptr<PointCloud3D> cloud;
    std::shared_ptr<HeightMap>         heightmap;
    // T0-2 P3: 슬롯 다중화. 스칼라 = size 1. 배열 생산자(T2-1 ConnectedComponents)를 위한 토대.
    //   기존 단일 슬롯 코드는 region0()/setRegion() 호환 접근자로 그대로 동작.
    std::vector<std::shared_ptr<Region>>     regions;   // 픽셀 집합/마스크 (B2, 1급 iconic)
    std::vector<std::shared_ptr<PlaneModel>> planes;    // fitted plane (PlaneFit → HeightMeasure)
    std::shared_ptr<std::vector<double>> heights; // 측정된 높이값 배열 (HeightMeasure 출력)
    std::shared_ptr<std::vector<RefPoint>> points; // 검출된 기준점들 (LineCenter)
    std::shared_ptr<OriginCoord>  origin;        // 선택된 출력 좌표 X/Y (LineCenter → 좌표정렬)
    // 단계별 미리보기(선택) — (이름, HeightMap) 목록. 결과창 드롭다운으로 중간단계 조회용.
    std::shared_ptr<std::vector<std::pair<std::string, HeightMapPtr>>> stages;
    std::string                   sourceId;     // sensor / file origin
    int64_t                       timestampUs = 0;
    // 실행 1회분 프레임 레지스트리 (runPipeline이 생성, 모든 노드가 공유)
    std::shared_ptr<FrameRegistry> frames;
    // 이 노드가 정의한 프레임 목록 (캐시 적중 시 레지스트리 복원용)
    std::vector<Frame>             definedFrames;

    // ── 호환 접근자 — 첫 원소(스칼라 관례) 또는 널 ──────────────────────────
    const std::shared_ptr<Region>& region0() const {
        static const std::shared_ptr<Region> kNull;
        return regions.empty() ? kNull : regions.front();
    }
    void setRegion(std::shared_ptr<Region> r) { regions.clear(); if (r) regions.push_back(std::move(r)); }
    const std::shared_ptr<PlaneModel>& plane0() const {
        static const std::shared_ptr<PlaneModel> kNull;
        return planes.empty() ? kNull : planes.front();
    }
    void setPlane(std::shared_ptr<PlaneModel> p) { planes.clear(); if (p) planes.push_back(std::move(p)); }

    bool hasCloud()   const { return cloud && !cloud->empty(); }
    bool hasHeightMap()    const { return heightmap  && !heightmap->empty(); }
    bool hasRegion()  const { return !regions.empty() && regions.front() && !regions.front()->empty(); }
    bool hasPlane()   const { return !planes.empty() && planes.front() && planes.front()->valid; }
    bool hasHeights() const { return heights && !heights->empty(); }
    bool hasPoints()  const { return points && !points->empty(); }
    bool hasOrigin()  const { return origin && (origin->hasX || origin->hasY); }
};

using VisionDataPtr = std::shared_ptr<VisionData>;

} // namespace vision
