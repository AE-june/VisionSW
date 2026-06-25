#include "PlaneFitTool.h"
#include "Logger.h"
#include <algorithm>
#include <numeric>
#include <random>
#include <cmath>
#include <Eigen/Dense>

namespace vision {

PlaneFitTool::PlaneFitTool(PlaneFitParams params) : m_params(std::move(params)) {}

// ═════════════════════════════════════════════════════════════════════
//  execute
// ═════════════════════════════════════════════════════════════════════
ToolResult PlaneFitTool::execute(VisionDataPtr input) {
    m_result = {};

    if (!input || !input->hasZMap())
        return { ToolStatus::Fail, "PlaneFit: ZMap이 없습니다." };
    if (m_params.refRois.empty())
        return { ToolStatus::Fail, "PlaneFit: Reference ROI가 없습니다." };

    const ZMap& map = *input->zmap;

    // Collect points from all reference ROIs
    std::vector<Pt3> pts;
    for (const auto& roi : m_params.refRois) {
        auto p = extractPoints(map, roi);
        pts.insert(pts.end(), p.begin(), p.end());
    }
    if (pts.size() < 3)
        return { ToolStatus::Fail, "PlaneFit: 평면 피팅 포인트 부족 (최소 3개 필요)" };

    // Fit plane
    Plane plane;
    switch (m_params.algorithm) {
    case PlaneFitParams::Algorithm::LeastSquares: plane = fitLS(pts);     break;
    case PlaneFitParams::Algorithm::RANSAC:       plane = fitRANSAC(pts); break;
    case PlaneFitParams::Algorithm::SVD:          plane = fitSVD(pts);    break;
    }
    if (!plane.valid)
        return { ToolStatus::Fail, "PlaneFit: 평면 피팅 실패" };

    // RMSE: 전체 ref 포인트의 평면 대비 수직 잔차 RMS (OpenMP reduction)
    const double len = std::sqrt(1.0 + plane.a * plane.a + plane.b * plane.b);
    const long np = static_cast<long>(pts.size());
    double sse = 0;
    #pragma omp parallel for reduction(+:sse)
    for (long i = 0; i < np; ++i) {
        double resid = (pts[i][2] - (plane.a * pts[i][0] + plane.b * pts[i][1] + plane.c)) / len;
        sse += resid * resid;
    }
    double rmse = std::sqrt(sse / pts.size());
    double tiltDeg = std::atan(std::sqrt(plane.a * plane.a + plane.b * plane.b))
                     * 180.0 / 3.14159265358979323846;

    m_result.a   = plane.a;
    m_result.b   = plane.b;
    m_result.c   = plane.c;
    m_result.rmse          = rmse;
    m_result.tiltDeg       = tiltDeg;
    m_result.refPointCount = static_cast<int>(pts.size());
    m_result.inlierCount   = plane.inliers;
    m_result.valid         = true;

    // 3D 뷰용: 전체 ZMap을 격자 다운샘플 (목표 개수는 파라미터, 메모리 보호용 하드 상한)
    const size_t HARD_CAP = 500000;
    const size_t target = std::min(HARD_CAP,
        static_cast<size_t>(std::max(1, m_params.maxCloudPoints)));
    const size_t total  = static_cast<size_t>(map.width) * map.height;
    const int    step   = std::max(1, static_cast<int>(std::sqrt(
                              static_cast<double>(total) / target)));
    for (int row = 0; row < map.height; row += step)
        for (int col = 0; col < map.width; col += step)
            if (map.valid(col, row))
                m_result.cloudPoints.push_back({ map.xMm(col), map.yMm(row),
                                                 static_cast<double>(map.zMm(col, row)) });

    VISION_LOG_INFO("PlaneFit: z = {:.6f}*x + {:.6f}*y + {:.6f}  rmse={:.4f}mm tilt={:.3f}° pts={}",
        plane.a, plane.b, plane.c, rmse, tiltDeg, pts.size());

    // 출력: 입력 ZMap 복사 + 피팅된 평면 모델 첨부
    auto out = std::make_shared<VisionData>(*input);
    out->plane = std::make_shared<PlaneModel>(PlaneModel{ plane.a, plane.b, plane.c, true });
    return { ToolStatus::Ok, "", out };
}

// ─────────────────────────────────────────────────────────────────────
//  extractPoints — percentage ROI → (x_mm, y_mm, z_mm)
// ─────────────────────────────────────────────────────────────────────
std::vector<PlaneFitTool::Pt3>
PlaneFitTool::extractPoints(const ZMap& map, const PlaneFitParams::ROI& roi) const {
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

    // row-major 순회 (data가 [row*width+col] 이므로 캐시 효율적)
    for (int row = y0; row < y1; ++row)
        for (int col = x0; col < x1; ++col)
            if (map.valid(col, row))
                pts.push_back({ map.xMm(col), map.yMm(row),
                                 static_cast<double>(map.zMm(col, row)) });
    return pts;
}

// ─────────────────────────────────────────────────────────────────────
//  fitLS — 최소제곱법: z = a*x + b*y + c
//  중심화(centroid 차감) + Eigen ColPivHouseholderQR
//  → 좌표가 큰 경우(수천 mm) 정규방정식의 조건수 제곱 문제를 회피
// ─────────────────────────────────────────────────────────────────────
PlaneFitTool::Plane PlaneFitTool::fitLS(const std::vector<Pt3>& pts) const {
    if (pts.size() < 3) return {};
    const Eigen::Index n = static_cast<Eigen::Index>(pts.size());

    double cx = 0, cy = 0, cz = 0;
    for (const auto& p : pts) { cx += p[0]; cy += p[1]; cz += p[2]; }
    cx /= pts.size(); cy /= pts.size(); cz /= pts.size();

    // (z-cz) = a·(x-cx) + b·(y-cy)
    Eigen::MatrixXd A(n, 2);
    Eigen::VectorXd rhs(n);
    for (Eigen::Index i = 0; i < n; ++i) {
        A(i, 0) = pts[i][0] - cx;
        A(i, 1) = pts[i][1] - cy;
        rhs(i)  = pts[i][2] - cz;
    }
    const Eigen::Vector2d sol = A.colPivHouseholderQr().solve(rhs);
    const double a = sol(0), b = sol(1);
    const double c = cz - a * cx - b * cy;   // 원좌표 평면으로 복원
    return { a, b, c, true, static_cast<int>(pts.size()) };
}

// ─────────────────────────────────────────────────────────────────────
//  fitRANSAC
// ─────────────────────────────────────────────────────────────────────
PlaneFitTool::Plane PlaneFitTool::fitRANSAC(const std::vector<Pt3>& pts) const {
    if (pts.size() < 3) return {};

    const double thresh = m_params.ransacThresholdMm;
    const int    maxIt  = m_params.ransacIterations;

    Plane best;
    int bestCount = 0;
    const long np = static_cast<long>(pts.size());

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, static_cast<int>(pts.size()) - 1);

    int dynMaxIt = maxIt;   // inlier 비율에 따라 동적으로 줄어듦(조기 종료)
    for (int iter = 0; iter < dynMaxIt; ++iter) {
        int i0 = dist(rng), i1, i2;
        do { i1 = dist(rng); } while (i1 == i0);
        do { i2 = dist(rng); } while (i2 == i0 || i2 == i1);

        double x0=pts[i0][0], y0=pts[i0][1], z0=pts[i0][2];
        double ux=pts[i1][0]-x0, uy=pts[i1][1]-y0, uz=pts[i1][2]-z0;
        double vx=pts[i2][0]-x0, vy=pts[i2][1]-y0, vz=pts[i2][2]-z0;

        double nx = uy*vz - uz*vy;
        double ny = uz*vx - ux*vz;
        double nz = ux*vy - uy*vx;
        if (std::abs(nz) < 1e-12) continue;

        double a = -nx/nz, b = -ny/nz;
        double c = z0 - a*x0 - b*y0;
        double len = std::sqrt(1.0 + a*a + b*b);

        int inliers = 0;
        #pragma omp parallel for reduction(+:inliers)
        for (long i = 0; i < np; ++i)
            if (std::abs(pts[i][2] - (a*pts[i][0] + b*pts[i][1] + c)) / len < thresh) ++inliers;

        if (inliers > bestCount) {
            bestCount = inliers;
            best = { a, b, c, true, inliers };
            // 동적 반복수: inlier 비율 w로 99% 신뢰 도달에 필요한 반복수 추정
            double w = static_cast<double>(inliers) / static_cast<double>(np);
            double denom = std::log(1.0 - w * w * w);
            if (denom < 0) {
                int est = static_cast<int>(std::ceil(std::log(1.0 - 0.99) / denom));
                if (est < dynMaxIt) dynMaxIt = std::max(est, 10);
            }
        }
    }
    if (!best.valid) return {};

    // Refit on inliers
    double a = best.a, b = best.b, c = best.c;
    double len = std::sqrt(1.0 + a*a + b*b);
    std::vector<Pt3> inlierPts;
    for (auto& p : pts)
        if (std::abs(p[2] - (a*p[0] + b*p[1] + c)) / len < thresh)
            inlierPts.push_back(p);

    if (inlierPts.size() >= 3) {
        auto refined = fitLS(inlierPts);
        refined.inliers = static_cast<int>(inlierPts.size());
        return refined;
    }
    return best;
}

// ─────────────────────────────────────────────────────────────────────
//  fitSVD — PCA(직교거리 최소화): 중심화 후 공분산 행렬의 최소 고유벡터 = 법선
//  Eigen SelfAdjointEigenSolver 사용 (3×3 대칭, 고유값 오름차순)
// ─────────────────────────────────────────────────────────────────────
PlaneFitTool::Plane PlaneFitTool::fitSVD(const std::vector<Pt3>& pts) const {
    if (pts.size() < 3) return {};

    double cx = 0, cy = 0, cz = 0;
    for (const auto& p : pts) { cx += p[0]; cy += p[1]; cz += p[2]; }
    const double n = static_cast<double>(pts.size());
    cx /= n; cy /= n; cz /= n;

    Eigen::Matrix3d cov = Eigen::Matrix3d::Zero();
    for (const auto& p : pts) {
        const Eigen::Vector3d d(p[0] - cx, p[1] - cy, p[2] - cz);
        cov.noalias() += d * d.transpose();
    }

    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es(cov);
    const Eigen::Vector3d normal = es.eigenvectors().col(0);   // 최소 고유값

    const double nc = normal(2);
    if (std::abs(nc) < 1e-12) return {};
    const double a = -normal(0) / nc, b = -normal(1) / nc;
    const double c = cz - a * cx - b * cy;
    return { a, b, c, true, static_cast<int>(pts.size()) };
}

} // namespace vision
