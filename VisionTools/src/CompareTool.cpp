#include "CompareTool.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace vision {

CompareTool::CompareTool(CompareParams params)
    : m_params(std::move(params)) {}

ToolResult CompareTool::execute(VisionDataPtr input) {
    auto p0 = input ? input->in(0) : nullptr;
    if (!p0 || p0->measurements.empty())
        return {ToolStatus::Fail, "Compare: 측정값(포트 0)이 없습니다."};

    const auto& src = p0->measurements;
    const bool  allTargets = m_params.target.empty();

    auto out = std::make_shared<VisionData>();
    out->sourceId = input->sourceId;
    out->frames   = input->frames;

    // 측정값 pass-through
    out->measurements = src;

    int compared = 0;
    for (const auto& m : src) {
        if (!allTargets && m.name != m_params.target) continue;
        if (!m.valid) continue;

        Decision d;
        d.name     = m.name;
        d.measured = m.value;
        d.nominal  = m_params.nominal;
        d.tolerance = m_params.tolerance;

        const std::string& mode = m_params.mode;
        if (mode == "tolerance") {
            d.pass = std::fabs(m.value - m_params.nominal) <= m_params.tolerance;
            std::ostringstream ss;
            ss << m.value << " ∈ [" << (m_params.nominal - m_params.tolerance)
               << ", " << (m_params.nominal + m_params.tolerance) << "]";
            d.reason = ss.str();
        } else if (mode == "range") {
            d.pass = (m.value >= m_params.min && m.value <= m_params.max);
            std::ostringstream ss;
            ss << m.value << " ∈ [" << m_params.min << ", " << m_params.max << "]";
            d.reason = ss.str();
        } else if (mode == "max") {
            d.pass = (m.value <= m_params.max);
            std::ostringstream ss;
            ss << m.value << " ≤ " << m_params.max;
            d.reason = ss.str();
        } else if (mode == "min") {
            d.pass = (m.value >= m_params.min);
            std::ostringstream ss;
            ss << m.value << " ≥ " << m_params.min;
            d.reason = ss.str();
        } else {
            return {ToolStatus::Fail, "Compare: 알 수 없는 mode=" + mode};
        }

        out->decisions.push_back(d);
        ++compared;
    }

    if (compared == 0)
        return {ToolStatus::Fail,
            "Compare: target='" + m_params.target + "' 측정값을 찾을 수 없습니다."};

    const int passed = static_cast<int>(
        std::count_if(out->decisions.begin(), out->decisions.end(),
                      [](const Decision& d) { return d.pass; }));
    VISION_LOG_INFO("Compare: mode={} compared={} passed={}", m_params.mode, compared, passed);
    return {ToolStatus::Ok, "", out};
}

} // namespace vision
