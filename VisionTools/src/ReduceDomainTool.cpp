#include "ReduceDomainTool.h"
#include <cmath>
#include <limits>

namespace vision {

ToolResult ReduceDomainTool::execute(VisionDataPtr input) {
    if (!input || !input->hasHeightMap())
        return { ToolStatus::Fail, "ReduceDomain: HeightMap 입력이 없습니다." };
    if (!input->hasRegion())
        return { ToolStatus::Fail, "ReduceDomain: Region 입력이 없습니다." };

    const HeightMap& map = *input->heightmap;
    const Region&    rg  = *input->region;
    if (rg.width != map.width || rg.height != map.height)
        return { ToolStatus::Fail, "ReduceDomain: Region 크기가 HeightMap과 다릅니다." };

    auto out = std::make_shared<HeightMap>(map);   // 전체 복사(보정/원점/채널 보존)
    const size_t stride = out->channelStride();
    const float NaN = std::numeric_limits<float>::quiet_NaN();
    const int w = out->width, h = out->height;

    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) {
            if (rg.mask[static_cast<size_t>(r) * w + c]) continue;   // 도메인 내부는 유지
            size_t idx = static_cast<size_t>(r) * w + c;
            for (int ch = 0; ch < out->channels; ++ch)
                out->data[static_cast<size_t>(ch) * stride + idx] = NaN;
        }

    auto od = std::make_shared<VisionData>();
    od->heightmap = out;
    od->sourceId  = input->sourceId;
    return { ToolStatus::Ok, "", od };
}

} // namespace vision
