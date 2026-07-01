#include "HeightFromPlaneTool.h"
#include "Logger.h"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace vision {

HeightFromPlaneTool::HeightFromPlaneTool(HeightFromPlaneParams params)
    : m_params(std::move(params)) {}

// ═════════════════════════════════════════════════════════════════════
//  execute
// ═════════════════════════════════════════════════════════════════════
ToolResult HeightFromPlaneTool::execute(VisionDataPtr input) {
    m_result = {};

    if (!input || !input->hasZMap())
        return { ToolStatus::Fail, "HeightMeasure: ZMap이 없습니다." };
    if (m_params.measureRois.empty())
        return { ToolStatus::Fail, "HeightMeasure: 측정 ROI가 없습니다." };

    const ZMap&       map   = *input->zmap;
    // plane 입력이 있으면 평면 기준 수직거리, 없으면 절대 높이(z)
    const PlaneModel* plane = input->hasPlane() ? input->plane.get() : nullptr;
    // ZMap 원점이 설정돼 있으면(Align 통과) ROI를 원점만큼 이동.
    // ROI 좌표는 원점 기준 상대값(px)이라 검출된 원점에 더해 절대 위치를 만든다.
    // 원점이 0이면 기존과 동일(하위 호환).
    const int offCol = static_cast<int>(std::lround(map.originCol));
    const int offRow = static_cast<int>(std::lround(map.originRow));

    bool allPass = true;
    for (const auto& roi : m_params.measureRois) {
        auto pts = extractPoints(map, roi, offCol, offRow);

        HeightMeasure hm;
        if (pts.empty()) {
            hm.pointCount = 0;
            hm.pass = false;
            allPass = false;
            m_result.measures.push_back(hm);
            continue;
        }

        Pt3 rep = aggregate(pts);
        hm.cx = rep[0];
        hm.cy = rep[1];
        hm.z  = rep[2];
        hm.pointCount = static_cast<int>(pts.size());
        hm.distance   = plane ? plane->signedDistance(rep[0], rep[1], rep[2]) : rep[2];

        if (m_params.useTolerance) {
            hm.pass = std::abs(hm.distance - m_params.nominalMm) <= m_params.toleranceMm;
            if (!hm.pass) allPass = false;
        } else {
            hm.pass = true;
        }

        VISION_LOG_INFO("HeightMeasure: Q=({:.3f},{:.3f},{:.4f})  dist={:.4f} mm  pts={}  {}",
            hm.cx, hm.cy, hm.z, hm.distance, hm.pointCount,
            hm.pass ? "PASS" : "FAIL");

        m_result.measures.push_back(hm);
    }

    m_result.valid   = true;
    m_result.allPass = allPass;

    // 출력: 측정된 높이값 배열을 첨부 (+ 입력 zmap/plane은 미리보기/체인용으로 통과)
    auto out = std::make_shared<VisionData>(*input);
    out->heights = std::make_shared<std::vector<double>>();
    out->heights->reserve(m_result.measures.size());
    for (const auto& m : m_result.measures) out->heights->push_back(m.distance);
    return { ToolStatus::Ok, "", out };
}

// ─────────────────────────────────────────────────────────────────────
//  extractPoints — percentage ROI → (x_mm, y_mm, z_mm)
// ─────────────────────────────────────────────────────────────────────
std::vector<HeightFromPlaneTool::Pt3>
HeightFromPlaneTool::extractPoints(const ZMap& map,
                                   const HeightFromPlaneParams::ROI& roi,
                                   int offCol, int offRow) const {
    int x0 = static_cast<int>(roi.xPct * map.width)               + offCol;
    int y0 = static_cast<int>(roi.yPct * map.height)              + offRow;
    int x1 = static_cast<int>((roi.xPct + roi.wPct) * map.width)  + offCol;
    int y1 = static_cast<int>((roi.yPct + roi.hPct) * map.height) + offRow;

    x0 = std::clamp(x0, 0, map.width  - 1);
    y0 = std::clamp(y0, 0, map.height - 1);
    x1 = std::clamp(x1, 0, map.width);
    y1 = std::clamp(y1, 0, map.height);

    std::vector<Pt3> pts;
    pts.reserve(static_cast<size_t>(x1 - x0) * (y1 - y0));

    // 원형(타원) ROI면 내접 타원 내부 픽셀만
    const double cx = (x0 + x1) * 0.5, cy = (y0 + y1) * 0.5;
    const double rx = std::max(1.0, (x1 - x0) * 0.5), ry = std::max(1.0, (y1 - y0) * 0.5);

    // row-major 순회 (data가 [row*width+col] 이므로 캐시 효율적)
    for (int row = y0; row < y1; ++row)
        for (int col = x0; col < x1; ++col) {
            if (!map.valid(col, row)) continue;
            if (roi.isCircle) {
                double nx = (col - cx) / rx, ny = (row - cy) / ry;
                if (nx * nx + ny * ny > 1.0) continue;
            }
            pts.push_back({ map.xMm(col), map.yMm(row),
                            static_cast<double>(map.zMm(col, row)) });
        }
    return pts;
}

// ─────────────────────────────────────────────────────────────────────
//  aggregate — ROI 내 점들 → 대표 (x, y, z)
//   Max:      최대 Z 점 (위치도 그 점)
//   Mean:     전체 평균
//   HighTail: 상위 highTailPct% Z의 평균 (위치도 그 점들의 평균)
// ─────────────────────────────────────────────────────────────────────
HeightFromPlaneTool::Pt3
HeightFromPlaneTool::aggregate(const std::vector<Pt3>& pts) const {
    switch (m_params.aggregation) {
    case HeightFromPlaneParams::Aggregation::Max: {
        const Pt3* best = &pts[0];
        for (const auto& p : pts)
            if (p[2] > (*best)[2]) best = &p;
        return *best;
    }
    case HeightFromPlaneParams::Aggregation::Mean: {
        double sx = 0, sy = 0, sz = 0;
        for (const auto& p : pts) { sx += p[0]; sy += p[1]; sz += p[2]; }
        double n = static_cast<double>(pts.size());
        return { sx / n, sy / n, sz / n };
    }
    case HeightFromPlaneParams::Aggregation::HighTail: {
        std::vector<Pt3> buf = pts;
        int n = std::max(1, static_cast<int>(
            std::ceil(buf.size() * m_params.highTailPct / 100.f)));
        int start = static_cast<int>(buf.size()) - n;
        // 상위 n개만 필요 → nth_element로 부분선택 (O(N))
        std::nth_element(buf.begin(), buf.begin() + start, buf.end(),
                         [](const Pt3& a, const Pt3& b){ return a[2] < b[2]; });
        double sx = 0, sy = 0, sz = 0;
        for (int i = start; i < static_cast<int>(buf.size()); ++i) {
            sx += buf[i][0]; sy += buf[i][1]; sz += buf[i][2];
        }
        return { sx / n, sy / n, sz / n };
    }
    }
    return pts[0];
}

} // namespace vision
