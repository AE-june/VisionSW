#include "LineFitHeightMeasure.h"
#include "Logger.h"

#include <algorithm>
#include <numeric>
#include <random>
#include <cmath>
#include <stdexcept>

namespace vision {

// ═════════════════════════════════════════════════════════════════════
//  생성자
// ═════════════════════════════════════════════════════════════════════
LineFitHeightMeasure::LineFitHeightMeasure(LineFitParams params)
    : m_params(std::move(params))
{}

// ═════════════════════════════════════════════════════════════════════
//  execute
// ═════════════════════════════════════════════════════════════════════
ToolResult LineFitHeightMeasure::execute(VisionDataPtr input) {
    m_lastResult = {};

    if (!input || !input->hasZMap())
        return { ToolStatus::Fail, "LineFitHeightMeasure: ZMap이 없습니다." };

    const ZMap& map = *input->zmap;

    // ── 1. ROI 유효성 검사 ───────────────────────────────────────────
    for (auto* roi : { &m_params.roiFit1, &m_params.roiFit2, &m_params.roiMeasure }) {
        if (!roi->valid())
            return { ToolStatus::Fail, "LineFitHeightMeasure: 유효하지 않은 ROI" };
        if (!roi->fitsIn(map))
            return { ToolStatus::Fail, "LineFitHeightMeasure: ROI가 ZMap 범위를 벗어남" };
    }

    if (m_params.referenceMode == ReferenceMode::Line) {
        return executeLine(map, input);
    } else {
        return executePlane(map, input);
    }
}

// ─────────────────────────────────────────────────────────────────────
//  executeLine — XZ 평면 직선 기준
// ─────────────────────────────────────────────────────────────────────
ToolResult LineFitHeightMeasure::executeLine(const ZMap& map, VisionDataPtr input) {
    // ── 2. Fit ROI에서 col별 (x_mm, z_mm) 추출 ──────────────────────
    auto pts1 = extractPoints(map, m_params.roiFit1);
    auto pts2 = extractPoints(map, m_params.roiFit2);

    if (pts1.size() + pts2.size() < 2)
        return { ToolStatus::Fail, "LineFitHeightMeasure: 피팅 포인트 부족" };

    std::vector<XZPair> fitPts;
    fitPts.insert(fitPts.end(), pts1.begin(), pts1.end());
    fitPts.insert(fitPts.end(), pts2.begin(), pts2.end());

    // ── 3. 직선 피팅 ────────────────────────────────────────────────
    LineParams line = m_params.useRansac ? fitRansac(fitPts) : fitLS(fitPts);
    if (!line.valid)
        return { ToolStatus::Fail, "LineFitHeightMeasure: 직선 피팅 실패" };

    VISION_LOG_INFO("LineFit[Line]: z = {:.6f}*x + {:.6f}, inliers={}",
        line.slope, line.intercept, line.inliers);

    // ── 4. Measure ROI → Q 선택 (HighTail) ──────────────────────────
    auto [Qx, Qz] = selectMeasurePoint(map);
    if (std::isnan(Qz))
        return { ToolStatus::Fail, "LineFitHeightMeasure: Measure ROI에 유효 포인트 없음" };

    // ── 5. Z축 단차 계산 ─────────────────────────────────────────────
    double refZ      = line.slope * Qx + line.intercept;
    double heightDiff = Qz - refZ;

    double Fx = 0, Fz = 0;
    computeFoot(Qx, Qz, line.slope, line.intercept, Fx, Fz);

    // ── 6. 결과 저장 ─────────────────────────────────────────────────
    m_lastResult.slope       = line.slope;
    m_lastResult.intercept   = line.intercept;
    m_lastResult.inlierCount = line.inliers;
    m_lastResult.Qx          = Qx;
    m_lastResult.Qz          = Qz;
    m_lastResult.refZatQ     = refZ;
    m_lastResult.Fx          = Fx;
    m_lastResult.Fz          = Fz;
    m_lastResult.heightDiff  = heightDiff;
    m_lastResult.valid       = true;

    VISION_LOG_INFO("LineFit[Line]: Q=({:.3f}, {:.3f})  refZ={:.4f}  Δz={:.4f} mm",
        Qx, Qz, refZ, heightDiff);

    return { ToolStatus::Ok, "", std::make_shared<VisionData>(*input) };
}

// ─────────────────────────────────────────────────────────────────────
//  executePlane — XYZ 평면 기준
// ─────────────────────────────────────────────────────────────────────
ToolResult LineFitHeightMeasure::executePlane(const ZMap& map, VisionDataPtr input) {
    // ── 2. Fit ROI에서 col별 (x_mm, y_mm, z_mm) 추출 ────────────────
    auto pts1 = extractPoints3D(map, m_params.roiFit1);
    auto pts2 = extractPoints3D(map, m_params.roiFit2);

    if (pts1.size() + pts2.size() < 3)
        return { ToolStatus::Fail, "LineFitHeightMeasure: 평면 피팅 포인트 부족 (최소 3개)" };

    std::vector<XYZTriplet> fitPts;
    fitPts.insert(fitPts.end(), pts1.begin(), pts1.end());
    fitPts.insert(fitPts.end(), pts2.begin(), pts2.end());

    // ── 3. 평면 피팅 z = a*x + b*y + c ─────────────────────────────
    PlaneParams plane = fitPlane(fitPts);
    if (!plane.valid)
        return { ToolStatus::Fail, "LineFitHeightMeasure: 평면 피팅 실패" };

    VISION_LOG_INFO("LineFit[Plane]: z = {:.6f}*x + {:.6f}*y + {:.6f}",
        plane.a, plane.b, plane.c);

    // ── 4. Measure ROI → Q 선택 (HighTail, XYZ) ─────────────────────
    auto [Qx, Qy, Qz] = selectMeasurePoint3D(map);
    if (std::isnan(Qz))
        return { ToolStatus::Fail, "LineFitHeightMeasure: Measure ROI에 유효 포인트 없음" };

    // ── 5. Z축 단차 계산 ─────────────────────────────────────────────
    double refZ      = plane.a * Qx + plane.b * Qy + plane.c;
    double heightDiff = Qz - refZ;

    // ── 6. 결과 저장 ─────────────────────────────────────────────────
    m_lastResult.planeA      = plane.a;
    m_lastResult.planeB      = plane.b;
    m_lastResult.planeC      = plane.c;
    m_lastResult.Qx          = Qx;
    m_lastResult.Qy          = Qy;
    m_lastResult.Qz          = Qz;
    m_lastResult.refZatQ     = refZ;
    m_lastResult.heightDiff  = heightDiff;
    m_lastResult.valid       = true;

    VISION_LOG_INFO("LineFit[Plane]: Q=({:.3f}, {:.3f}, {:.3f})  refZ={:.4f}  Δz={:.4f} mm",
        Qx, Qy, Qz, refZ, heightDiff);

    return { ToolStatus::Ok, "", std::make_shared<VisionData>(*input) };
}

// ═════════════════════════════════════════════════════════════════════
//  extractPoints  — Fit ROI에서 col별 (x_mm, z_mm) [Line 모드]
// ═════════════════════════════════════════════════════════════════════
std::vector<LineFitHeightMeasure::XZPair>
LineFitHeightMeasure::extractPoints(const ZMap& map, const Rect2D& roi) const {
    std::vector<XZPair> pts;
    pts.reserve(roi.w);

    for (int col = roi.x; col < roi.right(); ++col) {
        double z = aggregateColumn(map, roi, col);
        if (!std::isnan(z))
            pts.emplace_back(map.xMm(col), z);
    }
    return pts;
}

// ═════════════════════════════════════════════════════════════════════
//  extractPoints3D  — Fit ROI에서 col별 (x_mm, y_mm, z_mm) [Plane 모드]
//  aggregation 방식으로 Z 선택, 해당 행의 Y 좌표를 함께 반환
// ═════════════════════════════════════════════════════════════════════
std::vector<LineFitHeightMeasure::XYZTriplet>
LineFitHeightMeasure::extractPoints3D(const ZMap& map, const Rect2D& roi) const {
    std::vector<XYZTriplet> pts;
    pts.reserve(roi.w);

    for (int col = roi.x; col < roi.right(); ++col) {
        double x = map.xMm(col);

        // 유효 (row, z) 수집
        std::vector<std::pair<int,float>> rowZ;  // (row index, z_mm)
        rowZ.reserve(roi.h);
        for (int row = roi.y; row < roi.bottom(); ++row) {
            if (!map.valid(col, row)) continue;
            rowZ.emplace_back(row, map.zMm(col, row));
        }
        if (rowZ.empty()) continue;

        double bestY = 0, bestZ = 0;

        switch (m_params.aggregation) {
        case ZAggregation::Max: {
            auto it = std::max_element(rowZ.begin(), rowZ.end(),
                          [](auto& a, auto& b){ return a.second < b.second; });
            bestY = map.yMm(it->first);
            bestZ = it->second;
            break;
        }
        case ZAggregation::Mean: {
            double sy = 0, sz = 0;
            for (auto& [r, z] : rowZ) { sy += map.yMm(r); sz += z; }
            bestY = sy / rowZ.size();
            bestZ = sz / rowZ.size();
            break;
        }
        case ZAggregation::HighTail: {
            std::sort(rowZ.begin(), rowZ.end(),
                      [](auto& a, auto& b){ return a.second < b.second; });
            int n = std::max(1, static_cast<int>(
                std::ceil(rowZ.size() * m_params.highTailPct / 100.f)));
            int start = static_cast<int>(rowZ.size()) - n;
            double sy = 0, sz = 0;
            for (int i = start; i < static_cast<int>(rowZ.size()); ++i) {
                sy += map.yMm(rowZ[i].first);
                sz += rowZ[i].second;
            }
            bestY = sy / n;
            bestZ = sz / n;
            break;
        }
        }

        pts.emplace_back(x, bestY, bestZ);
    }
    return pts;
}

// ═════════════════════════════════════════════════════════════════════
//  aggregateColumn  — 한 컬럼의 Z값 집계 (mm, Line 모드용)
// ═════════════════════════════════════════════════════════════════════
double LineFitHeightMeasure::aggregateColumn(const ZMap& map,
                                              const Rect2D& roi, int col) const {
    std::vector<float> zvals;
    zvals.reserve(roi.h);

    for (int row = roi.y; row < roi.bottom(); ++row) {
        if (!map.valid(col, row)) continue;
        zvals.push_back(map.zMm(col, row));
    }

    if (zvals.empty()) return std::numeric_limits<double>::quiet_NaN();

    switch (m_params.aggregation) {
    case ZAggregation::Max:
        return *std::max_element(zvals.begin(), zvals.end());
    case ZAggregation::Mean:
        return std::accumulate(zvals.begin(), zvals.end(), 0.f) / zvals.size();
    case ZAggregation::HighTail: {
        std::sort(zvals.begin(), zvals.end());
        int n = std::max(1, static_cast<int>(
            std::ceil(zvals.size() * m_params.highTailPct / 100.f)));
        int start = static_cast<int>(zvals.size()) - n;
        double sum = 0;
        for (int i = start; i < static_cast<int>(zvals.size()); ++i) sum += zvals[i];
        return sum / n;
    }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

// ═════════════════════════════════════════════════════════════════════
//  selectMeasurePoint  — Measure ROI 전체 → HighTail → (Qx, Qz)
// ═════════════════════════════════════════════════════════════════════
LineFitHeightMeasure::XZPair
LineFitHeightMeasure::selectMeasurePoint(const ZMap& map) const {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const Rect2D& roi = m_params.roiMeasure;

    std::vector<XZPair> pts;
    pts.reserve(static_cast<size_t>(roi.w) * roi.h);

    for (int col = roi.x; col < roi.right(); ++col) {
        double x = map.xMm(col);
        for (int row = roi.y; row < roi.bottom(); ++row) {
            if (!map.valid(col, row)) continue;
            pts.emplace_back(x, static_cast<double>(map.zMm(col, row)));
        }
    }

    if (pts.empty()) return { 0.0, nan };

    std::sort(pts.begin(), pts.end(),
              [](const XZPair& a, const XZPair& b){ return a.second < b.second; });

    int n = std::max(1, static_cast<int>(
        std::ceil(static_cast<double>(pts.size()) * m_params.measureHighTailPct / 100.0)));
    int start = static_cast<int>(pts.size()) - n;

    double sumX = 0.0, sumZ = 0.0;
    for (int i = start; i < static_cast<int>(pts.size()); ++i) {
        sumX += pts[i].first;
        sumZ += pts[i].second;
    }

    return { sumX / n, sumZ / n };
}

// ═════════════════════════════════════════════════════════════════════
//  selectMeasurePoint3D  — Measure ROI 전체 → HighTail → (Qx, Qy, Qz)
// ═════════════════════════════════════════════════════════════════════
LineFitHeightMeasure::XYZTriplet
LineFitHeightMeasure::selectMeasurePoint3D(const ZMap& map) const {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const Rect2D& roi = m_params.roiMeasure;

    std::vector<XYZTriplet> pts;
    pts.reserve(static_cast<size_t>(roi.w) * roi.h);

    for (int col = roi.x; col < roi.right(); ++col) {
        double x = map.xMm(col);
        for (int row = roi.y; row < roi.bottom(); ++row) {
            if (!map.valid(col, row)) continue;
            pts.emplace_back(x, static_cast<double>(map.yMm(row)),
                             static_cast<double>(map.zMm(col, row)));
        }
    }

    if (pts.empty()) return { 0.0, 0.0, nan };

    std::sort(pts.begin(), pts.end(),
              [](const XYZTriplet& a, const XYZTriplet& b){
                  return std::get<2>(a) < std::get<2>(b);
              });

    int n = std::max(1, static_cast<int>(
        std::ceil(static_cast<double>(pts.size()) * m_params.measureHighTailPct / 100.0)));
    int start = static_cast<int>(pts.size()) - n;

    double sumX = 0.0, sumY = 0.0, sumZ = 0.0;
    for (int i = start; i < static_cast<int>(pts.size()); ++i) {
        sumX += std::get<0>(pts[i]);
        sumY += std::get<1>(pts[i]);
        sumZ += std::get<2>(pts[i]);
    }

    return { sumX / n, sumY / n, sumZ / n };
}

// ═════════════════════════════════════════════════════════════════════
//  fitPlane  — 최소제곱 평면 피팅  z = a*x + b*y + c
//
//  정규방정식 (3×3):
//  [ Sxx  Sxy  Sx ] [a]   [Sxz]
//  [ Sxy  Syy  Sy ] [b] = [Syz]
//  [ Sx   Sy   n  ] [c]   [Sz ]
//
//  가우스 소거법 (부분 피벗팅) 으로 풀기
// ═════════════════════════════════════════════════════════════════════
LineFitHeightMeasure::PlaneParams
LineFitHeightMeasure::fitPlane(const std::vector<XYZTriplet>& pts) const {
    if (pts.size() < 3) return {};

    double Sxx=0, Sxy=0, Sx=0;
    double Syy=0, Sy=0,  Sn=0;
    double Sxz=0, Syz=0, Sz=0;

    for (auto& [x, y, z] : pts) {
        Sxx += x*x; Sxy += x*y; Sx += x;
        Syy += y*y; Sy  += y;   Sn += 1.0;
        Sxz += x*z; Syz += y*z; Sz += z;
    }

    // 증강 행렬 [A | b]
    double A[3][4] = {
        { Sxx, Sxy, Sx, Sxz },
        { Sxy, Syy, Sy, Syz },
        { Sx,  Sy,  Sn, Sz  }
    };

    // 가우스 소거 (부분 피벗팅)
    for (int i = 0; i < 3; ++i) {
        // 피벗 선택
        int pivot = i;
        for (int j = i+1; j < 3; ++j)
            if (std::abs(A[j][i]) > std::abs(A[pivot][i])) pivot = j;
        std::swap(A[i], A[pivot]);

        if (std::abs(A[i][i]) < 1e-12) return {};

        for (int j = i+1; j < 3; ++j) {
            double f = A[j][i] / A[i][i];
            for (int k = i; k < 4; ++k)
                A[j][k] -= f * A[i][k];
        }
    }

    // 후방 대입
    double sol[3] = {};
    for (int i = 2; i >= 0; --i) {
        sol[i] = A[i][3];
        for (int j = i+1; j < 3; ++j)
            sol[i] -= A[i][j] * sol[j];
        sol[i] /= A[i][i];
    }

    return { sol[0], sol[1], sol[2], true };
}

// ═════════════════════════════════════════════════════════════════════
//  fitLS / fitRansac / computeFoot  (Line 모드 전용)
// ═════════════════════════════════════════════════════════════════════
LineFitHeightMeasure::LineParams
LineFitHeightMeasure::fitLS(const std::vector<XZPair>& pts) const {
    if (pts.size() < 2) return {};

    double n = static_cast<double>(pts.size());
    double sumX=0, sumZ=0, sumXZ=0, sumX2=0;

    for (auto& [x, z] : pts) {
        sumX  += x;  sumZ  += z;
        sumXZ += x*z; sumX2 += x*x;
    }

    double denom = n * sumX2 - sumX * sumX;
    if (std::abs(denom) < 1e-12) return {};

    double a = (n * sumXZ - sumX * sumZ) / denom;
    double b = (sumZ - a * sumX) / n;

    return { a, b, static_cast<int>(pts.size()), true };
}

LineFitHeightMeasure::LineParams
LineFitHeightMeasure::fitRansac(const std::vector<XZPair>& pts) const {
    if (pts.size() < 2) return {};

    const int    maxIter = m_params.ransacIterations;
    const double thresh  = m_params.ransacThresholdMm;

    LineParams best;
    int bestCount = 0;

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, static_cast<int>(pts.size()) - 1);

    for (int iter = 0; iter < maxIter; ++iter) {
        int i = dist(rng), j = dist(rng);
        while (j == i) j = dist(rng);

        double dx = pts[j].first - pts[i].first;
        if (std::abs(dx) < 1e-12) continue;

        double a = (pts[j].second - pts[i].second) / dx;
        double b = pts[i].second - a * pts[i].first;
        double nf = std::sqrt(1.0 + a*a);

        int inliers = 0;
        for (auto& [x, z] : pts)
            if (std::abs(z - (a*x + b)) / nf < thresh) ++inliers;

        if (inliers > bestCount) {
            bestCount = inliers;
            best = { a, b, inliers, true };
        }
    }

    if (!best.valid) return {};

    // 인라이어로 재피팅
    std::vector<XZPair> inliers;
    double a = best.slope, b = best.intercept;
    double nf = std::sqrt(1.0 + a*a);
    for (auto& [x, z] : pts)
        if (std::abs(z - (a*x + b)) / nf < thresh)
            inliers.push_back({x, z});

    if (inliers.size() >= 2) {
        auto refined = fitLS(inliers);
        refined.inliers = static_cast<int>(inliers.size());
        return refined;
    }
    return best;
}

void LineFitHeightMeasure::computeFoot(double Qx, double Qz,
                                        double slope, double intercept,
                                        double& Fx, double& Fz) {
    double d2 = 1.0 + slope * slope;
    double t  = (Qx + (Qz - intercept) * slope) / d2;
    Fx = t;
    Fz = intercept + t * slope;
}

} // namespace vision
