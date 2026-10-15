#include "RegionSelectTool.h"
#include <algorithm>
#include <utility>

namespace vision {

RegionSelectTool::RegionSelectTool(RegionSelectParams params)
    : m_params(std::move(params)) {}

ToolResult RegionSelectTool::execute(VisionDataPtr input) {
    if (!input) return { ToolStatus::Fail, "RegionSelect: 입력이 없습니다." };

    const auto& regions = input->inRegions(0);
    if (regions.empty())
        return { ToolStatus::Fail, "RegionSelect: Region[] 입력이 없습니다." };

    std::shared_ptr<Region> picked;

    if (m_params.mode == "largest") {
        size_t best = 0;
        for (const auto& rg : regions)
            if (rg && rg->area() > best) { best = rg->area(); picked = rg; }
    } else if (m_params.mode == "smallest") {
        size_t best = SIZE_MAX;
        for (const auto& rg : regions)
            if (rg && rg->area() < best) { best = rg->area(); picked = rg; }
    } else {
        // index mode
        int idx = m_params.index;
        if (idx < 0 || idx >= static_cast<int>(regions.size()))
            return { ToolStatus::Fail, "RegionSelect: index " + std::to_string(idx)
                     + " 범위 초과 (배열 크기: " + std::to_string(regions.size()) + ")" };
        picked = regions[static_cast<size_t>(idx)];
    }

    if (!picked)
        return { ToolStatus::Fail, "RegionSelect: 선택된 Region이 없습니다." };

    auto out = std::make_shared<VisionData>();
    out->sourceId = input->sourceId;
    out->frames   = input->frames;
    out->setRegion(picked);
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
