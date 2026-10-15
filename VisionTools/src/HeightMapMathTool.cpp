#include "HeightMapMathTool.h"
#include <cmath>
#include <limits>

namespace vision {

HeightMapMathTool::HeightMapMathTool(HeightMapMathParams params) : m_params(std::move(params)) {}

ToolResult HeightMapMathTool::execute(VisionDataPtr input) {
    if (!input || !input->inHeightMap(0))
        return { ToolStatus::Fail, "HeightMapMath: 포트0 HeightMap이 없습니다." };

    const HeightMap& A   = *input->inHeightMap(0);
    const auto       hmB = input->inHeightMap(1);
    const std::string& op = m_params.op;

    const int    w  = A.width, h = A.height;
    const size_t N  = static_cast<size_t>(w) * h;
    const float  NaN = std::numeric_limits<float>::quiet_NaN();

    if ((op == "add" || op == "subtract") && !hmB)
        return { ToolStatus::Fail, "HeightMapMath: op=" + op + " 에는 포트1 HeightMap이 필요합니다." };
    if (hmB && (hmB->width != w || hmB->height != h))
        return { ToolStatus::Fail, "HeightMapMath: 포트0/1 HeightMap 크기가 다릅니다." };

    auto out_hm = std::make_shared<HeightMap>(A);
    std::vector<float>& out = out_hm->data;
    const float* a = A.data.data();
    const float* b = hmB ? hmB->data.data() : nullptr;

    if (op == "abs") {
        for (size_t i = 0; i < N; ++i)
            out[i] = std::isnan(a[i]) ? NaN : std::fabs(a[i]);
    } else if (op == "add") {
        for (size_t i = 0; i < N; ++i)
            out[i] = (std::isnan(a[i]) || std::isnan(b[i])) ? NaN : a[i] + b[i];
    } else if (op == "subtract") {
        for (size_t i = 0; i < N; ++i)
            out[i] = (std::isnan(a[i]) || std::isnan(b[i])) ? NaN : a[i] - b[i];
    } else if (op == "multiply") {
        if (b) {
            for (size_t i = 0; i < N; ++i)
                out[i] = (std::isnan(a[i]) || std::isnan(b[i])) ? NaN : a[i] * b[i];
        } else {
            const float f = static_cast<float>(m_params.factor);
            for (size_t i = 0; i < N; ++i)
                out[i] = std::isnan(a[i]) ? NaN : a[i] * f;
        }
    } else {
        return { ToolStatus::Fail, "HeightMapMath: 알 수 없는 op: " + op };
    }

    auto vd = std::make_shared<VisionData>();
    vd->setHeightMap(out_hm);
    vd->sourceId = input->sourceId;
    vd->frames   = input->frames;
    return { ToolStatus::Ok, "", vd };
}

} // namespace vision
