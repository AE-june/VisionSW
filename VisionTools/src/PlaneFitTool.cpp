#include "PlaneFitTool.h"
#include "Logger.h"
#include <algorithm>
#include <numeric>
#include <random>
#include <cmath>

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

    // RMSE: 전체 ref 포인트의 평면 대비 수직 잔차 RMS
    const double len = std::sqrt(1.0 + plane.a * plane.a + plane.b * plane.b);
    double sse = 0;
    for (auto& p : pts) {
        double resid = (p[2] - (plane.a * p[0] + plane.b * p[1] + plane.c)) / len;
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

    for (int col = x0; col < x1; ++col)
        for (int row = y0; row < y1; ++row)
            if (map.valid(col, row))
                pts.push_back({ map.xMm(col), map.yMm(row),
                                 static_cast<double>(map.zMm(col, row)) });
    return pts;
}

// ─────────────────────────────────────────────────────────────────────
//  fitLS — 최소제곱법: z = a*x + b*y + c
// ─────────────────────────────────────────────────────────────────────
PlaneFitTool::Plane PlaneFitTool::fitLS(const std::vector<Pt3>& pts) const {
    if (pts.size() < 3) return {};

    double Sxx=0,Sxy=0,Sx=0, Syy=0,Sy=0, Sn=0;
    double Sxz=0,Syz=0,Sz=0;

    for (auto& p : pts) {
        double x = p[0], y = p[1], z = p[2];
        Sxx += x*x; Sxy += x*y; Sx += x;
        Syy += y*y; Sy  += y;   Sn += 1;
        Sxz += x*z; Syz += y*z; Sz += z;
    }

    double A[3][4] = {
        { Sxx, Sxy, Sx, Sxz },
        { Sxy, Syy, Sy, Syz },
        { Sx,  Sy,  Sn, Sz  }
    };

    for (int i = 0; i < 3; ++i) {
        int piv = i;
        for (int j = i+1; j < 3; ++j)
            if (std::abs(A[j][i]) > std::abs(A[piv][i])) piv = j;
        std::swap(A[i], A[piv]);
        if (std::abs(A[i][i]) < 1e-12) return {};
        for (int j = i+1; j < 3; ++j) {
            double f = A[j][i] / A[i][i];
            for (int k = i; k < 4; ++k) A[j][k] -= f * A[i][k];
        }
    }

    double sol[3] = {};
    for (int i = 2; i >= 0; --i) {
        sol[i] = A[i][3];
        for (int j = i+1; j < 3; ++j) sol[i] -= A[i][j] * sol[j];
        sol[i] /= A[i][i];
    }

    return { sol[0], sol[1], sol[2], true, static_cast<int>(pts.size()) };
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

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, static_cast<int>(pts.size()) - 1);

    for (int iter = 0; iter < maxIt; ++iter) {
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
        for (auto& p : pts)
            if (std::abs(p[2] - (a*p[0] + b*p[1] + c)) / len < thresh) ++inliers;

        if (inliers > bestCount) {
            bestCount = inliers;
            best = { a, b, c, true, inliers };
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
//  fitSVD — PCA 기반, Jacobi 고유값 분해
// ─────────────────────────────────────────────────────────────────────
PlaneFitTool::Plane PlaneFitTool::fitSVD(const std::vector<Pt3>& pts) const {
    if (pts.size() < 3) return {};

    double cx=0, cy=0, cz=0;
    for (auto& p : pts) { cx += p[0]; cy += p[1]; cz += p[2]; }
    const double n = static_cast<double>(pts.size());
    cx /= n; cy /= n; cz /= n;

    // Scatter matrix
    double S[3][3] = {};
    for (auto& p : pts) {
        double v[3] = { p[0]-cx, p[1]-cy, p[2]-cz };
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                S[i][j] += v[i] * v[j];
    }

    double evals[3], evecs[3][3];
    jacobi3(S, evals, evecs);

    // Normal = eigenvector for smallest eigenvalue (index 0 after ascending sort)
    double na = evecs[0][0], nb = evecs[1][0], nc = evecs[2][0];
    if (std::abs(nc) < 1e-12) return {};

    double a = -na/nc, b = -nb/nc;
    double c = cz - a*cx - b*cy;

    return { a, b, c, true, static_cast<int>(pts.size()) };
}

// ─────────────────────────────────────────────────────────────────────
//  jacobi3 — 3×3 대칭 행렬 Jacobi 고유값 분해
//  결과: evals (오름차순), evecs (열 = 고유벡터)
// ─────────────────────────────────────────────────────────────────────
void PlaneFitTool::jacobi3(double A[3][3], double evals[3], double evecs[3][3]) {
    double M[3][3];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            M[i][j]     = A[i][j];
            evecs[i][j] = (i == j) ? 1.0 : 0.0;
        }
    }

    for (int iter = 0; iter < 100; ++iter) {
        int p = 0, q = 1;
        double maxVal = std::abs(M[0][1]);
        if (std::abs(M[0][2]) > maxVal) { maxVal = std::abs(M[0][2]); p=0; q=2; }
        if (std::abs(M[1][2]) > maxVal) { maxVal = std::abs(M[1][2]); p=1; q=2; }
        if (maxVal < 1e-14) break;

        double theta = (M[q][q] - M[p][p]) / (2.0 * M[p][q]);
        double t     = (theta >= 0 ? 1.0 : -1.0)
                       / (std::abs(theta) + std::sqrt(1.0 + theta*theta));
        double c2 = 1.0 / std::sqrt(1.0 + t*t);
        double s2 = t * c2;

        M[p][p] -= t * M[p][q];
        M[q][q] += t * M[p][q];
        M[p][q] = M[q][p] = 0.0;

        for (int r = 0; r < 3; ++r) {
            if (r == p || r == q) continue;
            double Mrp = c2 * M[r][p] - s2 * M[r][q];
            double Mrq = s2 * M[r][p] + c2 * M[r][q];
            M[r][p] = M[p][r] = Mrp;
            M[r][q] = M[q][r] = Mrq;
        }
        for (int r = 0; r < 3; ++r) {
            double Vp = c2 * evecs[r][p] - s2 * evecs[r][q];
            double Vq = s2 * evecs[r][p] + c2 * evecs[r][q];
            evecs[r][p] = Vp;
            evecs[r][q] = Vq;
        }
    }

    // Sort ascending by eigenvalue
    int idx[3] = {0, 1, 2};
    double ev[3] = { M[0][0], M[1][1], M[2][2] };
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2-i; ++j)
            if (ev[idx[j]] > ev[idx[j+1]]) std::swap(idx[j], idx[j+1]);

    for (int i = 0; i < 3; ++i) evals[i] = ev[idx[i]];

    double tmp[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            tmp[i][j] = evecs[i][idx[j]];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            evecs[i][j] = tmp[i][j];
}

} // namespace vision
