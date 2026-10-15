#pragma once
#include "IAlgorithmTool.h"

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  ThresholdTool — Image(HeightMap) → Region
//  채널 값(기본 채널0=height, mm)에 임계값을 적용해 이진 마스크(Region)를 생산.
//  HALCON의 threshold 개념. 유효(비-NaN) 픽셀만 후보.
// ─────────────────────────────────────────────────────────────────────
struct ThresholdParams {
    enum class Mode { Mm, Raw };  // 기준값 단위: mm(zMm) 또는 raw 픽셀값
    int   channel      = 0;       // 임계 대상 채널 (기본 0=height)
    Mode  mode         = Mode::Mm;
    float thresholdMm  = 0.f;     // mm 모드 기준 높이 (zMm 기준)
    float thresholdRaw = 0.f;     // raw 모드 기준 픽셀값 (rawAt 직접 비교)
    bool  keepAbove    = true;    // true: 값>=thr, false: 값<=thr
};

class ThresholdTool : public IAlgorithmTool {
public:
    explicit ThresholdTool(ThresholdParams params = {});
    std::string name() const override { return "Threshold"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    ThresholdParams m_params;
};

} // namespace vision
