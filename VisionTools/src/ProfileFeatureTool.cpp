#include "ProfileFeatureTool.h"
#include "Aggregate.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace vision {

ProfileFeatureTool::ProfileFeatureTool(ProfileFeatureParams params)
    : m_params(std::move(params)) {}

// ─────────────────────────────────────────────────────────────────────
//  nth 정규화: 범위 밖이면 -1 반환
// ─────────────────────────────────────────────────────────────────────
static int normalizeNth(int nth, int total) {
    if (total <= 0) return -1;
    const int k = nth >= 0 ? nth : total + nth;
    return (k >= 0 && k < total) ? k : -1;
}

// ── Phase 6: 검출 계열 헬퍼 ──────────────────────────────────────────

struct P6Candidate {
    double sMm;        // 서브픽셀 s 위치 (mm)
    double zMm;        // 격자점 z (mm)
    double xMm, yMm;  // 격자점 x, y (mm)
    double strength;   // 검출 강도
};

// NaN-aware 이동평균. z[i]가 NaN이면 출력도 NaN 유지.
static std::vector<double> smoothZNaN(const std::vector<double>& z, int window) {
    if (window <= 1) return z;
    const int N    = static_cast<int>(z.size());
    const int half = window / 2;
    std::vector<double> out(z);
    for (int i = 0; i < N; ++i) {
        if (std::isnan(z[i])) continue;  // NaN 보존
        double sum = 0.0; int cnt = 0;
        for (int j = std::max(0, i - half); j <= std::min(N - 1, i + half); ++j) {
            if (!std::isnan(z[j])) { sum += z[j]; ++cnt; }
        }
        if (cnt > 0) out[i] = sum / cnt;
    }
    return out;
}

// 후진 차분: dz[i] = z[i] - z[i-1], dz[0]=NaN. NaN 전파.
static std::vector<double> backDiff(const std::vector<double>& z) {
    const int N = static_cast<int>(z.size());
    std::vector<double> dz(N, std::numeric_limits<double>::quiet_NaN());
    for (int i = 1; i < N; ++i)
        dz[i] = z[i] - z[i - 1];
    return dz;
}

// 인접 3점 포물선 극점 보간 (균등 간격 h 가정, s0 중심).
// 보간 범위 벗어나지 않도록 shift를 [-h, h]로 clamp.
static double subpixelExtremum(double s0,
                                double fm, double f0, double fp,
                                double h) {
    const double denom = fm + fp - 2.0 * f0;
    if (std::abs(denom) < 1e-15) return s0;
    double shift = -(fp - fm) * h / (2.0 * denom);
    if (shift >  h) shift =  h;
    if (shift < -h) shift = -h;
    return s0 + shift;
}

// ── edge 검출 ────────────────────────────────────────────────────────
// dz[i] = z_smooth[i] - z_smooth[i-1]. NaN이면 dz도 NaN → 허위 엣지 없음.
static std::vector<P6Candidate> detectEdges(
    const Profile& prof,
    const std::string& dir, double threshold, int smoothWindow)
{
    const int N = static_cast<int>(prof.size());
    auto zs = smoothZNaN(prof.z, smoothWindow);
    auto dz = backDiff(zs);

    std::vector<P6Candidate> cands;
    for (int i = 1; i < N - 1; ++i) {
        if (std::isnan(dz[i]) || std::isnan(dz[i - 1]) || std::isnan(dz[i + 1])) continue;

        const double val    = dz[i];
        const double absVal = std::abs(val);

        bool meetsDir = false, isLocalExtr = false;
        if (dir == "rising") {
            meetsDir    = val > threshold;
            isLocalExtr = val > dz[i - 1] && val > dz[i + 1];
        } else if (dir == "falling") {
            meetsDir    = val < -threshold;
            isLocalExtr = val < dz[i - 1] && val < dz[i + 1];
        } else {  // "any"
            meetsDir    = absVal > threshold;
            isLocalExtr = absVal > std::abs(dz[i - 1]) && absVal > std::abs(dz[i + 1]);
        }
        if (!meetsDir || !isLocalExtr) continue;

        const double h   = (prof.s[i + 1] - prof.s[i - 1]) / 2.0;
        const double sMm = subpixelExtremum(prof.s[i], dz[i - 1], dz[i], dz[i + 1], h);
        cands.push_back({sMm, prof.z[i], prof.x[i], prof.y[i], absVal});
    }
    return cands;
}

// ── ridge / valley 검출 ──────────────────────────────────────────────
static std::vector<P6Candidate> detectRidgesValleys(
    const Profile& prof, bool isRidge,
    double threshold, int smoothWindow)
{
    const int N = static_cast<int>(prof.size());
    auto zs = smoothZNaN(prof.z, smoothWindow);

    std::vector<P6Candidate> cands;
    for (int i = 1; i < N - 1; ++i) {
        if (std::isnan(zs[i]) || std::isnan(zs[i - 1]) || std::isnan(zs[i + 1])) continue;

        const bool isExtr = isRidge
            ? (zs[i] > zs[i - 1] && zs[i] > zs[i + 1])
            : (zs[i] < zs[i - 1] && zs[i] < zs[i + 1]);
        if (!isExtr) continue;

        const double strength = isRidge
            ? std::min(zs[i] - zs[i - 1], zs[i] - zs[i + 1])
            : std::min(zs[i - 1] - zs[i], zs[i + 1] - zs[i]);
        if (strength < threshold) continue;

        const double h   = (prof.s[i + 1] - prof.s[i - 1]) / 2.0;
        const double sMm = subpixelExtremum(prof.s[i], zs[i - 1], zs[i], zs[i + 1], h);
        cands.push_back({sMm, prof.z[i], prof.x[i], prof.y[i], strength});
    }
    return cands;
}

// ── corner 검출 (2차 차분 극값) ──────────────────────────────────────
static std::vector<P6Candidate> detectCorners(
    const Profile& prof, double threshold, int smoothWindow)
{
    const int N = static_cast<int>(prof.size());
    auto zs = smoothZNaN(prof.z, smoothWindow);

    std::vector<double> d2z(N, std::numeric_limits<double>::quiet_NaN());
    for (int i = 1; i < N - 1; ++i) {
        if (!std::isnan(zs[i - 1]) && !std::isnan(zs[i]) && !std::isnan(zs[i + 1]))
            d2z[i] = zs[i + 1] - 2.0 * zs[i] + zs[i - 1];
    }

    std::vector<P6Candidate> cands;
    for (int i = 2; i < N - 2; ++i) {
        if (std::isnan(d2z[i]) || std::isnan(d2z[i - 1]) || std::isnan(d2z[i + 1])) continue;

        const double absVal = std::abs(d2z[i]);
        if (absVal <= threshold) continue;
        if (absVal <= std::abs(d2z[i - 1]) || absVal <= std::abs(d2z[i + 1])) continue;

        const double h   = (prof.s[i + 1] - prof.s[i - 1]) / 2.0;
        const double sMm = subpixelExtremum(prof.s[i], d2z[i - 1], d2z[i], d2z[i + 1], h);
        cands.push_back({sMm, prof.z[i], prof.x[i], prof.y[i], absVal});
    }
    return cands;
}

// ── execute ──────────────────────────────────────────────────────────

ToolResult ProfileFeatureTool::execute(VisionDataPtr input) {
    if (!input) return {ToolStatus::Fail, "ProfileFeature: 입력이 없습니다."};
    const auto& profs = input->inProfiles(0);
    if (profs.empty())
        return {ToolStatus::Fail, "ProfileFeature: Profile(포트 0)이 없습니다."};

    auto out = std::make_shared<VisionData>();
    out->sourceId = input->sourceId;
    out->frames   = input->frames;

    // 여러 행 Profile → 순회 분석 (RegionMeasure 패턴). 이름에 label 프리픽스.
    const bool multi = profs.size() > 1;
    int okCount = 0;
    std::string lastErr;
    for (std::size_t pi = 0; pi < profs.size(); ++pi) {
        if (!profs[pi] || profs[pi]->empty()) continue;
        std::string prefix;
        if (multi) {
            const std::string& lbl = profs[pi]->label;
            prefix = (lbl.empty() ? std::to_string(pi) : lbl) + ".";
        }
        ToolResult r = analyzeOne(*profs[pi]);
        if (r.status != ToolStatus::Ok || !r.output) { lastErr = r.message; continue; }
        ++okCount;
        for (auto& m : r.output->measurements)
            out->measurements.push_back({ prefix + m.name, m.value, m.unit, m.valid });
        for (auto& pt : r.output->points)
            out->points.push_back(pt);
    }
    if (okCount == 0)
        return {ToolStatus::Fail, lastErr.empty() ? "ProfileFeature: 분석 실패" : lastErr};

    VISION_LOG_INFO("ProfileFeature: kind={} profiles={} → {} measurements",
        m_params.kind, profs.size(), out->measurements.size());
    return {ToolStatus::Ok, "", out};
}

// 단일 Profile 분석 — 원본 로직. prefix 없이 측정값/점 생성(execute가 병합 시 프리픽스).
ToolResult ProfileFeatureTool::analyzeOne(const Profile& prof) const {
    if (prof.empty())
        return {ToolStatus::Fail, "ProfileFeature: Profile이 비어있습니다."};

    const bool hasRange =
        (m_params.searchFromMm != 0 || m_params.searchToMm != 0) &&
        (m_params.searchToMm > m_params.searchFromMm);

    const std::string& kind = m_params.kind;

    auto out = std::make_shared<VisionData>();

    auto addMeas = [&](const std::string& name, double value,
                       const std::string& unit = "mm") {
        out->measurements.push_back({name, value, unit, true});
    };

    // ── Phase 6 검출 계열 ─────────────────────────────────────────────
    if (kind == "edge" || kind == "ridge" || kind == "valley" || kind == "corner") {
        std::vector<P6Candidate> cands;
        if (kind == "edge")
            cands = detectEdges(prof, m_params.edgeDir,
                                m_params.edgeThresholdMm, m_params.smoothWindow);
        else if (kind == "ridge")
            cands = detectRidgesValleys(prof, /*isRidge=*/true,
                                        m_params.edgeThresholdMm, m_params.smoothWindow);
        else if (kind == "valley")
            cands = detectRidgesValleys(prof, /*isRidge=*/false,
                                        m_params.edgeThresholdMm, m_params.smoothWindow);
        else
            cands = detectCorners(prof, m_params.edgeThresholdMm, m_params.smoothWindow);

        if (hasRange) {
            cands.erase(
                std::remove_if(cands.begin(), cands.end(),
                    [&](const P6Candidate& c) {
                        return c.sMm < m_params.searchFromMm ||
                               c.sMm > m_params.searchToMm;
                    }),
                cands.end());
        }

        if (cands.empty())
            return {ToolStatus::Fail, "ProfileFeature: " + kind + " 검출 없음."};

        std::sort(cands.begin(), cands.end(),
                  [](const P6Candidate& a, const P6Candidate& b) {
                      return a.sMm < b.sMm;
                  });

        const int k = normalizeNth(m_params.nth, static_cast<int>(cands.size()));
        if (k < 0)
            return {ToolStatus::Fail, "ProfileFeature: " + kind + " nth 범위 초과."};

        const auto& c = cands[k];
        addMeas(kind + "_s", c.sMm);
        addMeas(kind + "_z", c.zMm);
        RefPoint rp;
        rp.cxMm  = c.xMm;
        rp.cyMm  = c.yMm;
        rp.valid = true;
        out->points.push_back(rp);

        VISION_LOG_INFO("ProfileFeature: kind={} n={} → s={:.4f}mm z={:.4f}mm",
            kind, static_cast<int>(cands.size()), c.sMm, c.zMm);
        return {ToolStatus::Ok, "", out};
    }

    // ── Phase 5 집계 계열 ─────────────────────────────────────────────
    std::vector<double>      zVals;
    std::vector<std::size_t> profIdx;

    for (std::size_t i = 0; i < prof.size(); ++i) {
        if (!prof.valid(i)) continue;
        if (hasRange) {
            if (prof.s[i] < m_params.searchFromMm) continue;
            if (prof.s[i] > m_params.searchToMm)   continue;
        }
        zVals.push_back(prof.z[i]);
        profIdx.push_back(i);
    }

    if (zVals.empty())
        return {ToolStatus::Fail,
            "ProfileFeature: 유효 샘플이 없습니다 (kind=" + kind + ")."};

    const int N = static_cast<int>(zVals.size());

    auto addPoint = [&](std::size_t pi) {
        RefPoint rp;
        rp.cxMm  = prof.x[pi];
        rp.cyMm  = prof.y[pi];
        rp.valid = true;
        out->points.push_back(rp);
    };

    using namespace vision::agg;

    if (kind == "maxZ") {
        std::vector<int> order(N);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(),
                  [&](int a, int b) { return zVals[a] > zVals[b]; });
        const int k = normalizeNth(m_params.nth, N);
        if (k < 0) return {ToolStatus::Fail, "ProfileFeature: maxZ nth 범위 초과."};
        const std::size_t pi = profIdx[order[k]];
        addMeas("maxZ",   prof.z[pi]);
        addMeas("maxZ_s", prof.s[pi]);
        addPoint(pi);

    } else if (kind == "minZ") {
        std::vector<int> order(N);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(),
                  [&](int a, int b) { return zVals[a] < zVals[b]; });
        const int k = normalizeNth(m_params.nth, N);
        if (k < 0) return {ToolStatus::Fail, "ProfileFeature: minZ nth 범위 초과."};
        const std::size_t pi = profIdx[order[k]];
        addMeas("minZ",   prof.z[pi]);
        addMeas("minZ_s", prof.s[pi]);
        addPoint(pi);

    } else if (kind == "maxS") {
        const int nth = m_params.nth >= 0 ? m_params.nth : (N + m_params.nth);
        const int k   = normalizeNth(N - 1 - nth, N);
        if (k < 0) return {ToolStatus::Fail, "ProfileFeature: maxS nth 범위 초과."};
        const std::size_t pi = profIdx[k];
        addMeas("maxS",   prof.s[pi]);
        addMeas("maxS_z", prof.z[pi]);
        addPoint(pi);

    } else if (kind == "minS") {
        const int k = normalizeNth(m_params.nth, N);
        if (k < 0) return {ToolStatus::Fail, "ProfileFeature: minS nth 범위 초과."};
        const std::size_t pi = profIdx[k];
        addMeas("minS",   prof.s[pi]);
        addMeas("minS_z", prof.z[pi]);
        addPoint(pi);

    } else if (kind == "mean") {
        auto r = mean(zVals.data(), N);
        if (!r.valid) return {ToolStatus::Fail, "ProfileFeature: mean 계산 실패."};
        addMeas("mean", r.value);

    } else if (kind == "median") {
        auto r = median(zVals.data(), N);
        if (!r.valid) return {ToolStatus::Fail, "ProfileFeature: median 계산 실패."};
        addMeas("median", r.value);

    } else if (kind == "stdDev") {
        auto r = stdDev(zVals.data(), N);
        if (!r.valid) return {ToolStatus::Fail, "ProfileFeature: stdDev 계산 실패."};
        addMeas("stdDev", r.value);

    } else if (kind == "percentile") {
        auto r = percentile(zVals.data(), N, m_params.percentile);
        if (!r.valid) return {ToolStatus::Fail, "ProfileFeature: percentile 계산 실패."};
        addMeas("percentile", r.value);

    } else if (kind == "highTail") {
        auto r = highTail(zVals.data(), N, m_params.percentile);
        if (!r.valid) return {ToolStatus::Fail, "ProfileFeature: highTail 계산 실패."};
        addMeas("highTail", r.value);

    } else {
        return {ToolStatus::Fail, "ProfileFeature: 알 수 없는 kind=" + kind};
    }

    VISION_LOG_INFO("ProfileFeature: kind={} n={} → {} measurements",
        kind, N, out->measurements.size());
    return {ToolStatus::Ok, "", out};
}

} // namespace vision
