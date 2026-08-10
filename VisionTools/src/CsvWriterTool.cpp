#include "CsvWriterTool.h"
#include "Logger.h"
#include <fstream>
#include <iomanip>
#include <filesystem>

namespace vision {

CsvWriterTool::CsvWriterTool(CsvWriterParams params) : m_params(std::move(params)) {}

ToolResult CsvWriterTool::execute(VisionDataPtr input) {
    if (m_params.path.empty())
        return { ToolStatus::Fail, "CsvWriter: 파일 경로가 설정되지 않았습니다." };
    if (!input || input->measurements.empty())
        return { ToolStatus::Fail, "CsvWriter: 입력에 measurements가 없습니다. RegionMeasure 또는 Compare를 먼저 연결하세요." };

    // UTF-8 경로 처리 (한글 등 비-ASCII 경로 안전)
    const auto u8p = std::filesystem::u8path(m_params.path);

    std::error_code ec;
    const bool isEmpty = !std::filesystem::exists(u8p, ec)
                      || (std::filesystem::file_size(u8p, ec) == 0);

    // append 모드 — 실행할 때마다 한 행 추가
    std::ofstream ofs(u8p, std::ios::app);
    if (!ofs.is_open())
        return { ToolStatus::Fail, "CsvWriter: 파일을 열 수 없습니다: " + m_params.path };

    const auto& meas = input->measurements;

    // 파일이 비어있으면 헤더 행 먼저 기록
    if (isEmpty) {
        if (!m_params.label.empty()) ofs << "label,";
        for (std::size_t i = 0; i < meas.size(); ++i) {
            if (i) ofs << ",";
            ofs << meas[i].name;
        }
        ofs << "\n";
    }

    // 데이터 행
    if (!m_params.label.empty())
        ofs << m_params.label << ",";
    ofs << std::fixed << std::setprecision(6);
    for (std::size_t i = 0; i < meas.size(); ++i) {
        if (i) ofs << ",";
        ofs << meas[i].value;
    }
    ofs << "\n";

    VISION_LOG_INFO("CsvWriter: {}개 측정값을 한 행으로 추가 → {}", meas.size(), m_params.path);

    auto out = std::make_shared<VisionData>();
    out->measurements.push_back({"rowCount", static_cast<double>(meas.size()), "cols", true});
    out->sourceId = input->sourceId;
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
