#include "ScalarMathTool.h"
#include <cmath>
#include <utility>

namespace vision {

ScalarMathTool::ScalarMathTool(ScalarMathParams params)
    : m_params(std::move(params)) {}

// 이름으로 측정값 찾기, 없으면 첫 번째
static const Measurement* findMeasurement(const std::vector<Measurement>& ms,
                                          const std::string& name) {
    if (ms.empty()) return nullptr;
    if (name.empty()) return &ms[0];
    for (const auto& m : ms)
        if (m.name == name) return &m;
    return nullptr;
}

ToolResult ScalarMathTool::execute(VisionDataPtr input) {
    if (!input) return { ToolStatus::Fail, "ScalarMath: 입력이 없습니다." };

    const auto inA = input->in(0);
    const auto inB = input->in(1);
    if (!inA) return { ToolStatus::Fail, "ScalarMath: 포트0 Measurements가 없습니다." };
    if (!inB) return { ToolStatus::Fail, "ScalarMath: 포트1 Measurements가 없습니다." };

    const Measurement* mA = findMeasurement(inA->measurements, m_params.nameA);
    const Measurement* mB = findMeasurement(inB->measurements, m_params.nameB);

    if (!mA) return { ToolStatus::Fail, "ScalarMath: 포트0에서 '" + m_params.nameA + "' 측정값을 찾지 못했습니다." };
    if (!mB) return { ToolStatus::Fail, "ScalarMath: 포트1에서 '" + m_params.nameB + "' 측정값을 찾지 못했습니다." };
    if (!mA->valid || !mB->valid)
        return { ToolStatus::Fail, "ScalarMath: 입력 측정값이 유효하지 않습니다." };

    double result = 0;
    if (m_params.op == "add")      result = mA->value + mB->value;
    else if (m_params.op == "multiply") result = mA->value * mB->value;
    else if (m_params.op == "divide") {
        if (std::abs(mB->value) < 1e-12)
            return { ToolStatus::Fail, "ScalarMath: 0으로 나누기" };
        result = mA->value / mB->value;
    } else {  // subtract (default)
        result = mA->value - mB->value;
    }

    Measurement out;
    out.name  = m_params.outputName;
    out.value = result;
    out.unit  = m_params.outputUnit.empty() ? mA->unit : m_params.outputUnit;
    out.valid = true;

    auto vd = std::make_shared<VisionData>();
    vd->measurements.push_back(out);
    vd->sourceId = input->sourceId;
    vd->frames   = input->frames;
    return { ToolStatus::Ok, "", vd };
}

} // namespace vision
