#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

struct ExtractProfileParams {
    std::string mode     = "axisX";  // "axisX" | "axisY" | "line"
    int         span     = 10;        // axisX/Y: 출력 1개당 평균할 라인수. 전체를 span줄씩 타일링.
    int         channel  = 0;         // 어느 채널을 z로 쓸지
    // line 모드 (Phase 4)
    double      p0x = 0, p0y = 0;    // 시작점
    double      p1x = 0, p1y = 0;    // 끝점
    std::string unit    = "mm";       // "mm" | "px" — p0/p1 해석 단위
    int         count   = 0;          // 샘플 수. 0이면 자동(1px 간격)
    std::string interp       = "bilinear"; // "nearest" | "bilinear"
    std::string aggregation  = "mean";    // "mean" | "min" | "max" | "median" | "stddev"
    std::string nodeId;                   // 오버레이 식별용
};

class ExtractProfileTool : public IAlgorithmTool {
public:
    explicit ExtractProfileTool(ExtractProfileParams params = {});
    std::string name() const override { return "ExtractProfile"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    ExtractProfileParams m_params;
};

} // namespace vision
