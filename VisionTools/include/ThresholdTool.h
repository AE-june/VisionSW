#pragma once
#include "IAlgorithmTool.h"

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  ThresholdTool — Image(HeightMap) → Region
//  채널 값(기본 채널0=height, mm)에 임계값을 적용해 이진 마스크(Region)를 생산.
//  HALCON의 threshold 개념. 유효(비-NaN) 픽셀만 후보.
// ─────────────────────────────────────────────────────────────────────
struct ThresholdParams {
    int   channel     = 0;     // 임계 대상 채널 (기본 0=height)
    float thresholdMm = 0.f;   // 기준 높이 (mm, zMm 기준)
    bool  keepAbove   = true;  // true: z>=thr, false: z<=thr
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
