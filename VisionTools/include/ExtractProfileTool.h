#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

struct ExtractProfileParams {
    std::string mode     = "axisX";  // "axisX" | "axisY" | "line"
    int         index    = 0;         // axisX/Y: 행/열 인덱스
    int         span     = 1;         // axisX/Y: 이웃 N줄 평균 (홀수 권장)
    int         repeat   = 1;         // axisX/Y: N개 평행 단면. 현재 1로 강제(D-3).
    int         channel  = 0;         // 어느 채널을 z로 쓸지
    // line 모드 (Phase 4)
    double      p0x = 0, p0y = 0;    // 시작점
    double      p1x = 0, p1y = 0;    // 끝점
    std::string unit    = "mm";       // "mm" | "px" — p0/p1 해석 단위
    int         count   = 0;          // 샘플 수. 0이면 자동(1px 간격)
    std::string interp  = "bilinear"; // "nearest" | "bilinear"
    std::string nodeId;               // 오버레이 식별용
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
