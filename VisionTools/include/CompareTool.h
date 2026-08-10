#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

struct CompareParams {
    std::string target;               // 대상 측정값 이름. 빈 문자열이면 전체
    std::string mode = "tolerance";   // tolerance | range | max | min
    double      nominal   = 0.0;      // 공칭값 (tolerance 모드)
    double      tolerance = 0.05;     // 공차 (tolerance 모드)
    double      min       = 0.0;      // 하한 (range / min 모드)
    double      max       = 0.0;      // 상한 (range / max 모드)
};

class CompareTool : public IAlgorithmTool {
public:
    explicit CompareTool(CompareParams params = {});
    std::string name() const override { return "Compare"; }
    ToolResult  execute(VisionDataPtr input) override;
private:
    CompareParams m_params;
};

} // namespace vision
