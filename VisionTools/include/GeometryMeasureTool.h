#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  GeometryMeasureTool — 기하 프리미티브 간 측정
//
//  kind (Line):   포트0 = LineFit 출력, 포트1 = 두 번째 Line (일부 kind)
//    "angle"          — 두 라인 사잇각 (0~90 deg). 포트0+1 필요.
//    "lineDistance"   — 두 평행라인 수직거리 (mm). 포트0+1 필요.
//    "intersectX"     — 두 라인 교점 X (mm). 포트0+1 필요.
//    "intersectY"     — 두 라인 교점 Y (mm). 포트0+1 필요.
//    "centerDistance" — 두 라인 중심점 간 거리 (mm). 포트0+1 필요.
//    "posX"           — 포트0 라인 중심 X (mm). 포트0만.
//    "posY"           — 포트0 라인 중심 Y (mm). 포트0만.
//  kind (Plane):  포트0 = PlaneFit 출력
//    "planeAngle"        — 두 평면 법선 사잇각 (deg). 포트0+1 필요.
//    "planeDistance"     — 두 평행 평면 수직 오프셋 (mm). 포트0+1 필요.
//    "planePointDistance"— 포트0 평면 ~ 포트1 RegionMeasure 중심점 수직거리 (mm).
//  kind (Circle): 포트0 = CircleFit 출력
//    "circlePosX"    — 원 중심 X (mm).
//    "circlePosY"    — 원 중심 Y (mm).
//    "circleRadius"  — 반경 (mm).
//    "circleDiameter"— 직경 (mm).
//    "circleResidual"— 피팅 잔차 (mm).
// ─────────────────────────────────────────────────────────────────────
struct GeometryMeasureParams {
    std::string kind       = "angle";    // 위 목록
    std::string outputName;              // "" = kind로 자동
    std::string outputUnit;              // "" = 자동 (deg 또는 mm)
};

class GeometryMeasureTool : public IAlgorithmTool {
public:
    explicit GeometryMeasureTool(GeometryMeasureParams params = {});
    std::string name() const override { return "GeometryMeasure"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    GeometryMeasureParams m_params;
};

} // namespace vision
