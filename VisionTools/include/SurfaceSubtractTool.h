#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

struct SurfaceSubtractParams {
    bool        absolute  = false;
    std::string nanPolicy = "propagate";   // "propagate" | "zero"
    std::string nodeId;
};

class SurfaceSubtractTool : public IAlgorithmTool {
public:
    explicit SurfaceSubtractTool(SurfaceSubtractParams params = {});
    std::string name() const override { return "SurfaceSubtract"; }
    ToolResult  execute(VisionDataPtr input) override;
private:
    SurfaceSubtractParams m_params;
};

} // namespace vision
