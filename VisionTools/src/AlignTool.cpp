#include "AlignTool.h"
#include "Logger.h"

namespace vision {

ToolResult AlignTool::execute(VisionDataPtr input) {
    m_result = {};

    if (!input || !input->hasHeightMap())
        return { ToolStatus::Fail, "Align: HeightMap 입력이 없습니다" };
    if (!input->hasOrigin())
        return { ToolStatus::Fail,
                 "Align: 기준점(Point) 입력이 없습니다 — LineCenter의 Point 출력을 연결하세요" };

    const auto& p = *input->origin;

    // 선택되지 않은 축(hasX/hasY=false)은 원점 이동 없음(0 유지) — 해당 축 좌표계 변환 안 함.
    m_result.offCol = p.hasX ? p.xPx : 0.0;
    m_result.offRow = p.hasY ? p.yPx : 0.0;
    m_result.offXMm = p.hasX ? p.xMm : 0.0;
    m_result.offYMm = p.hasY ? p.yMm : 0.0;
    m_result.valid  = true;

    VISION_LOG_INFO("Align: origin=({:.1f},{:.1f})px ({:.3f},{:.3f})mm  [X:{} Y:{}]",
                    m_result.offCol, m_result.offRow, m_result.offXMm, m_result.offYMm,
                    p.hasX ? "on" : "off", p.hasY ? "on" : "off");

    auto out  = std::make_shared<VisionData>(*input);
    auto heightmap = std::make_shared<HeightMap>(*input->heightmap);
    if (p.hasX) heightmap->originCol = static_cast<float>(p.xPx);   // 미선택 축은 기존값(0) 유지
    if (p.hasY) heightmap->originRow = static_cast<float>(p.yPx);
    out->heightmap = heightmap;

    return { ToolStatus::Ok, "", out };
}

} // namespace vision
