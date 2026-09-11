#include "ConnectedComponentsTool.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <utility>

namespace vision {

ConnectedComponentsTool::ConnectedComponentsTool(ConnectedComponentsParams params)
    : m_params(std::move(params)) {}

ToolResult ConnectedComponentsTool::execute(VisionDataPtr input) {
    if (!input) return { ToolStatus::Fail, "ConnectedComponents: 입력이 없습니다." };

    const auto& regions = input->inRegions(0);
    if (regions.empty() || !regions[0] || regions[0]->empty())
        return { ToolStatus::Fail, "ConnectedComponents: Region 입력이 없습니다." };
    const Region* src = regions[0].get();
    if (!src || src->empty())
        return { ToolStatus::Fail, "ConnectedComponents: Region이 비어 있습니다." };

    const int w = src->width, h = src->height;

    // 마스크 → CV_8U
    cv::Mat mat(h, w, CV_8U, const_cast<uint8_t*>(src->mask.data()));

    // 연결 성분 레이블링
    cv::Mat labels, stats, centroids;
    const int conn = (m_params.connectivity == 4) ? 4 : 8;
    int nLabels = cv::connectedComponentsWithStats(mat, labels, stats, centroids, conn, CV_32S);

    if (nLabels <= 1) {
        // 내부 픽셀 없음 — 빈 배열 출력
        auto out = std::make_shared<VisionData>();
        out->sourceId = input->sourceId;
        out->frames   = input->frames;
        return { ToolStatus::Ok, "", out };
    }

    // 각 레이블(1..nLabels-1) → Region 생성
    // raster 정렬을 위해 (topRow*w + leftCol) 기준 정렬
    struct Blob {
        std::shared_ptr<Region> region;
        int sortKey;   // 첫 픽셀(raster 순서)
    };
    std::vector<Blob> blobs;
    blobs.reserve(static_cast<size_t>(nLabels - 1));

    const int* lptr = labels.ptr<int32_t>(0);
    const int* sptr = stats.ptr<int32_t>(0);  // [label * 5]: x,y,w,h,area

    for (int lbl = 1; lbl < nLabels; ++lbl) {
        const int area = sptr[static_cast<ptrdiff_t>(lbl) * 5 + cv::CC_STAT_AREA];
        if (area < m_params.minAreaPx) continue;
        if (m_params.maxAreaPx > 0 && area > m_params.maxAreaPx) continue;

        const int bx = sptr[static_cast<ptrdiff_t>(lbl) * 5 + cv::CC_STAT_LEFT];
        const int by = sptr[static_cast<ptrdiff_t>(lbl) * 5 + cv::CC_STAT_TOP];
        const int bw = sptr[static_cast<ptrdiff_t>(lbl) * 5 + cv::CC_STAT_WIDTH];
        const int bh = sptr[static_cast<ptrdiff_t>(lbl) * 5 + cv::CC_STAT_HEIGHT];

        auto rg = std::make_shared<Region>(Region::makeEmpty(w, h));
        rg->frameId = src->frameId;

        // BBox 안에서만 순회 (전체 W×H 안 돌아도 됨)
        int firstKey = by * w + bx;  // 첫 픽셀 raster 키 (BBox 상단좌)
        for (int r = by; r < by + bh; ++r) {
            const int* row = lptr + r * w;
            uint8_t*   dst = rg->mask.data() + static_cast<size_t>(r) * w;
            for (int c = bx; c < bx + bw; ++c) {
                if (row[c] == lbl) dst[c] = 1;
            }
        }

        // 실제 첫 픽셀을 찾아 raster 정렬 키로 사용
        bool found = false;
        for (int r = by; r < by + bh && !found; ++r)
            for (int c = bx; c < bx + bw && !found; ++c)
                if (lptr[r * w + c] == lbl) { firstKey = r * w + c; found = true; }

        blobs.push_back({ std::move(rg), firstKey });
    }

    // raster 순 정렬 (결정론 보장)
    std::sort(blobs.begin(), blobs.end(),
              [](const Blob& a, const Blob& b) { return a.sortKey < b.sortKey; });

    auto out = std::make_shared<VisionData>();
    out->sourceId = input->sourceId;
    out->frames   = input->frames;
    for (auto& b : blobs)
        out->regions.push_back(std::move(b.region));

    return { ToolStatus::Ok, "", out };
}

} // namespace vision
