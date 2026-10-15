#include "Aggregate.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace vision::agg {

static std::vector<double> collectValid(const double* v, std::size_t n) {
    std::vector<double> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        if (!std::isnan(v[i])) out.push_back(v[i]);
    return out;
}

Result mean(const double* v, std::size_t n) {
    auto vals = collectValid(v, n);
    if (vals.empty()) return {};
    double sum = 0;
    for (double x : vals) sum += x;
    return { sum / static_cast<double>(vals.size()), true, vals.size() };
}

Result median(const double* v, std::size_t n) {
    auto vals = collectValid(v, n);
    if (vals.empty()) return {};
    const std::size_t N = vals.size();
    const std::size_t mid = N / 2;
    std::nth_element(vals.begin(), vals.begin() + mid, vals.end());
    double med = vals[mid];
    if (N % 2 == 0) {
        // 짝수: 상위 절반 최솟값(lower median 직전) + mid 평균
        double lo = *std::max_element(vals.begin(), vals.begin() + mid);
        med = (lo + med) / 2.0;
    }
    return { med, true, N };
}

Result maxV(const double* v, std::size_t n) {
    auto vals = collectValid(v, n);
    if (vals.empty()) return {};
    return { *std::max_element(vals.begin(), vals.end()), true, vals.size() };
}

Result minV(const double* v, std::size_t n) {
    auto vals = collectValid(v, n);
    if (vals.empty()) return {};
    return { *std::min_element(vals.begin(), vals.end()), true, vals.size() };
}

Result stdDev(const double* v, std::size_t n) {
    auto vals = collectValid(v, n);
    if (vals.empty()) return {};
    if (vals.size() == 1) return { 0.0, true, 1 };
    double sum = 0;
    for (double x : vals) sum += x;
    double m = sum / static_cast<double>(vals.size());
    double sse = 0;
    for (double x : vals) { double d = x - m; sse += d * d; }
    return { std::sqrt(sse / static_cast<double>(vals.size() - 1)), true, vals.size() };
}

Result percentile(const double* v, std::size_t n, double p) {
    auto vals = collectValid(v, n);
    if (vals.empty()) return {};
    const std::size_t N = vals.size();
    const double q = std::clamp(p, 0.0, 100.0) / 100.0;
    const std::size_t idx = static_cast<std::size_t>(
        std::clamp(static_cast<long long>(std::llround(q * static_cast<double>(N - 1))),
                   0LL, static_cast<long long>(N - 1)));
    std::nth_element(vals.begin(), vals.begin() + idx, vals.end());
    return { vals[idx], true, N };
}

// ⚠️ 구 HeightFromPlaneTool::aggregate(HighTail) 알고리즘 원형 그대로 이관.
// n_top = max(1, ceil(N * pct/100)) — 반올림 방식·경계 포함, 개선 금지.
Result highTail(const double* v, std::size_t n, double pct) {
    auto vals = collectValid(v, n);
    if (vals.empty()) return {};
    const std::size_t N = vals.size();
    const int n_top = std::max(1, static_cast<int>(
        std::ceil(static_cast<double>(N) * pct / 100.0)));
    const int start = static_cast<int>(N) - n_top;
    std::nth_element(vals.begin(), vals.begin() + start, vals.end());
    double sum = 0;
    for (int i = start; i < static_cast<int>(N); ++i) sum += vals[i];
    return { sum / static_cast<double>(n_top), true, static_cast<std::size_t>(n_top) };
}

} // namespace vision::agg
