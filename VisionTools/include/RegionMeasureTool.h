#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  RegionMeasureTool — (Region [+ HeightMap]) → Measurements
//  HALCON area_center + intensity 상당. Region 면적·무게중심·BBox·방향·
//  Z 집계(Aggregate.h)·체적·평탄도를 이름 있는 measurements로 출력한다.
// ─────────────────────────────────────────────────────────────────────
struct RegionMeasureParams {
    std::string aggregation = "Mean"; // Mean|Median|Max|Min|HighTail|Percentile|StdDev
    double highTailPct = 20.0;        // HighTail: 상위 몇 % 표본 평균
    double percentile  = 50.0;        // Percentile: 0~100
};

class RegionMeasureTool : public IAlgorithmTool {
public:
    explicit RegionMeasureTool(RegionMeasureParams params = {});
    std::string name() const override { return "RegionMeasure"; }
    ToolResult  execute(VisionDataPtr input) override;
private:
    RegionMeasureParams m_params;
};

} // namespace vision
