#pragma once
#include "IAlgorithmTool.h"

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  ConnectedComponentsTool — Region → Region[]
//  이진 마스크에서 연결된 블롭 묶음을 분리. HALCON connection 개념.
//  출력: 블롭별 별도 Region, raster 순서(상단-왼쪽 픽셀 기준) 정렬.
// ─────────────────────────────────────────────────────────────────────
struct ConnectedComponentsParams {
    int connectivity = 8;   // 4 or 8
    int minAreaPx    = 1;   // 이 픽셀 수 미만인 블롭 제외
    int maxAreaPx    = 0;   // 이 픽셀 수 초과인 블롭 제외 (0 = 제한 없음)
};

class ConnectedComponentsTool : public IAlgorithmTool {
public:
    explicit ConnectedComponentsTool(ConnectedComponentsParams params = {});
    std::string name() const override { return "ConnectedComponents"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    ConnectedComponentsParams m_params;
};

} // namespace vision
