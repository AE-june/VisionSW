#pragma once
#include "IAlgorithmTool.h"

namespace vision {

struct ValidRegionParams {
    int  channel = 0;
    bool invert  = false;
};

class ValidRegionTool : public IAlgorithmTool {
public:
    explicit ValidRegionTool(ValidRegionParams params = {});
    std::string name() const override { return "ValidRegion"; }
    ToolResult  execute(VisionDataPtr input) override;
private:
    ValidRegionParams m_params;
};

} // namespace vision
