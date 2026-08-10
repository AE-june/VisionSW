#include "SurfaceResampleTool.h"
#include "Frame.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace vision {

SurfaceResampleTool::SurfaceResampleTool(SurfaceResampleParams params)
    : m_params(std::move(params)) {}

ToolResult SurfaceResampleTool::execute(VisionDataPtr input) {
    if (!input || !input->inHeightMap(0))
        return { ToolStatus::Fail, "SurfaceResample: HeightMap이 없습니다." };

    const HeightMap& map = *input->inHeightMap(0);
    const int W = map.width, H = map.height, nch = map.channels;

    // 팩터 계산
    int factorX = 1, factorY = 1;
    if (m_params.mode == "factor") {
        if (m_params.factor < 1)
            return { ToolStatus::Fail, "SurfaceResample: factor는 1 이상이어야 합니다." };
        factorX = factorY = m_params.factor;
    } else {
        // mode=resolution
        if (m_params.targetXResMm <= 0.f || m_params.targetYResMm <= 0.f)
            return { ToolStatus::Fail, "SurfaceResample: targetXResMm/targetYResMm는 양수여야 합니다." };
        factorX = std::max(1, static_cast<int>(std::round(m_params.targetXResMm / map.xResMm)));
        factorY = std::max(1, static_cast<int>(std::round(m_params.targetYResMm / map.yResMm)));
    }

    const int ow = (W + factorX - 1) / factorX;
    const int oh = (H + factorY - 1) / factorY;

    auto out_hm          = std::make_shared<HeightMap>();
    out_hm->width        = ow;
    out_hm->height       = oh;
    out_hm->channels     = nch;
    out_hm->xResMm       = map.xResMm * factorX;
    out_hm->yResMm       = map.yResMm * factorY;
    out_hm->zResMm       = map.zResMm;
    out_hm->zZeroCount   = map.zZeroCount;
    out_hm->originCol    = map.originCol / factorX;  // mm 좌표 보존
    out_hm->originRow    = map.originRow / factorY;
    out_hm->channelRoles = map.channelRoles;

    const std::string newFrameId = m_params.nodeId.empty()
                                   ? map.frameId
                                   : ("hm:" + m_params.nodeId);
    out_hm->frameId = newFrameId;

    const size_t out_ch_stride = static_cast<size_t>(ow) * oh;
    out_hm->data.resize(static_cast<size_t>(nch) * out_ch_stride);

    const size_t in_ch_stride = map.channelStride();
    const float  nan_val      = std::numeric_limits<float>::quiet_NaN();

    if (m_params.method == "meanValid") {
        // 유효 픽셀 평균 (NaN 제외)
        for (int ch = 0; ch < nch; ++ch) {
            const float* in_ch  = map.data.data()    + static_cast<size_t>(ch) * in_ch_stride;
            float*       out_ch = out_hm->data.data() + static_cast<size_t>(ch) * out_ch_stride;
            for (int or_ = 0; or_ < oh; ++or_) {
                for (int oc = 0; oc < ow; ++oc) {
                    double sum = 0.0;
                    int    cnt = 0;
                    for (int dy = 0; dy < factorY; ++dy) {
                        int ir = or_ * factorY + dy;
                        if (ir >= H) break;
                        for (int dx = 0; dx < factorX; ++dx) {
                            int ic = oc * factorX + dx;
                            if (ic >= W) break;
                            float v = in_ch[static_cast<size_t>(ir) * W + ic];
                            if (!std::isnan(v)) { sum += v; ++cnt; }
                        }
                    }
                    out_ch[static_cast<size_t>(or_) * ow + oc] = cnt > 0
                        ? static_cast<float>(sum / cnt) : nan_val;
                }
            }
        }
    } else {
        // decimate: 격자 샘플링 (원본 값 그대로)
        for (int ch = 0; ch < nch; ++ch) {
            const float* in_ch  = map.data.data()    + static_cast<size_t>(ch) * in_ch_stride;
            float*       out_ch = out_hm->data.data() + static_cast<size_t>(ch) * out_ch_stride;
            for (int or_ = 0; or_ < oh; ++or_) {
                int ir = or_ * factorY;
                for (int oc = 0; oc < ow; ++oc) {
                    int ic = oc * factorX;
                    out_ch[static_cast<size_t>(or_) * ow + oc] =
                        in_ch[static_cast<size_t>(ir) * W + ic];
                }
            }
        }
    }

    auto out      = std::make_shared<VisionData>();
    out->setHeightMap(out_hm);
    out->frames    = input->frames;
    out->sourceId  = input->sourceId;

    if (!m_params.nodeId.empty()) {
        const std::string parentId = map.frameId.empty() ? frames::kWorld : map.frameId;
        Frame f{ newFrameId, parentId, Transform2D::identity() };
        out->definedFrames.push_back(f);
        if (out->frames) out->frames->define(f);
    }

    return { ToolStatus::Ok, "", out };
}

} // namespace vision
