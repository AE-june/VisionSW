#pragma once

#include "IAlgorithmTool.h"

namespace vision {

// ─────────────────────────────────────────────
//  EdgeDetector  (2D only for now)
//  Algorithm: Canny / Sobel (selectable via Strategy)
// ─────────────────────────────────────────────
class EdgeDetector : public IAlgorithmTool {
public:
    enum class Algorithm { Canny, Sobel };

    struct Params {
        Algorithm algorithm    = Algorithm::Canny;
        double    threshold1   = 50.0;
        double    threshold2   = 150.0;
        int       apertureSize = 3;
    };

    explicit EdgeDetector(Params params = {});

    std::string name() const override { return "EdgeDetector"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    Params m_params;
};

} // namespace vision
