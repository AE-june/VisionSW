#include "CsvWriterTool.h"
#include "Logger.h"
#include <fstream>
#include <iomanip>

namespace vision {

CsvWriterTool::CsvWriterTool(CsvWriterParams params) : m_params(std::move(params)) {}

ToolResult CsvWriterTool::execute(VisionDataPtr input) {
    m_result = {};

    if (m_params.path.empty())
        return { ToolStatus::Fail, "CsvWriter: 파일 경로가 설정되지 않았습니다." };
    if (!input || !input->hasHeights())
        return { ToolStatus::Fail, "CsvWriter: 입력에 높이값(Heights)이 없습니다. HeightMeasure를 먼저 연결하세요." };

    // append 모드 — 실행할 때마다 한 행 추가
    std::ofstream ofs(m_params.path, std::ios::app);
    if (!ofs.is_open())
        return { ToolStatus::Fail, "CsvWriter: 파일을 열 수 없습니다: " + m_params.path };

    const auto& h = *input->heights;
    // 라벨(소스 파일명 등)이 있으면 첫 컬럼에 기록 — 폴더검사 추적성
    if (!m_params.label.empty())
        ofs << m_params.label << ",";
    ofs << std::fixed << std::setprecision(6);
    for (std::size_t i = 0; i < h.size(); ++i) {
        if (i) ofs << ",";
        ofs << h[i];
    }
    ofs << "\n";

    m_result.saved   = true;
    m_result.columns = static_cast<int>(h.size());
    m_result.path    = m_params.path;

    VISION_LOG_INFO("CsvWriter: {}개 값을 한 행으로 추가 → {}", h.size(), m_params.path);

    // 타입화 출력: 싱크. 높이값만 통과(체인용), 이미지/zmap 미포함 → 결과창에 이미지 안 뜸.
    auto out = std::make_shared<VisionData>();
    out->heights  = input->heights;
    out->sourceId = input->sourceId;
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
