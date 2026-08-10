#include "AlignTool.h"
#include "Logger.h"

namespace vision {

ToolResult AlignTool::execute(VisionDataPtr input) {
    auto inHm = input ? input->inHeightMap(0) : nullptr;
    if (!inHm)
        return { ToolStatus::Fail, "Align: HeightMap 입력이 없습니다" };

    const auto& pts = input->inPoints(1);
    if (pts.empty() || !pts[0].valid)
        return { ToolStatus::Fail,
                 "Align: 기준점(Point) 입력이 없습니다 — LineCenter의 Point 출력을 연결하세요" };

    const auto& p = pts[0];
    const double offCol = m_params.useX ? p.cx  : 0.0;
    const double offRow = m_params.useY ? p.cy  : 0.0;
    const double offXMm = m_params.useX ? p.cxMm : 0.0;
    const double offYMm = m_params.useY ? p.cyMm : 0.0;

    VISION_LOG_INFO("Align: origin=({:.1f},{:.1f})px ({:.3f},{:.3f})mm  [X:{} Y:{}]",
                    offCol, offRow, offXMm, offYMm,
                    m_params.useX ? "on" : "off", m_params.useY ? "on" : "off");

    auto out = std::make_shared<VisionData>();
    out->frames   = input->frames;
    out->sourceId = input->sourceId;
    auto heightmap = std::make_shared<HeightMap>(*inHm);
    if (m_params.useX) heightmap->originCol = static_cast<float>(p.cx);
    if (m_params.useY) heightmap->originRow = static_cast<float>(p.cy);
    out->setHeightMap(heightmap);
    out->measurements = {
        {"offCol", offCol, "px", true},
        {"offRow", offRow, "px", true},
        {"offXMm", offXMm, "mm", true},
        {"offYMm", offYMm, "mm", true},
    };

    return { ToolStatus::Ok, "", out };
}

} // namespace vision
