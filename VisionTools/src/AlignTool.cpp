#include "AlignTool.h"
#include "Logger.h"

namespace vision {

ToolResult AlignTool::execute(VisionDataPtr input) {
    m_result = {};

    if (!input || !input->hasZMap())
        return { ToolStatus::Fail, "Align: ZMap 입력이 없습니다" };
    if (!input->hasPoint())
        return { ToolStatus::Fail,
                 "Align: 기준점(Point) 입력이 없습니다 — LineCenter의 Point 출력을 연결하세요" };

    const auto& p = *input->point;

    m_result.offCol = p.cx;
    m_result.offRow = p.cy;
    m_result.offXMm = p.cxMm;
    m_result.offYMm = p.cyMm;
    m_result.valid  = true;

    VISION_LOG_INFO("Align: origin=({:.1f},{:.1f})px ({:.3f},{:.3f})mm",
                    p.cx, p.cy, p.cxMm, p.cyMm);

    // 출력 ZMap: 입력을 복사하고 원점을 기준점으로 설정 → 좌표계가 변환된다.
    // 이 ZMap을 입력받는 하류 툴은 (col-originCol, row-originRow) 좌표계를
    // 그대로 사용하므로 측정 ROI가 기준점을 따라가고(피스처링), 보고 좌표도
    // 기준점 기준 상대값이 된다.
    auto out  = std::make_shared<VisionData>(*input);
    auto zmap = std::make_shared<ZMap>(*input->zmap);
    zmap->originCol = static_cast<float>(p.cx);
    zmap->originRow = static_cast<float>(p.cy);
    out->zmap = zmap;

    return { ToolStatus::Ok, "", out };
}

} // namespace vision
