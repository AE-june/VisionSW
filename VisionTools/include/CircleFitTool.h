#pragma once
#include "IAlgorithmTool.h"

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  CircleFitTool — Region [+ HeightMap] → Geometry(Circle) + Measurements
//  HALCON fit_circle_contour_xld 상당. 경계 픽셀에 최소제곱 원 피팅.
//  포트0 = Region, 포트1 = HeightMap (선택 — mm 변환용)
//  출력: geometries[0] (kind=Circle), measurements: radius/diameter/residual
// ─────────────────────────────────────────────────────────────────────
class CircleFitTool : public IAlgorithmTool {
public:
    std::string name() const override { return "CircleFit"; }
    ToolResult  execute(VisionDataPtr input) override;
};

} // namespace vision
