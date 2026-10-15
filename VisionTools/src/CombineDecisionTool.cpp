#include "CombineDecisionTool.h"
#include "Logger.h"
#include <algorithm>
#include <sstream>

namespace vision {

CombineDecisionTool::CombineDecisionTool(CombineDecisionParams params)
    : m_params(std::move(params)) {}

ToolResult CombineDecisionTool::execute(VisionDataPtr input) {
    auto p0 = input ? input->in(0) : nullptr;
    if (!p0 || p0->decisions.empty())
        return {ToolStatus::Fail, "CombineDecision: 판정(포트 0)이 없습니다."};

    const auto& src = p0->decisions;
    const int total  = static_cast<int>(src.size());
    const int passed = static_cast<int>(
        std::count_if(src.begin(), src.end(),
                      [](const Decision& d) { return d.pass; }));

    Decision combined;
    combined.name     = m_params.name;
    combined.measured = static_cast<double>(passed);
    combined.nominal  = static_cast<double>(total);

    std::ostringstream ss;
    const std::string& mode = m_params.mode;
    if (mode == "all") {
        combined.pass = (passed == total);
        ss << "all " << passed << "/" << total;
    } else if (mode == "any") {
        combined.pass = (passed > 0);
        ss << "any " << passed << "/" << total;
    } else if (mode == "count") {
        combined.pass = (passed >= m_params.count);
        ss << "count>=" << m_params.count << " " << passed << "/" << total;
    } else {
        return {ToolStatus::Fail, "CombineDecision: 알 수 없는 mode=" + mode};
    }
    combined.reason = ss.str();

    auto out = std::make_shared<VisionData>();
    out->sourceId = input->sourceId;
    out->frames   = input->frames;
    out->decisions.push_back(combined);

    VISION_LOG_INFO("CombineDecision: mode={} {}/{} → {}",
        mode, passed, total, combined.pass ? "PASS" : "FAIL");
    return {ToolStatus::Ok, "", out};
}

} // namespace vision
