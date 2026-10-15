#include "RegionBooleanTool.h"
#include <utility>

namespace vision {

RegionBooleanTool::RegionBooleanTool(RegionBooleanParams params)
    : m_params(std::move(params)) {}

ToolResult RegionBooleanTool::execute(VisionDataPtr input) {
    if (!input) return { ToolStatus::Fail, "RegionBoolean: 입력이 없습니다." };

    const auto rgA = input->inRegion(0);
    const auto rgB = input->inRegion(1);
    if (!rgA) return { ToolStatus::Fail, "RegionBoolean: 포트0 Region이 없습니다." };
    if (!rgB) return { ToolStatus::Fail, "RegionBoolean: 포트1 Region이 없습니다." };
    if (rgA->width != rgB->width || rgA->height != rgB->height)
        return { ToolStatus::Fail, "RegionBoolean: 두 Region 크기가 다릅니다." };

    const size_t N = static_cast<size_t>(rgA->width) * rgA->height;
    auto out = std::make_shared<Region>(Region::makeEmpty(rgA->width, rgA->height));
    out->frameId = rgA->frameId;

    const uint8_t* a = rgA->mask.data();
    const uint8_t* b = rgB->mask.data();
    uint8_t*       o = out->mask.data();

    if (m_params.op == "or") {
        for (size_t i = 0; i < N; ++i) o[i] = (a[i] | b[i]) ? 1 : 0;
    } else if (m_params.op == "xor") {
        for (size_t i = 0; i < N; ++i) o[i] = ((a[i] != 0) ^ (b[i] != 0)) ? 1 : 0;
    } else if (m_params.op == "subtract") {
        for (size_t i = 0; i < N; ++i) o[i] = (a[i] && !b[i]) ? 1 : 0;
    } else {  // and (default)
        for (size_t i = 0; i < N; ++i) o[i] = (a[i] & b[i]) ? 1 : 0;
    }

    auto vd = std::make_shared<VisionData>();
    vd->setRegion(std::move(out));
    vd->sourceId = input->sourceId;
    vd->frames   = input->frames;
    return { ToolStatus::Ok, "", vd };
}

} // namespace vision
