#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  GradientMapTool — HeightMap → 기울기 크기 맵
//  HALCON derivate_image(Sobel) / Aurora SurfaceNormals 상당.
//  Sobel 필터로 X·Y 방향 편미분 후 크기 계산. 결함·엣지·스크래치 검출 전처리.
//  output_mode = "magnitude" : sqrt(gx² + gy²) in mm/mm (단위없는 기울기)
//              = "gx"        : X 방향 편미분
//              = "gy"        : Y 방향 편미분
// ─────────────────────────────────────────────────────────────────────
struct GradientMapParams {
    std::string output_mode = "magnitude"; // magnitude|gx|gy
    int         ksize       = 3;           // Sobel 커널 크기 (3 or 5)
};

class GradientMapTool : public IAlgorithmTool {
public:
    explicit GradientMapTool(GradientMapParams params = {});
    std::string name() const override { return "GradientMap"; }
    ToolResult  execute(VisionDataPtr input) override;
private:
    GradientMapParams m_params;
};

} // namespace vision
