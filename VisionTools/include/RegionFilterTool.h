#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  RegionFilterTool — Region[] → Region[]
//  메트릭(area/width/height/density) 범위로 블롭 배열을 필터링.
//  HALCON select_shape 개념.
// ─────────────────────────────────────────────────────────────────────
struct RegionFilterParams {
    std::string metric = "area";   // area | width | height | density
    double minVal = 0.0;
    double maxVal = 0.0;           // 0.0 = 제한 없음
};

class RegionFilterTool : public IAlgorithmTool {
public:
    explicit RegionFilterTool(RegionFilterParams params = {});
    std::string name() const override { return "RegionFilter"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    RegionFilterParams m_params;
    double measure(const Region& rg) const;
};

} // namespace vision
