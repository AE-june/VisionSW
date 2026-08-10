#include "SurfaceSubtractTool.h"
#include <cmath>
#include <utility>

namespace vision {

SurfaceSubtractTool::SurfaceSubtractTool(SurfaceSubtractParams params)
    : m_params(std::move(params)) {}

ToolResult SurfaceSubtractTool::execute(VisionDataPtr input) {
    if (!input || !input->inHeightMap(0))
        return { ToolStatus::Fail, "SurfaceSubtract: HeightMap A(포트 0)가 없습니다." };
    if (!input->inHeightMap(1))
        return { ToolStatus::Fail, "SurfaceSubtract: HeightMap B(포트 1)가 없습니다." };

    const HeightMap& A = *input->inHeightMap(0);
    const HeightMap& B = *input->inHeightMap(1);

    if (A.width != B.width || A.height != B.height)
        return { ToolStatus::Fail, "SurfaceSubtract: 두 HeightMap 크기가 다릅니다." };
    if (std::fabs(A.xResMm - B.xResMm) > 1e-6f || std::fabs(A.yResMm - B.yResMm) > 1e-6f)
        return { ToolStatus::Fail, "SurfaceSubtract: 두 HeightMap 분해능이 다릅니다." };

    const bool propagate = (m_params.nanPolicy != "zero");
    const float NaN = std::numeric_limits<float>::quiet_NaN();
    const int w = A.width, h = A.height;
    const size_t N = (size_t)w * h;

    // 출력 Z 인코딩: zZeroCount=0, zResMm=1 → raw값이 그대로 mm 차이
    auto out_hm = std::make_shared<HeightMap>();
    out_hm->width      = w;
    out_hm->height     = h;
    out_hm->xResMm     = A.xResMm;
    out_hm->yResMm     = A.yResMm;
    out_hm->zResMm     = 1.0f;
    out_hm->zZeroCount = 0.0f;
    out_hm->originCol  = A.originCol;
    out_hm->originRow  = A.originRow;
    out_hm->frameId    = A.frameId;
    out_hm->data.resize(N);

    for (size_t i = 0; i < N; ++i) {
        float a = A.data[i], b = B.data[i];
        bool a_nan = std::isnan(a), b_nan = std::isnan(b);
        if (a_nan || b_nan) {
            out_hm->data[i] = propagate ? NaN : 0.f;
        } else {
            float a_mm = (a - A.zZeroCount) * A.zResMm;
            float b_mm = (b - B.zZeroCount) * B.zResMm;
            float diff = a_mm - b_mm;
            out_hm->data[i] = m_params.absolute ? std::fabs(diff) : diff;
        }
    }

    auto out = std::make_shared<VisionData>();
    out->setHeightMap(out_hm);
    out->frames   = input->frames;
    out->sourceId = input->sourceId;
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
