#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  RegionSelectTool — Region[] → Region
//  배열에서 원소 하나를 꺼낸다. HALCON sort_region/select 개념.
//  mode:
//    "index"   — index 번째 (0-based). 범위 초과 시 실패.
//    "largest" — 면적 최대 블롭
//    "smallest"— 면적 최소 블롭
// ─────────────────────────────────────────────────────────────────────
struct RegionSelectParams {
    std::string mode  = "index";   // index | largest | smallest
    int         index = 0;
};

class RegionSelectTool : public IAlgorithmTool {
public:
    explicit RegionSelectTool(RegionSelectParams params = {});
    std::string name() const override { return "RegionSelect"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    RegionSelectParams m_params;
};

} // namespace vision
