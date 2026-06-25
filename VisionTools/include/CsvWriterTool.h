#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  CsvWriterTool — 입력 데이터(높이값 배열)를 CSV 파일에 한 행씩 append.
//  연속 실행하면 행이 계속 누적된다 (반복성 테스트 기록용).
// ─────────────────────────────────────────────────────────────────────
struct CsvWriterParams {
    std::string path;        // 출력 CSV 경로
    std::string label;       // (선택) 행 첫 컬럼에 기록할 라벨 — 폴더검사 시 소스 파일명
};

struct CsvWriterResult {
    bool        saved = false;
    int         columns = 0;     // 이번에 기록한 값 개수
    std::string path;
    std::string message;
};

class CsvWriterTool : public IAlgorithmTool {
public:
    explicit CsvWriterTool(CsvWriterParams params = {});
    std::string name() const override { return "CsvWriter"; }
    ToolResult  execute(VisionDataPtr input) override;

    const CsvWriterResult& lastResult() const { return m_result; }

private:
    CsvWriterParams m_params;
    CsvWriterResult m_result;
};

} // namespace vision
