#pragma once

#include "IAlgorithmTool.h"

namespace vision {

// ─────────────────────────────────────────────
//  NoiseFilter
//  2D: Gaussian / Median blur
//  3D: Statistical Outlier Removal (SOR)
// ─────────────────────────────────────────────
class NoiseFilter : public IAlgorithmTool {
public:
    struct Params {
        float radius       = 1.0f;   // 3D SOR neighborhood radius
        int   kernelSize   = 3;      // 2D blur kernel (must be odd)
        int   minNeighbors = 5;      // 3D SOR min neighbor count
    };

    explicit NoiseFilter(Params params = {});

    std::string name() const override { return "NoiseFilter"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    Params m_params;

    ToolResult filter2D(VisionDataPtr input);
    ToolResult filter3D(VisionDataPtr input);
};

} // namespace vision
