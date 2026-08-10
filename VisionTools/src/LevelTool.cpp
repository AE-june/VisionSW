#include "LevelTool.h"
#include <cmath>
#include <limits>
#include <utility>

namespace vision {

LevelTool::LevelTool(LevelParams params) : m_params(std::move(params)) {}

ToolResult LevelTool::execute(VisionDataPtr input) {
    if (!input || !input->inHeightMap(0))
        return { ToolStatus::Fail, "Level: HeightMap이 없습니다." };
    if (!input->inPlane(1))
        return { ToolStatus::Fail, "Level: Plane이 없습니다." };

    const HeightMap&  map   = *input->inHeightMap(0);
    const PlaneModel& plane = *input->inPlane(1);

    if (!plane.valid)
        return { ToolStatus::Fail, "Level: Plane이 유효하지 않습니다." };

    // 프레임 불일치: 둘 다 비어있지 않고 다르면 Fail
    // TODO(T0-1 P3): FrameRegistry::transform(PF, HF)로 변환 후 계산
    const std::string& HF = map.frameId;
    const std::string& PF = plane.frameId;
    if (!HF.empty() && !PF.empty() && HF != PF)
        return { ToolStatus::Fail,
                 "Level: HeightMap 프레임(" + HF + ")과 Plane 프레임(" + PF + ")이 다릅니다." };

    const int    w        = map.width;
    const int    h        = map.height;
    const int    nch      = map.channels;
    const size_t ch_stride = map.channelStride();

    // 출력 HeightMap — D-1 B-plan: zZeroCount=0, zResMm 동일
    auto out_hm            = std::make_shared<HeightMap>();
    out_hm->width          = w;
    out_hm->height         = h;
    out_hm->channels       = nch;
    out_hm->xResMm         = map.xResMm;
    out_hm->yResMm         = map.yResMm;
    out_hm->zResMm         = map.zResMm;
    out_hm->zZeroCount     = 0.f;         // B-plan: 레벨링 결과 0 근처 → 정밀도 최대
    out_hm->originCol      = map.originCol;
    out_hm->originRow      = map.originRow;
    out_hm->frameId        = map.frameId;
    out_hm->channelRoles   = map.channelRoles;
    out_hm->data.resize(static_cast<size_t>(nch) * ch_stride);

    // 채널0 이외: bit-identical 복사
    for (int ch = 1; ch < nch; ++ch) {
        size_t off = static_cast<size_t>(ch) * ch_stride;
        std::copy(map.data.begin() + off,
                  map.data.begin() + off + ch_stride,
                  out_hm->data.begin() + off);
    }

    // 루프 밖 1회 계산 (§3.4 perf)
    const double a        = plane.a, b = plane.b, c_pl = plane.c;
    const bool   is_dist  = (m_params.mode == "distance");
    const double inv_norm = is_dist ? (1.0 / std::sqrt(1.0 + a*a + b*b)) : 1.0;
    const double ax_start = a * (0.0 - map.originCol) * map.xResMm;
    const double ax_step  = a * map.xResMm;
    const double inv_zRes = 1.0 / map.zResMm;
    const double off_mm   = m_params.offsetMm;
    const float  nan_val  = std::numeric_limits<float>::quiet_NaN();

    float* outPtr = out_hm->data.data();   // 채널0은 오프셋 0

    for (int r = 0; r < h; ++r) {
        const double by  = b * (r - (double)map.originRow) * map.yResMm;
        double       ax  = ax_start;
        for (int c = 0; c < w; ++c, ax += ax_step) {
            float raw0 = map.rawAt(c, r, 0);
            if (std::isnan(raw0)) {
                outPtr[static_cast<size_t>(r) * w + c] = m_params.keepInvalid ? nan_val : 0.f;
                continue;
            }
            double z_mm   = (raw0 - (double)map.zZeroCount) * map.zResMm;
            double dz     = z_mm - (ax + by + c_pl);
            double out_mm = (is_dist ? dz * inv_norm : dz) + off_mm;
            outPtr[static_cast<size_t>(r) * w + c] = static_cast<float>(out_mm * inv_zRes);
        }
    }

    auto out      = std::make_shared<VisionData>();
    out->setHeightMap(out_hm);
    out->frames    = input->frames;
    out->sourceId  = input->sourceId;
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
