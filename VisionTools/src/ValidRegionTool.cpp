#include "ValidRegionTool.h"
#include <cmath>
#include <utility>

namespace vision {

ValidRegionTool::ValidRegionTool(ValidRegionParams params) : m_params(std::move(params)) {}

ToolResult ValidRegionTool::execute(VisionDataPtr input) {
    if (!input || !input->inHeightMap(0))
        return { ToolStatus::Fail, "ValidRegion: HeightMap이 없습니다." };

    const HeightMap& map = *input->inHeightMap(0);
    const int w = map.width, h = map.height;
    int ch = m_params.channel;
    if (ch < 0 || ch >= map.channels) ch = 0;

    auto rg = std::make_shared<Region>(Region::makeEmpty(w, h));
    rg->frameId = map.frameId;

    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) {
            bool valid = !std::isnan(map.rawAt(c, r, ch));
            if (m_params.invert) valid = !valid;
            if (valid) rg->mask[static_cast<size_t>(r) * w + c] = 1;
        }

    auto out = std::make_shared<VisionData>();
    out->setRegion(rg);
    out->frames = input->frames;
    out->sourceId = input->sourceId;
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
