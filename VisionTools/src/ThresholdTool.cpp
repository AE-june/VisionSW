#include "ThresholdTool.h"
#include <cmath>
#include <utility>

namespace vision {

ThresholdTool::ThresholdTool(ThresholdParams params) : m_params(std::move(params)) {}

ToolResult ThresholdTool::execute(VisionDataPtr input) {
    if (!input || !input->hasHeightMap())
        return { ToolStatus::Fail, "Threshold: HeightMap이 없습니다." };

    const HeightMap& map = *input->heightmap;
    const int w = map.width, h = map.height;
    int ch = m_params.channel;
    if (ch < 0 || ch >= map.channels) ch = 0;

    auto rg = std::make_shared<Region>(Region::makeEmpty(w, h));
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) {
            float raw = map.rawAt(c, r, ch);
            if (std::isnan(raw)) continue;                 // 무효 픽셀 제외
            float zmm = (raw - map.zZeroCount) * map.zResMm;
            bool pass = m_params.keepAbove ? (zmm >= m_params.thresholdMm)
                                           : (zmm <= m_params.thresholdMm);
            if (pass) rg->mask[static_cast<size_t>(r) * w + c] = 1;
        }

    auto out = std::make_shared<VisionData>();
    out->setRegion(rg);
    out->sourceId = input->sourceId;
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
