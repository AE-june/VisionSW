#pragma once
#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <vector>

namespace vision {
namespace poly {

inline double eval3(const double c[4], double t) noexcept {
    return c[0] + t * (c[1] + t * (c[2] + t * c[3]));
}

// Robust degree-3 least-squares polynomial fit with iterative outlier trimming.
// ts: independent variable, zs: dependent variable.
// keep: in/out validity mask (true = include point in fit).
// coef[4]: output coefficients for c[0] + c[1]*t + c[2]*t^2 + c[3]*t^3.
// Returns false if fitting fails (too few points or degenerate system).
//
// Trimming: each iteration computes residuals on kept points, estimates robust
// sigma via MAD, then removes points outside ±3σ. Notch floor (~400µm below
// land surface) is excluded automatically since it far exceeds typical land σ.
inline bool robustPolyfit3(const std::vector<double>& ts,
                            const std::vector<double>& zs,
                            std::vector<bool>&          keep,
                            double                      coef[4],
                            int                         iters = 4) {
    const int N = (int)ts.size();
    for (int it = 0; it < iters; ++it) {
        int k = (int)std::count(keep.begin(), keep.end(), true);
        if (k < 7) return false;

        Eigen::MatrixXd A(k, 4);
        Eigen::VectorXd b(k);
        {
            int row = 0;
            for (int i = 0; i < N; ++i) {
                if (!keep[i]) continue;
                double t = ts[i];
                A(row, 0) = 1.0; A(row, 1) = t; A(row, 2) = t*t; A(row, 3) = t*t*t;
                b(row) = zs[i];
                ++row;
            }
        }
        const Eigen::VectorXd x = A.householderQr().solve(b);
        for (int c = 0; c < 4; ++c) coef[c] = x(c);

        // Compute absolute residuals for kept points
        std::vector<double> absRes;
        absRes.reserve((size_t)k);
        for (int i = 0; i < N; ++i) {
            if (!keep[i]) continue;
            absRes.push_back(std::abs(zs[i] - eval3(coef, ts[i])));
        }
        // MAD-based robust sigma = 1.4826 * median(|residual - median(residual)|)
        std::sort(absRes.begin(), absRes.end());
        double medAbs = absRes[absRes.size() / 2];
        std::vector<double> mad;
        mad.reserve(absRes.size());
        for (double r : absRes) mad.push_back(std::abs(r - medAbs));
        std::sort(mad.begin(), mad.end());
        double sigma = 1.4826 * mad[mad.size() / 2];
        if (sigma < 1e-9) break;

        // Symmetric ±3σ trim
        for (int i = 0; i < N; ++i) {
            if (!keep[i]) continue;
            double res = zs[i] - eval3(coef, ts[i]);
            keep[i] = (res > -3.0 * sigma) && (res < 3.0 * sigma);
        }
        if ((int)std::count(keep.begin(), keep.end(), true) < 7) return false;
    }
    return true;
}

} // namespace poly
} // namespace vision
