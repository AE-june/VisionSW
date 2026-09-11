#include "ProfileCaliperTool.h"
#include "ProfileFeatureTool.h"
#include "Logger.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <string>

namespace vision {

ProfileCaliperTool::ProfileCaliperTool(Params p)
    : m_params(std::move(p)) {}

ToolResult ProfileCaliperTool::execute(VisionDataPtr input) {
    if (!input)
        return {ToolStatus::Fail, "ProfileCaliper: 입력이 없습니다."};

    // Profile from port 0 (upstream ExtractProfile/CloudToProfiles)
    std::shared_ptr<Profile> profPtr;
    if (!input->profiles.empty()) {
        profPtr = input->profiles[0];
    } else if (!input->inputs.empty() && input->inputs[0] && !input->inputs[0]->profiles.empty()) {
        profPtr = input->inputs[0]->profiles[0];
    }
    if (!profPtr)
        return {ToolStatus::Fail, "ProfileCaliper: Profile(포트 0)이 없습니다."};

    const auto& prof = *profPtr;

    auto out = std::make_shared<VisionData>();
    out->sourceId = input->sourceId;
    out->frames   = input->frames;

    // 입력 프로파일 그대로 출력 → UI ProfileChart 시각화
    out->profiles = input->profiles.empty()
        ? input->inputs[0]->profiles
        : input->profiles;

    // 피처 결과 저장 (distances 계산용)
    struct FeatResult { double sMm = 0, zMm = 0; bool valid = false; };
    std::vector<FeatResult> featResults(m_params.features.size());

    // ProfileFeatureTool이 읽을 포트 0 VisionData
    auto profHolder = std::make_shared<VisionData>();
    profHolder->profiles = out->profiles;

    // ── 2. 피처 검출 (ProfileFeatureTool 재사용) ─────────────────────────
    for (std::size_t fi = 0; fi < m_params.features.size(); ++fi) {
        const auto& fd = m_params.features[fi];
        const std::string prefix = "feat[" + std::to_string(fi) + "].";

        ProfileFeatureParams fp;
        fp.kind            = fd.kind;
        fp.edgeDir         = fd.dir;
        fp.edgeThresholdMm = fd.threshold;
        fp.smoothWindow    = fd.smoothWindow;
        fp.searchFromMm    = fd.searchFromMm;
        fp.searchToMm      = fd.searchToMm;
        fp.nth             = fd.nth;

        auto synInput = std::make_shared<VisionData>();
        synInput->inputs.resize(1, profHolder);

        ProfileFeatureTool featureTool(fp);
        auto fr = featureTool.execute(synInput);

        if (fr.status != ToolStatus::Ok || !fr.output) {
            out->measurements.push_back({prefix + "sMm", 0, "mm", false});
            out->measurements.push_back({prefix + "zMm", 0, "mm", false});
            VISION_LOG_WARN("ProfileCaliper: feat[{}] 검출 실패 — {}", fi, fr.message);
            continue;
        }

        double sMm = 0, zMm = 0;
        bool hasS = false, hasZ = false;
        for (const auto& m : fr.output->measurements) {
            if (!m.valid) continue;
            if (m.name.size() >= 2 && m.name.substr(m.name.size() - 2) == "_s") { sMm = m.value; hasS = true; }
            if (m.name.size() >= 2 && m.name.substr(m.name.size() - 2) == "_z") { zMm = m.value; hasZ = true; }
        }

        out->measurements.push_back({prefix + "sMm", sMm, "mm", hasS});
        out->measurements.push_back({prefix + "zMm", zMm, "mm", hasZ});

        if (hasS) {
            featResults[fi] = {sMm, zMm, true};
            if (!fr.output->points.empty())
                out->points.push_back(fr.output->points[0]);
        }

        VISION_LOG_INFO("ProfileCaliper: feat[{}] kind={} s={:.4f}mm z={:.4f}mm",
            fi, fd.kind, sMm, zMm);
    }

    // ── 3. 라인피팅 (z = slope*s + intercept, 폐합 최소제곱) ────────────
    for (std::size_t li = 0; li < m_params.lineFits.size(); ++li) {
        const auto& ld = m_params.lineFits[li];
        const std::string prefix = "lineFit[" + std::to_string(li) + "].";
        const bool hasRange = (ld.toMm > ld.fromMm);

        // 범위 에코 (UI 오버레이용)
        out->measurements.push_back({prefix + "fromMm", ld.fromMm, "mm", true});
        out->measurements.push_back({prefix + "toMm",   ld.toMm,   "mm", true});

        double sumS = 0, sumZ = 0, sumSS = 0, sumSZ = 0;
        int n = 0;

        for (std::size_t i = 0; i < prof.size(); ++i) {
            if (!prof.valid(i)) continue;
            const double s = prof.s[i], z = prof.z[i];
            if (hasRange && (s < ld.fromMm || s > ld.toMm)) continue;
            sumS  += s; sumZ  += z;
            sumSS += s * s; sumSZ += s * z;
            ++n;
        }

        if (n < 2) {
            out->measurements.push_back({prefix + "slope",     0, "mm/mm", false});
            out->measurements.push_back({prefix + "intercept", 0, "mm",    false});
            out->measurements.push_back({prefix + "rmse",      0, "mm",    false});
            continue;
        }

        const double denom = (double)n * sumSS - sumS * sumS;
        if (std::abs(denom) < 1e-15) {
            out->measurements.push_back({prefix + "slope",     0, "mm/mm", false});
            out->measurements.push_back({prefix + "intercept", 0, "mm",    false});
            out->measurements.push_back({prefix + "rmse",      0, "mm",    false});
            continue;
        }

        const double slope     = ((double)n * sumSZ - sumS * sumZ) / denom;
        const double intercept = (sumZ - slope * sumS) / n;

        double sse = 0;
        for (std::size_t i = 0; i < prof.size(); ++i) {
            if (!prof.valid(i)) continue;
            const double s = prof.s[i], z = prof.z[i];
            if (hasRange && (s < ld.fromMm || s > ld.toMm)) continue;
            const double r = z - (slope * s + intercept);
            sse += r * r;
        }
        const double rmse = std::sqrt(sse / n);

        out->measurements.push_back({prefix + "slope",     slope,     "mm/mm", true});
        out->measurements.push_back({prefix + "intercept", intercept, "mm",    true});
        out->measurements.push_back({prefix + "rmse",      rmse,      "mm",    true});

        VISION_LOG_INFO("ProfileCaliper: lineFit[{}] n={} slope={:.6f} intercept={:.4f} rmse={:.6f}",
            li, n, slope, intercept, rmse);
    }

    // ── 4. 거리 측정 ─────────────────────────────────────────────────────
    bool allPass = true;
    for (std::size_t di = 0; di < m_params.distances.size(); ++di) {
        const auto& dd = m_params.distances[di];
        const std::string mname = "dist[" + std::to_string(di) + "]";

        const bool fi0ok = dd.from >= 0 && dd.from < (int)featResults.size() && featResults[dd.from].valid;
        const bool fi1ok = dd.to   >= 0 && dd.to   < (int)featResults.size() && featResults[dd.to  ].valid;

        if (!fi0ok || !fi1ok) {
            out->measurements.push_back({mname, 0, "mm", false});
            if (dd.nominalMm != 0 || dd.plusMm != 0 || dd.minusMm != 0) {
                out->decisions.push_back({mname, false, "피처 검출 실패", 0, dd.nominalMm,
                    (dd.plusMm + dd.minusMm) / 2});
                allPass = false;
            }
            continue;
        }

        const auto& r0 = featResults[dd.from];
        const auto& r1 = featResults[dd.to];

        double dist = 0;
        if (dd.mode == "deltaZ") {
            dist = std::abs(r1.zMm - r0.zMm);
        } else if (dd.mode == "euclidean") {
            const double ds = r1.sMm - r0.sMm;
            const double dz = r1.zMm - r0.zMm;
            dist = std::sqrt(ds * ds + dz * dz);
        } else {
            dist = std::abs(r1.sMm - r0.sMm);
        }

        out->measurements.push_back({mname, dist, "mm", true});

        if (dd.nominalMm != 0 || dd.plusMm != 0 || dd.minusMm != 0) {
            const double lo   = dd.nominalMm - dd.minusMm;
            const double hi   = dd.nominalMm + dd.plusMm;
            const bool   pass = (dist >= lo && dist <= hi);
            if (!pass) allPass = false;
            out->decisions.push_back({
                mname, pass,
                pass ? "OK" : ("측정값 " + std::to_string(dist) + "mm, 범위 "
                    + std::to_string(lo) + "~" + std::to_string(hi) + "mm"),
                dist, dd.nominalMm, (dd.plusMm + dd.minusMm) / 2
            });
        }

        VISION_LOG_INFO("ProfileCaliper: dist[{}] mode={} {:.4f}mm", di, dd.mode, dist);
    }

    if (!out->decisions.empty())
        out->decisions.push_back({"allPass", allPass, allPass ? "전체 합격" : "불합격 항목 있음", 0, 0, 0});

    return {ToolStatus::Ok, "", out};
}

} // namespace vision
