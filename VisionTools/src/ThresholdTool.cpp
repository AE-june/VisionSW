#include "ThresholdTool.h"
#include <cmath>
#include <utility>

namespace vision {

ThresholdTool::ThresholdTool(ThresholdParams params) : m_params(std::move(params)) {}

ToolResult ThresholdTool::execute(VisionDataPtr input) {
    if (!input || !input->inHeightMap(0))
        return { ToolStatus::Fail, "Threshold: HeightMap이 없습니다." };

    const HeightMap& map = *input->inHeightMap(0);
    const int w = map.width, h = map.height;
    int ch = m_params.channel;
    if (ch < 0 || ch >= map.channels) ch = 0;

    auto rg = std::make_shared<Region>(Region::makeEmpty(w, h));
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) {
            float raw = map.rawAt(c, r, ch);
            if (std::isnan(raw)) continue;                 // 무효 픽셀 제외
            float val, thr;
            if (m_params.mode == ThresholdParams::Mode::Raw) {
                val = raw;                                 // raw 픽셀값 직접 비교
                thr = m_params.thresholdRaw;
            } else {
                val = (raw - map.zZeroCount) * map.zResMm;  // mm(zMm)
                thr = m_params.thresholdMm;
            }
            bool pass = m_params.keepAbove ? (val >= thr) : (val <= thr);
            if (pass) rg->mask[static_cast<size_t>(r) * w + c] = 1;
        }

    auto out = std::make_shared<VisionData>();
    out->setRegion(rg);
    out->sourceId = input->sourceId;
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
