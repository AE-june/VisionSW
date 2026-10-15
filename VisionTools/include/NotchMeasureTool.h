#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

struct NotchMeasureParams {
    // 센서 기하
    double transportResMm  = 0.003998;
    double lateralPitchMm  = 0.0063;

    // 랜드 피팅
    int    landFitIters    = 4;
    double landTolUm       = 30.0;

    // 노치 개구 검출
    double notchTrigUm     = -150.0;
    double notchMaxGapUm   = 50.0;
    int    notchMinCols    = 20;

    // 바닥 검출 방식
    std::string method     = "flat";   // "flat" | "corner"

    // 방식 1 (flat)
    double floorWinUm      = 150.0;
    int    floorMinPts     = 12;

    // 방식 2 (corner)
    int    smoothCols      = 3;
    double slopeDrop       = 0.35;
    double cornerSearchUm  = 500.0;

    // 바닥 포인트 집계 방식
    std::string floorAgg   = "median"; // "median" | "mean"

    // 공통 라벨링
    double floorTolUm      = 40.0;
    double dupMergeUm      = 10.0;

    // 프로파일 머지 (측정 전 N개 스캔라인을 합쳐 노이즈 감소)
    int    avgProfiles     = 1;        // 1 = 프로파일별 개별 측정, N > 1 = N개 머지 후 1회 측정
    std::string avgMethod  = "mean";   // "mean" | "median" — 머지 방식
};

class NotchMeasureTool : public IAlgorithmTool {
public:
    explicit NotchMeasureTool(NotchMeasureParams p = {}) : m_p(p) {}
    std::string name() const override { return "NotchMeasure"; }
    ToolResult  execute(VisionDataPtr input) override;
private:
    NotchMeasureParams m_p;
};

} // namespace vision
