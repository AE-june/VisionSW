#include "ProfileCaliperTool.h"
#include "ProfileFeatureTool.h"
#include "CaliperResultCache.h"
#include "Logger.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace vision {

ProfileCaliperTool::ProfileCaliperTool(Params p)
    : m_params(std::move(p)) {}

ToolResult ProfileCaliperTool::execute(VisionDataPtr input) {
    if (!input)
        return {ToolStatus::Fail, "ProfileCaliper: 입력이 없습니다."};

    // Profile from port 0 (upstream ExtractProfile/CloudToProfiles)
    // ExtractProfile은 여러 타일을 출력하므로 profileIndex로 분석 대상 선택
    const std::vector<std::shared_ptr<Profile>>* profVec = nullptr;
    if (!input->profiles.empty()) {
        profVec = &input->profiles;
    } else if (!input->inputs.empty() && input->inputs[0] && !input->inputs[0]->profiles.empty()) {
        profVec = &input->inputs[0]->profiles;
    }
    if (!profVec || profVec->empty())
        return {ToolStatus::Fail, "ProfileCaliper: Profile(포트 0)이 없습니다."};

    int pidx = m_params.profileIndex;
    if (pidx < 0) pidx = 0;
    if (pidx >= (int)profVec->size()) pidx = (int)profVec->size() - 1;
    std::shared_ptr<Profile> profPtr = (*profVec)[pidx];
    if (!profPtr)
        return {ToolStatus::Fail, "ProfileCaliper: 선택한 Profile이 비어있습니다."};

    auto out = std::make_shared<VisionData>();
    out->sourceId = input->sourceId;
    out->frames   = input->frames;

    // 입력 프로파일 그대로 출력 → UI ProfileChart 시각화
    out->profiles = input->profiles.empty()
        ? input->inputs[0]->profiles
        : input->profiles;

    // element 결과 저장 (measurements 계산용)
    struct ElemResult { double sMm=0, zMm=0, slope=0, intercept=0, rmse=0; bool isPoint=true; bool valid=false; };

    // ── 한 프로파일에 대한 elements + measurements 분석 ───────────────────
    // pfx=="" (선택 프로파일): plain 이름 + elem[i].* 에코 + 포인트 방출 (emitPoints)
    // pfx=="prof[j]." (전체 프로파일): pfx+meas[i] 만 방출, elem 에코 생략
    auto analyzeOne = [&](const Profile& profRef, const std::string& pfx, bool emitPoints,
                          CaliperProfileResult* cacheOut = nullptr) -> bool {
        const bool full = pfx.empty();
        std::shared_ptr<Profile> basePtr = std::make_shared<Profile>(profRef);

        std::vector<ElemResult> elemResults(m_params.elements.size());
        if (cacheOut) {
            cacheOut->elems.assign(m_params.elements.size(), CaliperElemView{});
            cacheOut->meas.assign(m_params.measurements.size(), CaliperMeasView{});
        }

        // ── 2. element 처리 (point 추출 또는 line 피팅) ──────────────────────
        for (std::size_t i = 0; i < m_params.elements.size(); ++i) {
            const auto& ed = m_params.elements[i];
            const std::string prefix = pfx + "elem[" + std::to_string(i) + "].";

            // z-range 마스킹: zToMm > zFromMm 이면 범위 밖 샘플을 NaN 처리한 사본 사용
            const bool hasZRange = (ed.zToMm > ed.zFromMm);
            std::shared_ptr<Profile> workPtr = basePtr;
            if (hasZRange) {
                auto masked = std::make_shared<Profile>(*basePtr);
                for (std::size_t k = 0; k < masked->z.size(); ++k) {
                    const double zk = masked->z[k];
                    if (std::isnan(zk) || zk < ed.zFromMm || zk > ed.zToMm)
                        masked->z[k] = std::numeric_limits<double>::quiet_NaN();
                }
                workPtr = masked;
            }
            const Profile& prof = *workPtr;

            // ProfileFeatureTool이 읽을 포트 0 VisionData — (마스킹된) 선택 프로파일만
            auto profHolder = std::make_shared<VisionData>();
            profHolder->profiles = { workPtr };

            if (ed.type == "line") {
                const bool hasRange = (ed.toMm > ed.fromMm);

                // 범위 에코 (UI 오버레이용) — 선택 프로파일만 (payload 비대 방지)
                if (full) {
                    out->measurements.push_back({prefix + "fromMm", ed.fromMm, "mm", true});
                    out->measurements.push_back({prefix + "toMm",   ed.toMm,   "mm", true});
                }

                double sumS = 0, sumZ = 0, sumSS = 0, sumSZ = 0;
                int n = 0;

                for (std::size_t k = 0; k < prof.size(); ++k) {
                    if (!prof.valid(k)) continue;
                    const double s = prof.s[k], z = prof.z[k];
                    if (hasRange && (s < ed.fromMm || s > ed.toMm)) continue;
                    sumS  += s; sumZ  += z;
                    sumSS += s * s; sumSZ += s * z;
                    ++n;
                }

                const double denom = (double)n * sumSS - sumS * sumS;
                if (n < 2 || std::abs(denom) < 1e-15) {
                    if (full) {
                        out->measurements.push_back({prefix + "slope",     0, "mm/mm", false});
                        out->measurements.push_back({prefix + "intercept", 0, "mm",    false});
                        out->measurements.push_back({prefix + "rmse",      0, "mm",    false});
                    }
                    elemResults[i] = {0, 0, 0, 0, 0, false, false};
                    if (full)
                        VISION_LOG_WARN("ProfileCaliper: elem[{}] line 피팅 실패 (n={})", i, n);
                    continue;
                }

                const double slope     = ((double)n * sumSZ - sumS * sumZ) / denom;
                const double intercept = (sumZ - slope * sumS) / n;

                double sse = 0;
                for (std::size_t k = 0; k < prof.size(); ++k) {
                    if (!prof.valid(k)) continue;
                    const double s = prof.s[k], z = prof.z[k];
                    if (hasRange && (s < ed.fromMm || s > ed.toMm)) continue;
                    const double r = z - (slope * s + intercept);
                    sse += r * r;
                }
                const double rmse = std::sqrt(sse / n);

                if (full) {
                    out->measurements.push_back({prefix + "slope",     slope,     "mm/mm", true});
                    out->measurements.push_back({prefix + "intercept", intercept, "mm",    true});
                    out->measurements.push_back({prefix + "rmse",      rmse,      "mm",    true});
                }

                elemResults[i] = {0, 0, slope, intercept, rmse, false, true};

                if (full)
                    VISION_LOG_INFO("ProfileCaliper: elem[{}] line n={} slope={:.6f} intercept={:.4f} rmse={:.6f}",
                        i, n, slope, intercept, rmse);
            } else {
                // type == "point" (ProfileFeatureTool 위임)
                ProfileFeatureParams fp;
                fp.kind            = ed.kind;
                fp.edgeDir         = ed.dir;
                fp.edgeThresholdMm = ed.threshold;
                fp.smoothWindow    = ed.smoothWindow;
                fp.searchFromMm    = ed.fromMm;
                fp.searchToMm      = ed.toMm;
                fp.nth             = ed.nth;

                auto synInput = std::make_shared<VisionData>();
                synInput->inputs.resize(1, profHolder);

                ProfileFeatureTool featureTool(fp);
                auto fr = featureTool.execute(synInput);

                if (fr.status != ToolStatus::Ok || !fr.output) {
                    if (full) {
                        out->measurements.push_back({prefix + "sMm", 0, "mm", false});
                        out->measurements.push_back({prefix + "zMm", 0, "mm", false});
                    }
                    elemResults[i] = {0, 0, 0, 0, 0, true, false};
                    if (full)
                        VISION_LOG_WARN("ProfileCaliper: elem[{}] point 검출 실패 — {}", i, fr.message);
                    continue;
                }

                double sMm = 0, zMm = 0;
                bool hasS = false, hasZ = false;
                for (const auto& m : fr.output->measurements) {
                    if (!m.valid) continue;
                    const auto endsWith = [&](const char* suf) {
                        const std::string s(suf);
                        return m.name.size() >= s.size() && m.name.compare(m.name.size() - s.size(), s.size(), s) == 0;
                    };
                    if (endsWith("_s")) { sMm = m.value; hasS = true; }
                    else if (endsWith("_z")) { zMm = m.value; hasZ = true; }
                    // aggregation kind: 값 키가 kind 이름 그대로 (maxZ/minZ/mean=높이, maxS/minS=위치)
                    else if (m.name == "maxZ" || m.name == "minZ" || m.name == "mean" ||
                             m.name == "median" || m.name == "stdDev" || m.name == "percentile" ||
                             m.name == "highTail") { zMm = m.value; hasZ = true; }
                    else if (m.name == "maxS" || m.name == "minS") { sMm = m.value; hasS = true; }
                }

                if (full) {
                    out->measurements.push_back({prefix + "sMm", sMm, "mm", hasS});
                    out->measurements.push_back({prefix + "zMm", zMm, "mm", hasZ});
                }

                elemResults[i] = {sMm, zMm, 0, 0, 0, true, hasS};

                if (emitPoints && hasS && !fr.output->points.empty())
                    out->points.push_back(fr.output->points[0]);

                if (full)
                    VISION_LOG_INFO("ProfileCaliper: elem[{}] point kind={} s={:.4f}mm z={:.4f}mm",
                        i, ed.kind, sMm, zMm);
            }
        }

        // ── 캐시용 element view 채우기 (온디맨드 오버레이) ────────────────────
        if (cacheOut) {
            for (std::size_t i = 0; i < m_params.elements.size(); ++i) {
                const auto& er = elemResults[i];
                CaliperElemView v;
                v.type      = m_params.elements[i].type;   // "point" | "line"
                v.valid     = er.valid;
                v.sMm       = er.sMm;
                v.zMm       = er.zMm;
                v.slope     = er.slope;
                v.intercept = er.intercept;
                v.rmse      = er.rmse;
                v.fromMm    = m_params.elements[i].fromMm;
                v.toMm      = m_params.elements[i].toMm;
                cacheOut->elems[i] = v;
            }
        }

        // ── 3. measurement 조합 측정 ─────────────────────────────────────────
        bool allPass = true;
        for (std::size_t i = 0; i < m_params.measurements.size(); ++i) {
            const auto& md = m_params.measurements[i];
            const std::string mname = pfx + "meas[" + std::to_string(i) + "]";
            const bool needsTwo = (md.combo == "pp" || md.combo == "pl" || md.combo == "ll");

            auto refOk = [&](int idx) {
                return idx >= 0 && idx < (int)elemResults.size() && elemResults[idx].valid;
            };
            const bool aOk = refOk(md.refA);
            const bool bOk = !needsTwo || refOk(md.refB);

            if (!aOk || !bOk) {
                out->measurements.push_back({mname, 0, "mm", false});
                const bool hasTol = (md.nominalMm != 0 || md.plusMm != 0 || md.minusMm != 0);
                if (hasTol) {
                    out->decisions.push_back({mname, false, "element 검출 실패", 0, md.nominalMm,
                        (md.plusMm + md.minusMm) / 2});
                    allPass = false;
                }
                if (cacheOut) {
                    CaliperMeasView mv;
                    mv.value = 0; mv.unit = "mm";
                    mv.hasDecision = hasTol; mv.pass = false;
                    cacheOut->meas[i] = mv;
                }
                if (full)
                    VISION_LOG_WARN("ProfileCaliper: meas[{}] combo={} 참조 실패", i, md.combo);
                continue;
            }

            double value = 0;
            std::string unit = "mm";

            if (md.combo == "pp") {
                const auto& a = elemResults[md.refA];
                const auto& b = elemResults[md.refB];
                const double ds = b.sMm - a.sMm;
                const double dz = b.zMm - a.zMm;
                if (md.metric == "deltaS")      value = std::abs(ds);
                else if (md.metric == "deltaZ") value = std::abs(dz);
                else                            value = std::sqrt(ds * ds + dz * dz); // euclidean
            } else if (md.combo == "pl") {
                const auto& p = elemResults[md.refA]; // point
                const auto& l = elemResults[md.refB]; // line
                if (md.metric == "zDist") {
                    // z축(세로) 거리: 같은 s에서 점과 라인의 z 차
                    value = std::abs(p.zMm - (l.slope * p.sMm + l.intercept));
                } else { // perpDist: 수직(최단) 거리
                    value = std::abs(l.slope * p.sMm - p.zMm + l.intercept) / std::sqrt(l.slope * l.slope + 1);
                }
            } else if (md.combo == "ll") {
                const auto& l1 = elemResults[md.refA];
                const auto& l2 = elemResults[md.refB];
                if (md.metric == "offset") {
                    value = std::abs(l2.intercept - l1.intercept) / std::sqrt(l1.slope * l1.slope + 1);
                } else if (md.metric == "intersectS") {
                    const double dm = l1.slope - l2.slope;
                    value = (std::abs(dm) < 1e-12) ? 0.0 : (l2.intercept - l1.intercept) / dm;
                    unit  = "mm";
                } else if (md.metric == "intersectZ") {
                    const double dm = l1.slope - l2.slope;
                    const double s_int = (std::abs(dm) < 1e-12) ? 0.0 : (l2.intercept - l1.intercept) / dm;
                    value = l1.slope * s_int + l1.intercept;
                    unit  = "mm";
                } else { // angle
                    value = std::abs(std::atan(l1.slope) - std::atan(l2.slope)) * 180.0 / M_PI;
                    unit  = "deg";
                }
            } else if (md.combo == "l") {
                const auto& l = elemResults[md.refA];
                if (md.metric == "flatness") {
                    value = l.rmse;
                } else { // tilt
                    value = std::atan(l.slope) * 180.0 / M_PI;
                    unit  = "deg";
                }
            } else if (md.combo == "p") {
                const auto& p = elemResults[md.refA];
                if (md.metric == "absZ") value = p.zMm;
                else                     value = p.sMm; // absS
            }

            out->measurements.push_back({mname, value, unit, true});

            bool hasDecision = false, measPass = false;
            if (md.nominalMm != 0 || md.plusMm != 0 || md.minusMm != 0) {
                const double lo   = md.nominalMm - md.minusMm;
                const double hi   = md.nominalMm + md.plusMm;
                const bool   pass = (value >= lo && value <= hi);
                hasDecision = true; measPass = pass;
                if (!pass) allPass = false;
                out->decisions.push_back({
                    mname, pass,
                    pass ? "OK" : ("측정값 " + std::to_string(value) + unit + ", 범위 "
                        + std::to_string(lo) + "~" + std::to_string(hi) + unit),
                    value, md.nominalMm, (md.plusMm + md.minusMm) / 2
                });
            }

            if (cacheOut) {
                CaliperMeasView mv;
                mv.value = value; mv.unit = unit;
                mv.hasDecision = hasDecision; mv.pass = measPass;
                cacheOut->meas[i] = mv;
            }

            if (full)
                VISION_LOG_INFO("ProfileCaliper: meas[{}] combo={} metric={} {:.4f}{}",
                    i, md.combo, md.metric, value, unit);
        }

        return allPass;
    };

    // ── 전체 프로파일 분석 (bulk, prefix 이름) ─────────────────────────────
    //  각 프로파일의 compact 결과를 CaliperResultCache에 저장 → 슬라이더 이동 시
    //  fetchProfile 한 번으로 해당 프로파일의 element/measurement 오버레이를 받아온다.
    bool overallPass = true;
    std::vector<CaliperProfileResult> allProfileResults(profVec->size());
    for (std::size_t j = 0; j < profVec->size(); ++j) {
        if (!(*profVec)[j]) continue;
        const std::string jpfx = "prof[" + std::to_string(j) + "].";
        bool pj = analyzeOne(*(*profVec)[j], jpfx, /*emitPoints=*/false, &allProfileResults[j]);
        overallPass = overallPass && pj;
        out->decisions.push_back({jpfx + "allPass", pj, pj ? "합격" : "불합격", 0, 0, 0});
    }
    CaliperResultCache::instance().set(m_params.nodeId, std::move(allProfileResults));

    // ── 선택 프로파일 분석 (plain 이름 + 포인트 방출, UI 표시용) ────────────
    analyzeOne(*(*profVec)[pidx], "", /*emitPoints=*/true);

    // 전체 합/불 판정 (모든 프로파일 반영)
    out->decisions.push_back({"allPass", overallPass, overallPass ? "전체 합격" : "불합격 항목 있음", 0, 0, 0});

    return {ToolStatus::Ok, "", out};
}

} // namespace vision
