#include "RefHeightTool.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vision {

RefHeightTool::RefHeightTool(RefHeightParams params) : m_params(std::move(params)) {}

// ═════════════════════════════════════════════════════════════════════
//  execute
// ═════════════════════════════════════════════════════════════════════
ToolResult RefHeightTool::execute(VisionDataPtr input) {
    m_result = {};

    if (!input || !input->hasHeightMap())
        return { ToolStatus::Fail, "RefHeight: HeightMap이 없습니다." };
    if (m_params.rois.empty())
        return { ToolStatus::Fail, "RefHeight: ROI가 없습니다." };

    const HeightMap& map = *input->heightmap;

    // HeightMap 원점이 설정돼 있으면(Align 통과) ROI를 원점만큼 이동. PlaneFit/HeightMeasure와 동일 규칙.
    const int offCol = static_cast<int>(std::lround(map.originCol));
    const int offRow = static_cast<int>(std::lround(map.originRow));

    // 모든 ROI의 Z값을 하나의 표본으로 풀링
    std::vector<float> z;
    for (const auto& roi : m_params.rois) {
        auto p = extractZ(map, roi, offCol, offRow);
        z.insert(z.end(), p.begin(), p.end());
    }
    if (z.empty()) {
        std::string roiDump;
        char buf[128];
        for (const auto& roi : m_params.rois) {
            std::snprintf(buf, sizeof(buf), "[x%.3f y%.3f w%.3f h%.3f] ",
                roi.xPct, roi.yPct, roi.wPct, roi.hPct);
            roiDump += buf;
        }
        VISION_LOG_INFO("RefHeight [DIAG] no points: map={}x{} offCol={} offRow={} rois={}",
            map.width, map.height, offCol, offRow, roiDump);
        return { ToolStatus::Fail, "RefHeight: ROI 내 유효 포인트가 없습니다." };
    }

    const int totalCount = static_cast<int>(z.size());
    int rejected = 0;
    double avg = 0;

    if (m_params.mode == RefHeightParams::OutlierMode::Sor) {
        // 전체 표본의 mean/stddev 기준 sigma 밖 제거 (공간 윈도우 아닌 global 1회 통계 — NoiseFilter의
        // 픽셀별 지역 SOR과는 다른, ROI 표본 전체에 대한 단순 통계 필터)
        double mean = 0;
        for (float v : z) mean += v;
        mean /= z.size();

        double var = 0;
        for (float v : z) { double d = v - mean; var += d * d; }
        double sd = std::sqrt(var / z.size());

        double sum = 0; int kept = 0;
        const double thresh = m_params.sorSigma * sd;
        for (float v : z) {
            if (sd > 0.0 && std::abs(v - mean) > thresh) { ++rejected; continue; }
            sum += v; ++kept;
        }
        avg = kept > 0 ? sum / kept : mean;
    } else {
        // PercentileTrim — 정렬 후 상/하위 tail % 절삭
        std::sort(z.begin(), z.end());
        int loCut = static_cast<int>(z.size() * std::clamp(m_params.lowTailPct,  0.f, 100.f) / 100.0);
        int hiCut = static_cast<int>(z.size() * std::clamp(m_params.highTailPct, 0.f, 100.f) / 100.0);
        // 양쪽 절삭이 표본 전체를 삼키면 아무것도 안 자른 것으로 폴백(평균 0/음수 개수 방지)
        if (loCut + hiCut >= static_cast<int>(z.size())) { loCut = 0; hiCut = 0; }

        double sum = 0;
        for (int i = loCut; i < static_cast<int>(z.size()) - hiCut; ++i) sum += z[i];
        int kept = static_cast<int>(z.size()) - loCut - hiCut;
        rejected = totalCount - kept;
        avg = kept > 0 ? sum / kept : 0.0;
    }

    m_result.avgHeightMm   = avg;
    m_result.sampleCount   = totalCount - rejected;
    m_result.rejectedCount = rejected;
    m_result.valid         = true;

    VISION_LOG_INFO("RefHeight: avg={:.6f}mm samples={} rejected={} (mode={})",
        avg, m_result.sampleCount, rejected,
        m_params.mode == RefHeightParams::OutlierMode::Sor ? "sor" : "percentileTrim");

    // 타입화 출력 — 두 가지를 동시에 전달:
    //  plane:   PlaneFit과 동일한 계약(a=0,b=0,c=avgHeightMm인 수평 평면) → HeightMeasure로.
    //           HeightMeasure는 plane 유무만 보고 signedDistance()를 쓰므로 코드 변경 없이 그대로 꽂힌다.
    //  heights: 평균값 하나짜리 배열 → CsvWriter로. main.cpp 입력 병합이 heights를 이어붙이므로,
    //           HeightMeasure의 측정값들과 함께 한 CsvWriter 행에 같이 기록할 수 있다.
    auto out = std::make_shared<VisionData>();
    out->plane = std::make_shared<PlaneModel>(PlaneModel{ 0.0, 0.0, avg, true });
    out->heights = std::make_shared<std::vector<double>>(std::vector<double>{ avg });
    out->sourceId = input->sourceId;
    return { ToolStatus::Ok, "", out };
}

// ─────────────────────────────────────────────────────────────────────
//  extractZ — percentage ROI → 유효 Z값 목록
// ─────────────────────────────────────────────────────────────────────
std::vector<float> RefHeightTool::extractZ(const HeightMap& map, const RefHeightParams::ROI& roi,
                                            int offCol, int offRow) const {
    int x0 = static_cast<int>(roi.xPct * map.width)               + offCol;
    int y0 = static_cast<int>(roi.yPct * map.height)              + offRow;
    int x1 = static_cast<int>((roi.xPct + roi.wPct) * map.width)  + offCol;
    int y1 = static_cast<int>((roi.yPct + roi.hPct) * map.height) + offRow;

    x0 = std::clamp(x0, 0, map.width  - 1);
    y0 = std::clamp(y0, 0, map.height - 1);
    x1 = std::clamp(x1, 0, map.width);
    y1 = std::clamp(y1, 0, map.height);

    std::vector<float> z;
    z.reserve(static_cast<size_t>(std::max(0, x1 - x0)) * std::max(0, y1 - y0));

    for (int row = y0; row < y1; ++row)
        for (int col = x0; col < x1; ++col)
            if (map.valid(col, row))
                z.push_back(map.zMm(col, row));
    return z;
}

} // namespace vision
