#include "CountRegionsTool.h"

namespace vision {

CountRegionsTool::CountRegionsTool(CountRegionsParams params) : m_params(std::move(params)) {}

ToolResult CountRegionsTool::execute(VisionDataPtr input) {
    if (!input) return { ToolStatus::Fail, "CountRegions: 입력이 없습니다." };
    const auto& regions = input->inRegions(0);
    auto out = std::make_shared<VisionData>();
    out->measurements.push_back({m_params.outputName, static_cast<double>(regions.size()), "", true});
    out->sourceId = input->sourceId;
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
