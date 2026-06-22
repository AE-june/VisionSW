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
        return { ToolStatus::Fail, "HeightFromPlane: ZMap이 없습니다." };
    if (!input->hasPlane())
        return { ToolStatus::Fail, "HeightFromPlane: 입력에 평면이 없습니다. PlaneFit 노드를 먼저 연결하세요." };
    if (m_params.measureRois.empty())
        return { ToolStatus::Fail, "HeightFromPlane: Measure ROI가 없습니다." };

    const ZMap&       map   = *input->zmap;
    const PlaneModel& plane = *input->plane;

    bool allPass = true;
    for (const auto& roi : m_params.measureRois) {
        auto pts = extractPoints(map, roi);

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
        hm.distance   = plane.signedDistance(rep[0], rep[1], rep[2]);

        if (m_params.useTolerance) {
            hm.pass = std::abs(hm.distance - m_params.nominalMm) <= m_params.toleranceMm;
            if (!hm.pass) allPass = false;
        } else {
            hm.pass = true;
        }

        VISION_LOG_INFO("HeightFromPlane: Q=({:.3f},{:.3f},{:.4f})  dist={:.4f} mm  pts={}  {}",
            hm.cx, hm.cy, hm.z, hm.distance, hm.pointCount,
            hm.pass ? "PASS" : "FAIL");

        m_result.measures.push_back(hm);
    }

    m_result.valid   = true;
    m_result.allPass = allPass;

    // 평면 정보를 그대로 다음 노드로 전달
    return { ToolStatus::Ok, "", std::make_shared<VisionData>(*input) };
}

// ─────────────────────────────────────────────────────────────────────
//  extractPoints — percentage ROI → (x_mm, y_mm, z_mm)
// ─────────────────────────────────────────────────────────────────────
std::vector<HeightFromPlaneTool::Pt3>
HeightFromPlaneTool::extractPoints(const ZMap& map,
                                   const HeightFromPlaneParams::ROI& roi) const {
    int x0 = static_cast<int>(roi.xPct * map.width);
    int y0 = static_cast<int>(roi.yPct * map.height);
    int x1 = static_cast<int>((roi.xPct + roi.wPct) * map.width);
    int y1 = static_cast<int>((roi.yPct + roi.hPct) * map.height);

    x0 = std::clamp(x0, 0, map.width  - 1);
    y0 = std::clamp(y0, 0, map.height - 1);
    x1 = std::clamp(x1, 0, map.width);
    y1 = std::clamp(y1, 0, map.height);

    std::vector<Pt3> pts;
    pts.reserve(static_cast<size_t>(x1 - x0) * (y1 - y0));

    for (int col = x0; col < x1; ++col)
        for (int row = y0; row < y1; ++row)
            if (map.valid(col, row))
                pts.push_back({ map.xMm(col), map.yMm(row),
                                static_cast<double>(map.zMm(col, row)) });
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
        std::vector<Pt3> sorted = pts;
        std::sort(sorted.begin(), sorted.end(),
                  [](const Pt3& a, const Pt3& b){ return a[2] < b[2]; });
        int n = std::max(1, static_cast<int>(
            std::ceil(sorted.size() * m_params.highTailPct / 100.f)));
        int start = static_cast<int>(sorted.size()) - n;
        double sx = 0, sy = 0, sz = 0;
        for (int i = start; i < static_cast<int>(sorted.size()); ++i) {
            sx += sorted[i][0]; sy += sorted[i][1]; sz += sorted[i][2];
        }
        return { sx / n, sy / n, sz / n };
    }
    }
    return pts[0];
}

} // namespace vision
