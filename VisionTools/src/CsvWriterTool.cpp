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
    if (!input)
        return { ToolStatus::Fail, "CsvWriter: 입력이 없습니다." };

    const auto u8p = std::filesystem::u8path(m_params.path);

    // ── Profile 모드: 포트 0 입력에 profiles[]가 있으면 샘플별 행으로 전체 저장 (overwrite) ──
    {
        const auto& profs = input->inProfiles(0);
        if (!profs.empty()) {
            std::size_t nRows = 0;
            for (const auto& prof : profs) if (prof) nRows = std::max(nRows, prof->z.size());
            if (nRows == 0)
                return { ToolStatus::Fail, "CsvWriter: profiles에 데이터가 없습니다." };

            std::ofstream ofs(u8p, std::ios::trunc);
            if (!ofs.is_open())
                return { ToolStatus::Fail, "CsvWriter: 파일을 열 수 없습니다: " + m_params.path };

            // Header: x_mm, [profile labels...]
            ofs << "scan_pos_mm";
            for (const auto& prof : profs)
                if (prof) ofs << "," << prof->label;
            ofs << "\n";

            ofs << std::fixed << std::setprecision(6);
            for (std::size_t r = 0; r < nRows; ++r) {
                double x = 0;
                for (const auto& prof : profs) {
                    if (prof && r < prof->x.size()) { x = prof->x[r]; break; }
                }
                ofs << x;
                for (const auto& prof : profs) {
                    ofs << ",";
                    if (prof && r < prof->z.size()) {
                        double v = prof->z[r];
                        if (std::isnan(v)) ofs << "";
                        else ofs << v;
                    }
                }
                ofs << "\n";
            }

            VISION_LOG_INFO("CsvWriter: profiles {} × {} 행 저장 → {}", profs.size(), nRows, m_params.path);
            auto out = std::make_shared<VisionData>();
            out->measurements.push_back({"rowCount", (double)nRows, "rows", true});
            out->sourceId = input->sourceId;
            return { ToolStatus::Ok, "", out };
        }
    }

    // ── Measurement 모드: 포트 0 입력의 measurements[] 기준 한 행 append (기존 동작) ──
    auto src0 = input->in(0);
    if (!src0 || src0->measurements.empty())
        return { ToolStatus::Fail, "CsvWriter: 입력에 measurements도 profiles도 없습니다." };

    std::error_code ec;
    const bool isEmpty = !std::filesystem::exists(u8p, ec)
                      || (std::filesystem::file_size(u8p, ec) == 0);

    std::ofstream ofs(u8p, std::ios::app);
    if (!ofs.is_open())
        return { ToolStatus::Fail, "CsvWriter: 파일을 열 수 없습니다: " + m_params.path };

    const auto& meas = src0->measurements;
    if (isEmpty) {
        if (!m_params.label.empty()) ofs << "label,";
        for (std::size_t i = 0; i < meas.size(); ++i) {
            if (i) ofs << ",";
            ofs << meas[i].name;
        }
        ofs << "\n";
    }

    if (!m_params.label.empty()) ofs << m_params.label << ",";
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
