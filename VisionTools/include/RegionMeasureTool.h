#pragma once
#include "IAlgorithmTool.h"

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  RegionMeasureTool — (Region [+ HeightMap]) → Heights
//  HALCON의 area_center / intensity: Region의 면적·무게중심(+영역 내 높이통계).
//  HeightMap이 함께 오면 mm 면적·mm 좌표·영역내 평균높이(zMm)를 계산.
//  출력 heights = [areaPx, (areaMm2), cxMm, cyMm, (meanZmm)] → CsvWriter로 흐름.
// ─────────────────────────────────────────────────────────────────────
struct RegionMeasureResult {
    double areaPx   = 0;
    double areaMm2  = 0;
    double cxMm     = 0;   // HeightMap 없으면 px
    double cyMm     = 0;
    double meanZmm  = 0;
    bool   hasHeight = false;   // HeightMap 동반 시 areaMm2/mm좌표/meanZmm 유효
    bool   valid     = false;
};

class RegionMeasureTool : public IAlgorithmTool {
public:
    std::string name() const override { return "RegionMeasure"; }
    ToolResult  execute(VisionDataPtr input) override;
    const RegionMeasureResult& lastResult() const { return m_result; }

private:
    RegionMeasureResult m_result;
};

} // namespace vision
