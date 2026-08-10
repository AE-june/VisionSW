#include "ReduceDomainTool.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace vision {

ToolResult ReduceDomainTool::execute(VisionDataPtr input) {
    if (!input || !input->inHeightMap(0))
        return { ToolStatus::Fail, "ReduceDomain: HeightMap 입력이 없습니다." };

    const HeightMap& map = *input->inHeightMap(0);
    const int w = map.width, h = map.height;

    // 포트1의 Region 전체를 union — 여러 영역 동시 처리 (제외 영역 여러 개 등)
    const auto& regs = input->inRegions(1);
    if (regs.empty())
        return { ToolStatus::Fail, "ReduceDomain: Region 입력이 없습니다." };
    std::vector<uint8_t> uni(static_cast<size_t>(w) * h, 0);
    for (const auto& rp : regs) {
        if (!rp) continue;
        if (rp->width != w || rp->height != h)
            return { ToolStatus::Fail, "ReduceDomain: Region 크기가 HeightMap과 다릅니다." };
        for (size_t i = 0; i < uni.size(); ++i) uni[i] |= rp->mask[i];
    }

    auto out = std::make_shared<HeightMap>(map);   // 전체 복사(보정/원점/채널 보존)
    const size_t stride = out->channelStride();
    const float NaN = std::numeric_limits<float>::quiet_NaN();

    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) {
            const bool inside = uni[static_cast<size_t>(r) * w + c] != 0;
            // invert=false: 바깥 제거(안쪽 유지) · invert=true: 안쪽 제거(제외 마스크)
            const bool remove = m_params.invert ? inside : !inside;
            if (!remove) continue;
            size_t idx = static_cast<size_t>(r) * w + c;
            for (int ch = 0; ch < out->channels; ++ch)
                out->data[static_cast<size_t>(ch) * stride + idx] = NaN;
        }

    auto od = std::make_shared<VisionData>();
    od->setHeightMap(out);
    od->sourceId  = input->sourceId;
    return { ToolStatus::Ok, "", od };
}

} // namespace vision
