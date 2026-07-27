#pragma once

#include "IAlgorithmTool.h"
#include <string>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  AlignTool
//    입력: HeightMap 이미지 + 기준점(Point, 상류 LineCenter가 검출한 x,y).
//    기준점을 원점(0,0)으로 삼는 좌표 변환(현재 이동만, XY)을 만들어
//    출력 HeightMap에 실어 하류로 전달한다. 하류 측정 ROI가 이 변환만큼 함께
//    이동하여 측정 영역이 기준점을 따라간다 (피스처링).
// ─────────────────────────────────────────────────────────────────────
struct AlignResult {
    double offCol = 0, offRow = 0;   // 픽셀 오프셋 (기준점 위치)
    double offXMm = 0, offYMm = 0;   // mm 이동량
    bool   valid  = false;
    std::string message;
};

class AlignTool : public IAlgorithmTool {
public:
    AlignTool() = default;

    std::string name() const override { return "Align"; }
    ToolResult  execute(VisionDataPtr input) override;

    const AlignResult& lastResult() const { return m_result; }

private:
    AlignResult m_result;
};

} // namespace vision
