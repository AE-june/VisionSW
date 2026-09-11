#include "RegionFilterTool.h"
#include <utility>

namespace vision {

RegionFilterTool::RegionFilterTool(RegionFilterParams params)
    : m_params(std::move(params)) {}

double RegionFilterTool::measure(const Region& rg) const {
    if (m_params.metric == "width")   return rg.boundingBox().w;
    if (m_params.metric == "height")  return rg.boundingBox().h;
    if (m_params.metric == "density") {
        Rect2D bb = rg.boundingBox();
        int bbArea = bb.w * bb.h;
        return bbArea > 0 ? static_cast<double>(rg.area()) / bbArea : 0.0;
    }
    return static_cast<double>(rg.area());   // default: area
}

ToolResult RegionFilterTool::execute(VisionDataPtr input) {
    if (!input) return { ToolStatus::Fail, "RegionFilter: 입력이 없습니다." };

    const auto& regions = input->inRegions(0);
    if (regions.empty())
        return { ToolStatus::Fail, "RegionFilter: Region[] 입력이 없습니다." };

    auto out = std::make_shared<VisionData>();
    out->sourceId = input->sourceId;
    out->frames   = input->frames;

    for (const auto& rg : regions) {
        if (!rg) continue;
        double val = measure(*rg);
        if (val < m_params.minVal) continue;
        if (m_params.maxVal > 0.0 && val > m_params.maxVal) continue;
        out->regions.push_back(rg);
    }

    return { ToolStatus::Ok, "", out };
}

} // namespace vision
