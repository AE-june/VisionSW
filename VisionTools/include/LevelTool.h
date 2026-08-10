#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

struct LevelParams {
    std::string mode       = "distance"; // "distance" | "flatten"
    bool        keepInvalid = true;       // 입력 NaN → 출력 NaN 보존
    double      offsetMm   = 0.0;        // 결과에 더할 상수 오프셋 (mm)
};

class LevelTool : public IAlgorithmTool {
public:
    explicit LevelTool(LevelParams params = {});
    std::string name() const override { return "Level"; }
    ToolResult  execute(VisionDataPtr input) override;
private:
    LevelParams m_params;
};

} // namespace vision
