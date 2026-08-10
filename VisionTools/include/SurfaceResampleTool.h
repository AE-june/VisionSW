#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

struct SurfaceResampleParams {
    std::string mode          = "factor";  // "factor" | "resolution"
    int         factor        = 2;         // mode=factor. 2=1/2 해상도
    float       targetXResMm  = 0.f;      // mode=resolution
    float       targetYResMm  = 0.f;
    std::string method        = "decimate"; // "decimate" | "meanValid"
    std::string nodeId;                    // 신규 프레임 id 생성용
};

class SurfaceResampleTool : public IAlgorithmTool {
public:
    explicit SurfaceResampleTool(SurfaceResampleParams params = {});
    std::string name() const override { return "SurfaceResample"; }
    ToolResult  execute(VisionDataPtr input) override;
private:
    SurfaceResampleParams m_params;
};

} // namespace vision
