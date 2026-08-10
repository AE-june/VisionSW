#include "SurfaceCropTool.h"
#include "Frame.h"
#include <algorithm>
#include <limits>
#include <utility>

namespace vision {

SurfaceCropTool::SurfaceCropTool(SurfaceCropParams params) : m_params(std::move(params)) {}

ToolResult SurfaceCropTool::execute(VisionDataPtr input) {
    if (!input || !input->inHeightMap(0))
        return { ToolStatus::Fail, "SurfaceCrop: HeightMap이 없습니다." };

    const HeightMap& map = *input->inHeightMap(0);
    const int W = map.width, H = map.height, nch = map.channels;

    int dx = 0, dy = 0, ow = 0, oh = 0;
    const Region* rgn = nullptr;

    if (m_params.mode == "region") {
        if (!input->inRegion(1))
            return { ToolStatus::Fail, "SurfaceCrop: mode=region이지만 Region이 없습니다." };
        rgn = input->inRegion(1).get();
        if (rgn->width != W || rgn->height != H)
            return { ToolStatus::Fail, "SurfaceCrop: Region 크기가 HeightMap과 다릅니다." };
        const Rect2D bb = rgn->boundingBox();
        if (!bb.valid())
            return { ToolStatus::Fail, "SurfaceCrop: Region이 비어 있습니다." };
        dx = bb.x; dy = bb.y; ow = bb.w; oh = bb.h;
    } else {
        dx = m_params.rect_x;
        dy = m_params.rect_y;
        ow = m_params.rect_w > 0 ? m_params.rect_w : W - dx;
        oh = m_params.rect_h > 0 ? m_params.rect_h : H - dy;
        if (dx < 0 || dy < 0 || dx + ow > W || dy + oh > H || ow <= 0 || oh <= 0)
            return { ToolStatus::Fail, "SurfaceCrop: rect가 HeightMap 범위를 벗어납니다." };
    }

    const size_t out_ch_stride = static_cast<size_t>(ow) * oh;

    auto out_hm            = std::make_shared<HeightMap>();
    out_hm->width          = ow;
    out_hm->height         = oh;
    out_hm->channels       = nch;
    out_hm->xResMm         = map.xResMm;
    out_hm->yResMm         = map.yResMm;
    out_hm->zResMm         = map.zResMm;
    out_hm->zZeroCount     = map.zZeroCount;
    out_hm->originCol      = map.originCol - dx;   // 같은 물리점이 같은 mm
    out_hm->originRow      = map.originRow - dy;
    out_hm->channelRoles   = map.channelRoles;

    const std::string newFrameId = m_params.nodeId.empty()
                                   ? map.frameId
                                   : ("hm:" + m_params.nodeId);
    out_hm->frameId = newFrameId;
    out_hm->data.resize(static_cast<size_t>(nch) * out_ch_stride);

    const size_t in_ch_stride = map.channelStride();

    // 모든 채널 슬라이스 복사
    for (int ch = 0; ch < nch; ++ch) {
        const float* in_ch  = map.data.data()     + static_cast<size_t>(ch) * in_ch_stride;
        float*       out_ch = out_hm->data.data()  + static_cast<size_t>(ch) * out_ch_stride;
        for (int r = 0; r < oh; ++r) {
            const float* src = in_ch + static_cast<size_t>(dy + r) * W + dx;
            float*       dst = out_ch + static_cast<size_t>(r) * ow;
            std::copy(src, src + ow, dst);
        }
    }

    // mode=region + outsideNaN: Region 밖 픽셀의 채널0을 NaN으로
    if (m_params.mode == "region" && m_params.outsideNaN && rgn) {
        const float nan_val = std::numeric_limits<float>::quiet_NaN();
        float* ch0 = out_hm->data.data();
        for (int r = 0; r < oh; ++r)
            for (int c = 0; c < ow; ++c)
                if (!rgn->contains(dx + c, dy + r))
                    ch0[static_cast<size_t>(r) * ow + c] = nan_val;
    }

    auto out      = std::make_shared<VisionData>();
    out->setHeightMap(out_hm);
    out->frames    = input->frames;
    out->sourceId  = input->sourceId;

    // 신규 프레임 등록
    if (!m_params.nodeId.empty()) {
        const std::string parentId = map.frameId.empty() ? frames::kWorld : map.frameId;
        Frame f{ newFrameId, parentId, Transform2D::identity() };
        out->definedFrames.push_back(f);
        if (out->frames) out->frames->define(f);
    }

    return { ToolStatus::Ok, "", out };
}

} // namespace vision
