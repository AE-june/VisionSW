#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  HeightMapMathTool — per-pixel 단항/이항 산술 연산
//  HALCON abs_image / add_image / mult_image 상당.
//  op = "abs"      : |포트0|
//  op = "add"      : 포트0 + 포트1
//  op = "subtract" : 포트0 - 포트1
//  op = "multiply" : 포트0 × factor  (포트1 없을 때)
//               또는 포트0 × 포트1  (포트1 연결 시)
//  NaN 픽셀: 어느 한쪽이라도 NaN이면 결과도 NaN.
// ─────────────────────────────────────────────────────────────────────
struct HeightMapMathParams {
    std::string op     = "abs";
    double      factor = 1.0;   // multiply 단항 시 스케일
};

class HeightMapMathTool : public IAlgorithmTool {
public:
    explicit HeightMapMathTool(HeightMapMathParams params = {});
    std::string name() const override { return "HeightMapMath"; }
    ToolResult  execute(VisionDataPtr input) override;
private:
    HeightMapMathParams m_params;
};

} // namespace vision
