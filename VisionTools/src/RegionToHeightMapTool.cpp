#include "RegionToHeightMapTool.h"
#include "Region.h"
#include "HeightMap.h"
#include "VisionData.h"
#include <utility>
#include <vector>

namespace vision {

RegionToHeightMapTool::RegionToHeightMapTool(RegionToHeightMapParams params)
    : m_params(std::move(params)) {}

ToolResult RegionToHeightMapTool::execute(VisionDataPtr input) {
    if (!input)
        return { ToolStatus::Fail, "RegionToHeightMap: 입력이 없습니다." };

    // 포트0의 Region 전체를 union — 여러 영역을 하나의 이진 이미지로.
    const auto& regs = input->inRegions(0);
    if (regs.empty())
        return { ToolStatus::Fail, "RegionToHeightMap: Region 입력이 없습니다." };

    int w = 0, h = 0;
    for (const auto& rp : regs)
        if (rp) { w = rp->width; h = rp->height; break; }
    if (w == 0 || h == 0)
        return { ToolStatus::Fail, "RegionToHeightMap: 유효한 Region이 없습니다." };

    auto hm = std::make_shared<HeightMap>();
    hm->width = w; hm->height = h; hm->channels = 1;
    hm->data.assign(static_cast<size_t>(w) * h, m_params.outsideValue);

    for (const auto& rp : regs) {
        if (!rp) continue;
        if (rp->width != w || rp->height != h)
            return { ToolStatus::Fail, "RegionToHeightMap: Region 크기가 서로 다릅니다." };
        for (size_t i = 0; i < hm->data.size(); ++i)
            if (rp->mask[i]) hm->data[i] = m_params.insideValue;
    }

    // 옵션(포트1) HeightMap의 XY 지오메트리만 복사 — s좌표를 mm로 유지.
    // z 캘리브레이션(zResMm/zZeroCount)은 복사하지 않는다: 복사하면 0/1 값이
    // zResMm 배로 스케일돼(예: 0.001mm) 이진값 의미가 깨진다.
    if (auto ref = input->inHeightMap(1)) {
        if (ref->width == w && ref->height == h) {
            hm->xResMm    = ref->xResMm;
            hm->yResMm    = ref->yResMm;
            hm->originCol = ref->originCol;
            hm->originRow = ref->originRow;
            hm->frameId   = ref->frameId;
        }
    }
    // 참조 HeightMap이 없으면 Region의 frameId라도 승계.
    if (hm->frameId.empty() && regs[0])
        hm->frameId = regs[0]->frameId;

    auto out = std::make_shared<VisionData>();
    out->setHeightMap(hm);
    out->sourceId = input->sourceId;
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
