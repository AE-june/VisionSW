#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  RegionMorphologyTool — Region → Region
//  이진 마스크에 형태학적 연산 적용. HALCON erosion/dilation 개념.
//  op: "erode"|"dilate"|"open"|"close"
//  radius: 구조 원소 반지름(px). shape: "rect"|"ellipse"
// ─────────────────────────────────────────────────────────────────────
struct RegionMorphologyParams {
    std::string op     = "dilate";    // erode | dilate | open | close
    int         radius = 3;           // 구조 원소 반지름 (px)
    std::string shape  = "rect";      // rect | ellipse
};

class RegionMorphologyTool : public IAlgorithmTool {
public:
    explicit RegionMorphologyTool(RegionMorphologyParams params = {});
    std::string name() const override { return "RegionMorphology"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    RegionMorphologyParams m_params;
};

} // namespace vision
