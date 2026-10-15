#pragma once
#include "IAlgorithmTool.h"

namespace vision {

// RowStretch(행 늘리기): Region 포트(포트1, 선택)가 지정한 행을 scale배 선형보간 업샘플.
//   Region 있으면 해당 행(Region 픽셀 1개 이상 있는 행)만 늘리고 나머지는 ×1. 없으면 전체 ×scale.
class RowStretchTool : public IAlgorithmTool {
public:
    explicit RowStretchTool(int scale = 1) : m_scale(scale < 1 ? 1 : scale) {}
    std::string name() const override { return "RowStretch"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    int m_scale;
};

} // namespace vision
