#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

struct CombineDecisionParams {
    std::string mode  = "all";  // all(AND) | any(OR) | count
    int         count = 1;      // count 모드: N개 이상 통과해야 pass
    std::string name  = "combined";  // 출력 Decision 이름
};

class CombineDecisionTool : public IAlgorithmTool {
public:
    explicit CombineDecisionTool(CombineDecisionParams params = {});
    std::string name() const override { return "CombineDecision"; }
    ToolResult  execute(VisionDataPtr input) override;
private:
    CombineDecisionParams m_params;
};

} // namespace vision
