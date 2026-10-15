#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  RegionBooleanTool — Region × Region → Region
//  두 마스크에 불리언 연산 적용. HALCON intersection/union/difference 개념.
//  포트0 = A, 포트1 = B.
//  op: "and"|"or"|"xor"|"subtract"  (A-B)
// ─────────────────────────────────────────────────────────────────────
struct RegionBooleanParams {
    std::string op = "and";   // and | or | xor | subtract
};

class RegionBooleanTool : public IAlgorithmTool {
public:
    explicit RegionBooleanTool(RegionBooleanParams params = {});
    std::string name() const override { return "RegionBoolean"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    RegionBooleanParams m_params;
};

} // namespace vision
