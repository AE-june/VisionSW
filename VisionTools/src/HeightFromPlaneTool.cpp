#include "HeightFromPlaneTool.h"
#include "Logger.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <atomic>
#include <opencv2/core.hpp>

namespace vision {

HeightFromPlaneTool::HeightFromPlaneTool(HeightFromPlaneParams params)
    : m_params(std::move(params)) {}

// ═════════════════════════════════════════════════════════════════════
//  execute
// ═════════════════════════════════════════════════════════════════════
ToolResult HeightFromPlaneTool::execute(VisionDataPtr input) {
    m_result = {};

    if (!input || !input->hasHeightMap())
        return { ToolStatus::Fail, "HeightMeasure: HeightMap이 없습니다." };
    if (m_params.measureRois.empty())
        return { ToolStatus::Fail, "HeightMeasure: 측정 ROI가 없습니다." };

    const HeightMap&       map   = *input->heightmap;
    // plane 입력이 있으면 평면 기준 수직거리, 없으면 절대 높이(z)
    const PlaneModel* plane = input->hasPlane() ? input->plane.get() : nullptr;
    // HeightMap 원점이 설정돼 있으면(Align 통과) ROI를 원점만큼 이동.
    // ROI 좌표는 원점 기준 상대값(px)이라 검출된 원점에 더해 절대 위치를 만든다.
    // 원점이 0이면 기존과 동일(하위 호환).
    const int offCol = static_cast<int>(std::lround(map.originCol));
    const int offRow = static_cast<int>(std::lround(map.originRow));

    if (plane) {
        VISION_LOG_INFO("HeightMeasure [DIAG] plane: a={:.6f} b={:.6f} c={:.6f}  src={}",
            plane->a, plane->b, plane->c, input->sourceId);
    }

    // 마스크 경계를 픽셀 좌표로 1회 해석 → 픽셀 루프에서 재사용 (픽셀마다 재계산 방지)
    const auto masks = resolveMasks(map, offCol, offRow);

    // ROI별 측정은 서로 독립 → ROI 단위 병렬. 결과는 인덱스로 기록해 직렬과 동일 순서/값 보장.
    const int nRoi = static_cast<int>(m_params.measureRois.size());
    m_result.measures.assign(nRoi, HeightMeasure{});
    std::atomic<bool> allPassA{true};
    cv::parallel_for_(cv::Range(0, nRoi), [&](const cv::Range& rg) {
        for (int i = rg.start; i < rg.end; ++i) {
            const auto& roi = m_params.measureRois[i];
            auto pts = extractPoints(map, roi, offCol, offRow, masks);

            HeightMeasure hm;
            if (pts.empty()) {
                hm.pointCount = 0;
                hm.pass = false;
                allPassA = false;
                m_result.measures[i] = hm;
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
                if (!hm.pass) allPassA = false;
            } else {
                hm.pass = true;
            }
            m_result.measures[i] = hm;
        }
    });

    m_result.valid   = true;
    m_result.allPass = allPassA.load();

    // 타입화 출력: 측정된 높이값 배열만 전달 (이미지/plane 미포함).
    // 높이는 입력 heightmap을 입력 plane 기준으로 측정 — 결과창 이미지는 엔진이 입력 heightmap으로 폴백 표시.
    auto out = std::make_shared<VisionData>();
    out->heights = std::make_shared<std::vector<double>>();
    out->heights->reserve(m_result.measures.size());
    for (const auto& m : m_result.measures) out->heights->push_back(m.distance);
    out->sourceId = input->sourceId;
    return { ToolStatus::Ok, "", out };
}

// ─────────────────────────────────────────────────────────────────────
//  extractPoints — percentage ROI → (x_mm, y_mm, z_mm)
// ─────────────────────────────────────────────────────────────────────
std::vector<HeightFromPlaneTool::Pt3>
HeightFromPlaneTool::extractPoints(const HeightMap& map,
                                   const HeightFromPlaneParams::ROI& roi,
                                   int offCol, int offRow,
                                   const std::vector<MaskPx>& masks) const {
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
            if (masked(masks, col, row)) continue;   // 제외 영역 픽셀은 측정에서 뺀다
            pts.push_back({ map.xMm(col), map.yMm(row),
                            static_cast<double>(map.zMm(col, row)) });
        }
    return pts;
}

// ─────────────────────────────────────────────────────────────────────
//  resolveMasks — 마스크 ROI(pct)를 픽셀 좌표로 1회 해석.
//   기존에는 masked()가 픽셀마다 pct×dim 곱셈을 반복했으나, 여기서 미리 계산해
//   픽셀 루프는 정수 비교/타원 판정만 수행하도록 한다. (판정 규칙은 기존과 동일)
// ─────────────────────────────────────────────────────────────────────
std::vector<HeightFromPlaneTool::MaskPx>
HeightFromPlaneTool::resolveMasks(const HeightMap& map, int offCol, int offRow) const {
    std::vector<MaskPx> out;
    out.reserve(m_params.maskRois.size());
    for (const auto& mk : m_params.maskRois) {
        MaskPx m;
        if (!mk.poly.empty()) {   // 폴리곤: 꼭짓점을 px로 변환해 보관
            m.isPoly = true;
            m.poly.reserve(mk.poly.size());
            for (const auto& v : mk.poly)
                m.poly.push_back({ v[0] * map.width + offCol,
                                   v[1] * map.height + offRow });
        } else {                  // 사각형 / 원(내접 타원)
            m.x0 = static_cast<int>(mk.xPct * map.width)  + offCol;
            m.y0 = static_cast<int>(mk.yPct * map.height) + offRow;
            m.x1 = static_cast<int>((mk.xPct + mk.wPct) * map.width)  + offCol;
            m.y1 = static_cast<int>((mk.yPct + mk.hPct) * map.height) + offRow;
            m.isCircle = mk.isCircle;
            if (mk.isCircle) {
                m.cx = (m.x0 + m.x1) * 0.5; m.cy = (m.y0 + m.y1) * 0.5;
                m.rx = std::max(1, m.x1 - m.x0) * 0.5;
                m.ry = std::max(1, m.y1 - m.y0) * 0.5;
            }
        }
        out.push_back(std::move(m));
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────
//  masked — (col,row)가 마스크(제외) 영역 안인가. 사각형/원/폴리곤 지원.
// ─────────────────────────────────────────────────────────────────────
bool HeightFromPlaneTool::masked(const std::vector<MaskPx>& masks,
                                 int col, int row) const {
    for (const auto& mk : masks) {
        if (mk.isPoly) {   // 폴리곤: ray-casting (px 좌표)
            const int nv = static_cast<int>(mk.poly.size());
            bool in = false;
            for (int i = 0, j = nv - 1; i < nv; j = i++) {
                double xi = mk.poly[i][0], yi = mk.poly[i][1];
                double xj = mk.poly[j][0], yj = mk.poly[j][1];
                if (((yi > row) != (yj > row)) &&
                    (col < (xj - xi) * (row - yi) / (yj - yi) + xi)) in = !in;
            }
            if (in) return true;
        } else {                  // 사각형 / 원(내접 타원)
            if (col < mk.x0 || col >= mk.x1 || row < mk.y0 || row >= mk.y1) continue;
            if (mk.isCircle) {
                double nx = (col - mk.cx) / mk.rx, ny = (row - mk.cy) / mk.ry;
                if (nx * nx + ny * ny > 1.0) continue;
            }
            return true;
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────
//  aggregate — ROI 내 점들 → 대표 (x, y, z)
//   Max:      최대 Z 점 (위치도 그 점)
//   Mean:     전체 평균
//   HighTail: 상위 highTailPct% Z의 평균 (위치도 그 점들의 평균)
// ─────────────────────────────────────────────────────────────────────
HeightFromPlaneTool::Pt3
HeightFromPlaneTool::aggregate(std::vector<Pt3>& pts) const {
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
        // pts를 직접 부분정렬 (호출측에서 이후 재사용 안 함 → 복사 제거)
        int n = std::max(1, static_cast<int>(
            std::ceil(pts.size() * m_params.highTailPct / 100.f)));
        int start = static_cast<int>(pts.size()) - n;
        // 상위 n개만 필요 → nth_element로 부분선택 (O(N))
        std::nth_element(pts.begin(), pts.begin() + start, pts.end(),
                         [](const Pt3& a, const Pt3& b){ return a[2] < b[2]; });
        double sx = 0, sy = 0, sz = 0;
        for (int i = start; i < static_cast<int>(pts.size()); ++i) {
            sx += pts[i][0]; sy += pts[i][1]; sz += pts[i][2];
        }
        return { sx / n, sy / n, sz / n };
    }
    case HeightFromPlaneParams::Aggregation::Percentile: {
        // material ratio mr%(=highTailPct)에서의 높이 = 상위 mr% 백분위 "실측 점 하나". 표준(ISO 25178 베어링곡선).
        //  값·위치 모두 동일한 실제 픽셀에서 나오므로 결과 위치의 zmap 값과 정확히 일치(보간 안 함 → 잔차 0).
        //  순위 기반이라 그 위 극단 스파이크(리플/노이즈)에 강건.
        const int N = static_cast<int>(pts.size());
        auto byZ = [](const Pt3& a, const Pt3& b){ return a[2] < b[2]; };
        const double mr = std::clamp(m_params.highTailPct, 0.f, 100.f) / 100.0;
        const double q  = 1.0 - mr;                 // 오름차순 기준 분위 위치 (mr=0.5% → 0.995)
        const int idx = std::clamp(static_cast<int>(std::llround(q * (N - 1))), 0, N - 1);
        // idx번째 순위 점을 제자리에 → 그 점의 (x,y,z)를 그대로 사용 (실측 픽셀 1개)
        std::nth_element(pts.begin(), pts.begin() + idx, pts.end(), byZ);
        return pts[idx];
    }
    }
    return pts[0];
}

} // namespace vision
