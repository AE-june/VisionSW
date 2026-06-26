#pragma once
#include "IAlgorithmTool.h"
#include "ZMap.h"
#include <vector>
#include <array>
#include <string>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  HeightFromPlaneParams
//  입력으로 받은 평면(PlaneModel)을 기준으로, 여러 measure ROI에서
//  대표 Z를 추출하고 평면까지의 수직거리를 측정한다.
// ─────────────────────────────────────────────────────────────────────
struct HeightFromPlaneParams {
    // ROI in percentage of ZMap dimensions (0.0 ~ 1.0)
    struct ROI {
        float xPct = 0.f, yPct = 0.f, wPct = 1.f, hPct = 1.f;
        bool  isCircle = false;   // true면 ROI 사각 영역에 내접하는 타원
        bool valid() const { return wPct > 0.f && hPct > 0.f; }
    };

    std::vector<ROI> measureRois;   // 높이를 측정할 영역들 (>=1)

    enum class Aggregation {
        Mean,       // ROI 내 유효 Z 평균
        Max,        // ROI 내 최대 Z
        HighTail    // 상위 highTailPct% Z의 평균
    } aggregation = Aggregation::Mean;

    float highTailPct = 20.f;       // HighTail 비율 (%)

    // 합부 판정 (선택)
    bool  useTolerance = false;
    float nominalMm    = 0.f;       // 기준 수직거리 (mm)
    float toleranceMm  = 0.05f;     // 허용 오차 (± mm)
};

// ─────────────────────────────────────────────────────────────────────
//  측정 결과 (ROI 1개당 1개)
// ─────────────────────────────────────────────────────────────────────
struct HeightMeasure {
    double cx = 0, cy = 0;   // 대표 위치 (mm)
    double z  = 0;           // 집계된 대표 Z (mm)
    double distance = 0;     // 평면까지의 부호 있는 수직거리 (mm)
    int    pointCount = 0;
    bool   pass = true;      // tolerance 통과 여부 (useTolerance=false면 항상 true)
};

struct HeightFromPlaneResult {
    std::vector<HeightMeasure> measures;
    bool        valid   = false;
    bool        allPass = true;
    std::string message;
};

// ─────────────────────────────────────────────────────────────────────
//  HeightFromPlaneTool
// ─────────────────────────────────────────────────────────────────────
class HeightFromPlaneTool : public IAlgorithmTool {
public:
    explicit HeightFromPlaneTool(HeightFromPlaneParams params = {});
    std::string name() const override { return "HeightMeasure"; }
    ToolResult  execute(VisionDataPtr input) override;

    const HeightFromPlaneResult& lastResult() const { return m_result; }

private:
    HeightFromPlaneParams m_params;
    HeightFromPlaneResult m_result;

    using Pt3 = std::array<double, 3>;   // {x_mm, y_mm, z_mm}
    std::vector<Pt3> extractPoints(const ZMap& map,
                                   const HeightFromPlaneParams::ROI& roi,
                                   int offCol = 0, int offRow = 0) const;

    // ROI 내 점들 → (대표 x, 대표 y, 대표 z)
    Pt3 aggregate(const std::vector<Pt3>& pts) const;
};

} // namespace vision
