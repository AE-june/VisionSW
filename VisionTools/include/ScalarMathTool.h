#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  ScalarMathTool — Measurements × 2 → Measurement
//  두 포트의 지정 측정값에 사칙연산 적용.
//  포트0 = A (Measurements), 포트1 = B (Measurements).
//  nameA/nameB: 각 포트에서 꺼낼 측정값 이름. 비어있으면 첫 번째 값.
//  op: "add"|"subtract"|"multiply"|"divide"
//  outputName: 출력 Measurement 이름.
// ─────────────────────────────────────────────────────────────────────
struct ScalarMathParams {
    std::string op         = "subtract";  // add | subtract | multiply | divide
    std::string nameA;                    // "" = 첫 번째 값
    std::string nameB;                    // "" = 첫 번째 값
    std::string outputName = "result";
    std::string outputUnit;               // "" = nameA 단위 그대로
};

class ScalarMathTool : public IAlgorithmTool {
public:
    explicit ScalarMathTool(ScalarMathParams params = {});
    std::string name() const override { return "ScalarMath"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    ScalarMathParams m_params;
};

} // namespace vision
