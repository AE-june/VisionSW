#include "AlignTool.h"
#include "Logger.h"

namespace vision {

ToolResult AlignTool::execute(VisionDataPtr input) {
    m_result = {};

    if (!input || !input->hasZMap())
        return { ToolStatus::Fail, "Align: ZMap 입력이 없습니다" };
    if (!input->hasOrigin())
        return { ToolStatus::Fail,
                 "Align: 기준점(Point) 입력이 없습니다 — LineCenter의 Point 출력을 연결하세요" };

    const auto& p = *input->origin;

    m_result.offCol = p.xPx;
    m_result.offRow = p.yPx;
    m_result.offXMm = p.xMm;
    m_result.offYMm = p.yMm;
    m_result.valid  = true;

    VISION_LOG_INFO("Align: origin=({:.1f},{:.1f})px ({:.3f},{:.3f})mm",
                    p.xPx, p.yPx, p.xMm, p.yMm);

    auto out  = std::make_shared<VisionData>(*input);
    auto zmap = std::make_shared<ZMap>(*input->zmap);
    zmap->originCol = static_cast<float>(p.xPx);
    zmap->originRow = static_cast<float>(p.yPx);
    out->zmap = zmap;

    return { ToolStatus::Ok, "", out };
}

} // namespace vision
