#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  CountRegionsTool — Region[] → Measurement("count")
//  HALCON count_obj 상당. ConnectedComponents 이후 블롭 수 판정에 사용.
// ─────────────────────────────────────────────────────────────────────
struct CountRegionsParams {
    std::string outputName = "count";
};

class CountRegionsTool : public IAlgorithmTool {
public:
    explicit CountRegionsTool(CountRegionsParams params = {});
    std::string name() const override { return "CountRegions"; }
    ToolResult  execute(VisionDataPtr input) override;
private:
    CountRegionsParams m_params;
};

} // namespace vision
