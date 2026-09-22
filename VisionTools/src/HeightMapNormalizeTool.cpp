#include "HeightMapNormalizeTool.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>
#include <algorithm>

namespace vision {

HeightMapNormalizeTool::HeightMapNormalizeTool(HeightMapNormalizeParams params)
    : m_params(std::move(params)) {}

ToolResult HeightMapNormalizeTool::execute(VisionDataPtr input) {
    if (!input || !input->inHeightMap(0))
        return { ToolStatus::Fail, "HeightMapNormalize: HeightMap이 없습니다." };

    const HeightMap& map = *input->inHeightMap(0);
    const int    w         = map.width;
    const int    h         = map.height;
    const int    nch       = map.channels;
    const size_t ch_stride = map.channelStride();
    const float  NaN       = std::numeric_limits<float>::quiet_NaN();

    // 포트1: 선택적 Region — 연결 시 영역 밖 픽셀은 NaN.
    const auto rgn = input->inRegion(1);

    // ── 출력 HeightMap: 정규화값을 raw로 그대로 저장. zResMm=1, zZeroCount=0 → zMm=raw. ──
    auto out_hm          = std::make_shared<HeightMap>();
    out_hm->width        = w;
    out_hm->height       = h;
    out_hm->channels     = nch;
    out_hm->xResMm       = map.xResMm;
    out_hm->yResMm       = map.yResMm;
    out_hm->zResMm       = 1.f;
    out_hm->zZeroCount   = 0.f;
    out_hm->originCol    = map.originCol;
    out_hm->originRow    = map.originRow;
    out_hm->frameId      = map.frameId;
    out_hm->channelRoles = map.channelRoles;
    out_hm->data.assign(static_cast<size_t>(nch) * ch_stride, NaN);

    // 채널0 이외: bit-identical 복사 (정규화는 채널0만 대상).
    for (int ch = 1; ch < nch; ++ch) {
        size_t off = static_cast<size_t>(ch) * ch_stride;
        std::copy(map.data.begin() + off,
                  map.data.begin() + off + ch_stride,
                  out_hm->data.begin() + off);
    }

    // ── 유효(비-NaN, Region 내부) 픽셀 인덱스 + 값 수집 ──────────────────
    const float* src    = map.data.data();          // 채널0 오프셋 0
    float*       dst     = out_hm->data.data();      // 채널0 오프셋 0
    std::vector<size_t> idxs;                        // 유효 픽셀 선형 인덱스
    std::vector<float>  vals;                        // 대응 raw 값
    idxs.reserve(static_cast<size_t>(w) * h);
    vals.reserve(static_cast<size_t>(w) * h);
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) {
            const size_t i = static_cast<size_t>(r) * w + c;
            if (rgn && !rgn->contains(c, r)) continue;   // Region 밖 → NaN 유지
            float v = src[i];
            if (std::isnan(v)) continue;                 // 무효 픽셀 제외
            idxs.push_back(i);
            vals.push_back(v);
        }

    if (idxs.empty()) {
        // 유효 픽셀 없음 — 전부 NaN인 출력 그대로 반환.
        auto out = std::make_shared<VisionData>();
        out->setHeightMap(out_hm);
        out->frames   = input->frames;
        out->sourceId = input->sourceId;
        return { ToolStatus::Ok, "HeightMapNormalize: 유효 픽셀 없음(전부 NaN)", out };
    }

    // ── min / max / mean / std 통계 (유효 픽셀) ─────────────────────────
    double vmin = vals[0], vmax = vals[0], sum = 0.0, sumSq = 0.0;
    for (float v : vals) {
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
        sum   += v;
        sumSq += static_cast<double>(v) * v;
    }
    const double n    = static_cast<double>(vals.size());
    const double mean = sum / n;
    double var        = sumSq / n - mean * mean;
    if (var < 0.0) var = 0.0;                             // 부동소수 오차 방어
    const double stddev = std::sqrt(var);
    const double range  = vmax - vmin;

    const std::string& mode = m_params.mode;

    if (mode == "minmax") {
        const double oMin = m_params.outMin, oMax = m_params.outMax;
        if (range <= 0.0) {
            // 상수 맵 → 모두 outMin.
            for (size_t k = 0; k < idxs.size(); ++k)
                dst[idxs[k]] = static_cast<float>(oMin);
        } else {
            const double scale = (oMax - oMin) / range;
            for (size_t k = 0; k < idxs.size(); ++k)
                dst[idxs[k]] = static_cast<float>(oMin + (vals[k] - vmin) * scale);
        }
    }
    else if (mode == "zscore") {
        if (stddev <= 0.0) {
            for (size_t k = 0; k < idxs.size(); ++k)
                dst[idxs[k]] = 0.f;                       // 상수 맵 → z=0.
        } else {
            const double invStd = 1.0 / stddev;
            for (size_t k = 0; k < idxs.size(); ++k)
                dst[idxs[k]] = static_cast<float>((vals[k] - mean) * invStd);
        }
    }
    else if (mode == "equalize" || mode == "clahe") {
        // 유효 픽셀 값을 8bit(0..255)로 양자화 → OpenCV 평활화 → [min,max] 역매핑.
        if (range <= 0.0) {
            for (size_t k = 0; k < idxs.size(); ++k)
                dst[idxs[k]] = static_cast<float>(vmin);
        } else {
            const int m = static_cast<int>(vals.size());
            cv::Mat q(1, m, CV_8U);                       // 1행 이미지로 처리
            uchar* qp = q.ptr<uchar>(0);
            const double toByte = 255.0 / range;
            for (int k = 0; k < m; ++k) {
                int b = static_cast<int>(std::lround((vals[k] - vmin) * toByte));
                qp[k] = static_cast<uchar>(std::clamp(b, 0, 255));
            }
            cv::Mat eq;
            if (mode == "equalize") {
                cv::equalizeHist(q, eq);
            } else {
                int g = std::max(1, m_params.tileGrid);
                cv::Ptr<cv::CLAHE> clahe =
                    cv::createCLAHE(m_params.clipLimit, cv::Size(g, g));
                clahe->apply(q, eq);
            }
            // 8bit 결과를 다시 [vmin,vmax]로 역매핑.
            const uchar* ep = eq.ptr<uchar>(0);
            const double fromByte = range / 255.0;
            for (int k = 0; k < m; ++k)
                dst[idxs[k]] = static_cast<float>(vmin + ep[k] * fromByte);
        }
    }
    else {
        return { ToolStatus::Fail,
                 "HeightMapNormalize: 알 수 없는 mode '" + mode + "'" };
    }

    auto out = std::make_shared<VisionData>();
    out->setHeightMap(out_hm);
    out->frames   = input->frames;
    out->sourceId = input->sourceId;
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
