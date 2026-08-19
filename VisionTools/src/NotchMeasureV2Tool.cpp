#include "NotchMeasureV2Tool.h"
#include "Logger.h"
#include "Polyfit.h"
#include "VisionData.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace vision {
namespace {

static const double NAN_D = std::numeric_limits<double>::quiet_NaN();

// 한 chunk의 y(lateral) binning envelope 샘플 — 컬럼당 최대 z(윗면)
struct EnvPt {
    double y;   // mm (lateral)
    double z;   // mm
};

// 한 chunk(avgProfiles개 profile 머지) 처리 결과 — V1 ChunkResult와 동일 필드 + V2 확장(combinedLandZ)
struct ChunkResult {
    bool   hadFit       = false;   // 피팅+notch 검출 성공(바닥 검출 실패해도 true일 수 있음 — 안정화 대상)
    bool   valid        = false;   // 바닥까지 성공(또는 안정화로 보정)
    double coef[4]      = { 0, 0, 0, 0 };
    double notchLoY     = 0.0;
    double notchHiY     = 0.0;
    double floorCenterY = 0.0;
    double floorZRel    = 0.0;     // µm, 피팅 곡선 대비 상대값
    double floorWidth   = 0.0;     // µm
    double leftLandZ    = NAN_D;   // mm, 절대높이
    double rightLandZ   = NAN_D;   // mm
    double combinedLandZ = NAN_D;  // mm — 좌우 풀링(V2 확장, 3번째 깊이 출력용)
    double centerXmm    = 0.0;
};

static double median(std::vector<double> v) {
    if (v.empty()) return NAN_D;
    size_t m = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + (long)m, v.end());
    return v[m];
}

// V1 aggregate(): mean 또는 median (median은 인자를 nth_element로 부분정렬)
static double aggregate(std::vector<double>& v, const std::string& method) {
    if (v.empty()) return NAN_D;
    if (method == "mean") {
        double s = 0; for (double x : v) s += x;
        return s / (double)v.size();
    }
    size_t m = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + (long)m, v.end());
    return v[m];
}

// NaN 무시 이동중앙값(반경 half chunk). V1 stabilize()와 동일.
static std::vector<double> runningMedian(const std::vector<double>& vals, int half) {
    const int n = (int)vals.size();
    std::vector<double> out(n, NAN_D);
    for (int i = 0; i < n; ++i) {
        std::vector<double> win;
        win.reserve(2 * half + 1);
        for (int j = std::max(0, i - half); j <= std::min(n - 1, i + half); ++j)
            if (!std::isnan(vals[j])) win.push_back(vals[j]);
        if (!win.empty()) out[i] = median(std::move(win));
    }
    return out;
}

static double linearSlope(const double* ys, const double* zs, int n) {
    if (n < 2) return 0.0;
    double sx = 0, sz = 0, sxz = 0, sxx = 0;
    for (int i = 0; i < n; ++i) {
        sx += ys[i]; sz += zs[i];
        sxz += ys[i] * zs[i]; sxx += ys[i] * ys[i];
    }
    double d = (double)n * sxx - sx * sx;
    return (std::abs(d) < 1e-18) ? 0.0 : ((double)n * sxz - sx * sz) / d;
}

// V1 detectFloorFlat과 동일: 최소-분산(p95-p05) 창을 슬라이딩 탐색 → median/mean(floorAgg)을 바닥값으로.
// floorSearchFrac(V2 확장, 기본 1.0=전체 범위=V1과 동일)로 창 중심 후보 범위를 notch 중앙으로 좁힐 수 있음.
static bool detectFloorFlat(const std::vector<EnvPt>& env, const std::vector<double>& rel,
                             double loY, double hiY, const NotchMeasureV2Params& p, ChunkResult& cr) {
    const double halfWin = (p.floorWinUm / 1000.0) / 2.0;
    const double stepMm  = p.lateralResMm;
    const double trigUm  = p.notchTrigUm;

    const double notchWidth   = hiY - loY;
    const double centerMargin = notchWidth * (1.0 - std::clamp(p.floorSearchFrac, 0.05, 1.0)) / 2.0;
    const double searchLoY = loY + centerMargin;
    const double searchHiY = hiY - centerMargin;

    double bestSpread = std::numeric_limits<double>::max();
    double bestCenter = NAN_D, bestZagg = NAN_D;

    for (double c = searchLoY; c <= searchHiY; c += stepMm) {
        std::vector<double> winRel;
        winRel.reserve(p.floorMinPts * 2);
        for (int k = 0; k < (int)env.size(); ++k)
            if (env[k].y >= c - halfWin && env[k].y <= c + halfWin)
                winRel.push_back(rel[k]);
        if ((int)winRel.size() < p.floorMinPts) continue;

        bool touchesLand = false;
        for (double r : winRel) if (r > trigUm) { touchesLand = true; break; }
        if (touchesLand) continue;

        std::sort(winRel.begin(), winRel.end());
        const int m = (int)winRel.size();
        const double p05 = winRel[(size_t)std::floor(m * 0.05)];
        const double p95 = winRel[(size_t)std::floor(m * 0.95)];
        const double spread = p95 - p05;
        if (spread < bestSpread) {
            bestSpread = spread;
            bestCenter = c;
            std::vector<double> tmp = winRel;
            bestZagg = aggregate(tmp, p.floorAgg);
        }
    }
    if (std::isnan(bestCenter)) return false;
    cr.floorCenterY = bestCenter;
    cr.floorZRel    = bestZagg;
    cr.floorWidth   = p.floorWinUm;
    return true;
}

// V1 detectFloorCorner과 동일: 기울기 급감 지점(좌/우)을 코너로 판정, 그 사이를 바닥으로.
static bool detectFloorCorner(const std::vector<EnvPt>& env, const std::vector<double>& rel,
                               double loY, double hiY, const NotchMeasureV2Params& p, ChunkResult& cr) {
    const int ne = (int)env.size();
    if (ne < 5) return false;

    int h = std::max(1, std::min(p.smoothCols, 5) / 2);
    std::vector<double> srel(ne);
    for (int i = 0; i < ne; ++i) {
        int lo = std::max(0, i - h), hi = std::min(ne - 1, i + h);
        double sum = 0; int cnt = 0;
        for (int j = lo; j <= hi; ++j) { sum += rel[j]; ++cnt; }
        srel[i] = (cnt > 0) ? sum / cnt : rel[i];
    }

    const double centerY   = (loY + hiY) / 2.0;
    const double searchMm  = p.cornerSearchUm / 1000.0;
    const double stepMm    = p.lateralResMm;
    const double halfWinMm = stepMm * 3;
    const double offset50um = 0.050;

    auto slopeAt = [&](double c, bool goLeft) -> double {
        std::vector<double> ys, zs;
        for (int k = 0; k < ne; ++k)
            if (env[k].y >= c - halfWinMm && env[k].y <= c + halfWinMm)
                { ys.push_back(env[k].y); zs.push_back(srel[k]); }
        if ((int)ys.size() < 3) return NAN_D;
        double slope = linearSlope(ys.data(), zs.data(), (int)ys.size());
        return goLeft ? -slope : slope;
    };

    auto findCorner = [&](bool goLeft) -> double {
        double start = goLeft ? centerY - offset50um : centerY + offset50um;
        double limit = goLeft ? centerY - searchMm   : centerY + searchMm;
        double prevSlope = NAN_D;
        double dir = goLeft ? -stepMm : stepMm;
        for (double c = start; goLeft ? (c > limit) : (c < limit); c += dir) {
            double sl = slopeAt(c, goLeft);
            if (std::isnan(sl)) { prevSlope = NAN_D; continue; }
            if (!std::isnan(prevSlope) && std::abs(prevSlope) > 1e-6) {
                if (std::abs(sl) < std::abs(prevSlope) * p.slopeDrop)
                    return c;
            }
            prevSlope = sl;
        }
        return NAN_D;
    };

    double cL = findCorner(true), cR = findCorner(false);
    if (std::isnan(cL) || std::isnan(cR) || cL >= cR) return false;

    std::vector<double> floorRel;
    for (int k = 0; k < ne; ++k)
        if (env[k].y >= cL && env[k].y <= cR) floorRel.push_back(rel[k]);
    if (floorRel.empty()) return false;

    cr.floorCenterY = (cL + cR) / 2.0;
    cr.floorZRel    = aggregate(floorRel, p.floorAgg);
    cr.floorWidth   = (cR - cL) * 1000.0;
    return true;
}

// 한 chunk의 envelope 처리 — V1 processChunkEnv와 동일 흐름 + combinedLandZ(V2 확장) 계산 추가
static void processChunkEnv(const std::vector<EnvPt>& env, const NotchMeasureV2Params& p, ChunkResult& cr) {
    if ((int)env.size() < 9) return;

    std::vector<double> ys, zs;
    ys.reserve(env.size()); zs.reserve(env.size());
    for (auto& e : env) { ys.push_back(e.y); zs.push_back(e.z); }

    std::vector<bool> keep(env.size(), true);
    double coef[4] = {};
    if (!poly::robustPolyfit3(ys, zs, keep, coef, p.landFitIters)) return;

    std::vector<double> rel(env.size());
    for (int k = 0; k < (int)env.size(); ++k)
        rel[k] = (env[k].z - poly::eval3(coef, env[k].y)) * 1000.0;

    // notch 개구: land 기준선 대비 notchTrigUm보다 낮은 최장 연속구간(gap 허용)
    double notchLoY = 0, notchHiY = 0;
    {
        int bestLen = 0, bestStart = -1, runLen = 0, runStart = -1;
        for (int k = 0; k < (int)env.size(); ++k) {
            if (rel[k] < p.notchTrigUm) {
                if (runLen == 0) runStart = k;
                else {
                    double gap = (env[k].y - env[k - 1].y) * 1000.0;
                    if (gap > p.notchMaxGapUm) { runLen = 0; runStart = k; }
                }
                ++runLen;
                if (runLen > bestLen) { bestLen = runLen; bestStart = runStart; }
            } else {
                runLen = 0;
            }
        }
        if (bestLen < p.notchMinCols || bestStart < 0) return;
        notchLoY = env[bestStart].y;
        notchHiY = env[bestStart + bestLen - 1].y;
    }

    std::memcpy(cr.coef, coef, sizeof(coef));
    cr.hadFit   = true;
    cr.notchLoY = notchLoY;
    cr.notchHiY = notchHiY;

    bool floorOk = false;
    if (p.method == "corner") {
        floorOk = detectFloorCorner(env, rel, notchLoY, notchHiY, p, cr);
        if (!floorOk) floorOk = detectFloorFlat(env, rel, notchLoY, notchHiY, p, cr);
    } else {
        floorOk = detectFloorFlat(env, rel, notchLoY, notchHiY, p, cr);
    }
    if (!floorOk) return;

    // land: notch 밖 영역의 점을 사용.
    // landFlatFilter=true면 |rel|<landTolUm인 평탄한 점만(V1과 동일) — 너무 좁은 landMaxDistMm과 겹치면
    // 조건 만족 점이 하나도 없어 NaN이 될 수 있음.
    // landFlatFilter=false(기본)면 평탄도 필터 없이 구간 내 전체 점을 사용 — landAgg=median의 이상치 강건성에 의존.
    // landMaxDistMm > 0이면 notch 경계에서 그 거리 이내 점만 참조(기존 V2의 landSampleMm과 동일 취지).
    std::vector<double> leftVals, rightVals;
    for (int k = 0; k < (int)env.size(); ++k) {
        if (p.landFlatFilter && std::abs(rel[k]) >= p.landTolUm) continue;
        if (env[k].y < notchLoY) {
            if (p.landMaxDistMm > 0.0 && (notchLoY - env[k].y) > p.landMaxDistMm) continue;
            leftVals.push_back(env[k].z);
        } else if (env[k].y > notchHiY) {
            if (p.landMaxDistMm > 0.0 && (env[k].y - notchHiY) > p.landMaxDistMm) continue;
            rightVals.push_back(env[k].z);
        }
    }
    const bool useMedian = (p.landAgg == "median");
    if (!leftVals.empty())  { std::vector<double> t = leftVals;  cr.leftLandZ  = aggregate(t, useMedian ? "median" : "mean"); }
    if (!rightVals.empty()) { std::vector<double> t = rightVals; cr.rightLandZ = aggregate(t, useMedian ? "median" : "mean"); }
    if (!leftVals.empty() || !rightVals.empty()) {
        std::vector<double> pooled = leftVals;
        pooled.insert(pooled.end(), rightVals.begin(), rightVals.end());
        cr.combinedLandZ = aggregate(pooled, useMedian ? "median" : "mean");
    }
    cr.valid = true;
}

// V1 stabilize()와 동일: 이웃 chunk(±half) 이동중앙값과 어긋난 값 스냅 + 무효 chunk도 이웃으로 채움
static void stabilize(std::vector<ChunkResult>& chunks, const NotchMeasureV2Params& p) {
    const int n = (int)chunks.size();
    const int half = p.floorStabilizeHalf;
    if (half <= 0) return;

    std::vector<double> cy(n, NAN_D), zrel(n, NAN_D), loY(n, NAN_D), hiY(n, NAN_D);
    for (int i = 0; i < n; ++i) {
        if (chunks[i].valid) {
            cy[i] = chunks[i].floorCenterY; zrel[i] = chunks[i].floorZRel;
            loY[i] = chunks[i].notchLoY; hiY[i] = chunks[i].notchHiY;
        }
    }
    auto medCY = runningMedian(cy, half);
    auto medZrel = runningMedian(zrel, half);
    auto medLoY = runningMedian(loY, half);
    auto medHiY = runningMedian(hiY, half);

    for (int i = 0; i < n; ++i) {
        auto& cr = chunks[i];
        if (!cr.valid) {
            if (!std::isnan(medCY[i]) && !std::isnan(medZrel[i]) &&
                !std::isnan(medLoY[i]) && !std::isnan(medHiY[i])) {
                cr.floorCenterY = medCY[i];
                cr.floorZRel    = medZrel[i];
                cr.notchLoY     = medLoY[i];
                cr.notchHiY     = medHiY[i];
                cr.valid        = true;
            }
        } else {
            if (!std::isnan(medCY[i]) &&
                std::abs(cr.floorCenterY - medCY[i]) * 1000.0 > p.floorStabilizeCenterTolUm)
                cr.floorCenterY = medCY[i];
            if (!std::isnan(medZrel[i]) &&
                std::abs(cr.floorZRel - medZrel[i]) > p.floorStabilizeZTolUm)
                cr.floorZRel = medZrel[i];
        }
    }
}

} // anonymous namespace

ToolResult NotchMeasureV2Tool::execute(VisionDataPtr input) {
    if (!input || !input->inCloud(0))
        return { ToolStatus::Fail, "NotchMeasureV2: PointCloud 입력 필요 (포트 0)" };
    const PointCloud3D& cloud = *input->inCloud(0);
    if (cloud.empty())
        return { ToolStatus::Fail, "NotchMeasureV2: 빈 PointCloud" };

    const int N = (int)cloud.points.size();
    const double tres   = m_p.transportResMm;
    const double lpitch = m_p.lateralResMm;

    // x(transport) 기준 profile 그룹핑
    std::vector<int32_t> profIdxArr(N);
    for (int i = 0; i < N; ++i)
        profIdxArr[i] = (int32_t)std::lround((double)cloud.points[i].x / tres);

    std::vector<int> order(N);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(),
        [&](int a, int b) { return profIdxArr[a] < profIdxArr[b]; });

    struct Slice { int start, end; };
    std::vector<Slice> slices;
    slices.reserve(N / 16);
    {
        int i = 0;
        while (i < N) {
            int32_t pi = profIdxArr[order[i]];
            int j = i;
            while (j < N && profIdxArr[order[j]] == pi) ++j;
            slices.push_back({ i, j });
            i = j;
        }
    }
    const int nProf = (int)slices.size();

    // avgProfiles개씩 chunk로 머지 (V1과 동일)
    const int chunkSize = std::max(1, m_p.avgProfiles);
    const int nChunks   = (nProf + chunkSize - 1) / chunkSize;
    std::vector<int> profToChunk(nProf);
    for (int pi = 0; pi < nProf; ++pi) profToChunk[pi] = pi / chunkSize;

    std::vector<ChunkResult> chunks(nChunks);

#pragma omp parallel for schedule(dynamic, 4)
    for (int ci = 0; ci < nChunks; ++ci) {
        const int piStart = ci * chunkSize;
        const int piEnd   = std::min(nProf, piStart + chunkSize);

        double xSum = 0; long xCnt = 0;
        std::unordered_map<int, std::vector<double>> colZvals;   // 컬럼별 (여러 profile의) 대표 z 모음
        for (int pi = piStart; pi < piEnd; ++pi) {
            const Slice& sl = slices[pi];
            std::unordered_map<int, double> colBest;   // 컬럼당 최대 z — 이 profile 하나만
            for (int k = sl.start; k < sl.end; ++k) {
                const Point3f& pt = cloud.points[order[k]];
                xSum += pt.x; ++xCnt;
                int col = (int)std::lround((double)pt.y / lpitch);
                auto it = colBest.find(col);
                if (it == colBest.end() || (double)pt.z > it->second) colBest[col] = (double)pt.z;
            }
            for (auto& [col, zmax] : colBest) colZvals[col].push_back(zmax);
        }
        chunks[ci].centerXmm = xCnt > 0 ? xSum / (double)xCnt : 0.0;

        std::vector<EnvPt> env;
        env.reserve(colZvals.size());
        for (auto& [col, zvals] : colZvals) {
            std::vector<double> tmp = zvals;
            double zAgg = aggregate(tmp, m_p.avgMethod);
            env.push_back({ col * lpitch, zAgg });
        }
        std::sort(env.begin(), env.end(), [](const EnvPt& a, const EnvPt& b) { return a.y < b.y; });

        processChunkEnv(env, m_p, chunks[ci]);
    }

    stabilize(chunks, m_p);

    auto out = std::make_shared<VisionData>();
    out->sourceId = input->sourceId;

    // ── PointCloud3D 출력: land/floor로 분류된 원본 점만 필터링 (V1과 동일 판정식) ──
    {
        auto outCloud = std::make_shared<PointCloud3D>();
        outCloud->frameId = cloud.frameId;
#ifdef _OPENMP
        int nT = omp_get_max_threads();
#else
        int nT = 1;
#endif
        std::vector<std::vector<Point3f>> threadPts(nT);

#pragma omp parallel for schedule(dynamic, 64)
        for (int pi = 0; pi < nProf; ++pi) {
            const Slice& sl = slices[pi];
            const ChunkResult& cr = chunks[profToChunk[pi]];
            if (!cr.hadFit || !cr.valid || cr.notchHiY <= cr.notchLoY) continue;
#ifdef _OPENMP
            int tid = omp_get_thread_num();
#else
            int tid = 0;
#endif
            auto& buf = threadPts[tid];
            const double floorHalfMm = cr.floorWidth / 2000.0;
            for (int k = sl.start; k < sl.end; ++k) {
                const Point3f& pt = cloud.points[order[k]];
                const double y = (double)pt.y, z = (double)pt.z;
                const double relUm = (z - poly::eval3(cr.coef, y)) * 1000.0;
                const bool inNotch = (y > cr.notchLoY - m_p.landMarginMm) && (y < cr.notchHiY + m_p.landMarginMm);
                const bool withinLandDist = (m_p.landMaxDistMm <= 0.0)
                    || (y < cr.notchLoY && (cr.notchLoY - y) <= m_p.landMaxDistMm)
                    || (y > cr.notchHiY && (y - cr.notchHiY) <= m_p.landMaxDistMm);
                const bool isLand  = !inNotch && withinLandDist
                    && (!m_p.landFlatFilter || std::abs(relUm) < m_p.landTolUm);
                const bool isFloor = (std::abs(y - cr.floorCenterY) < floorHalfMm)
                                   && (std::abs(relUm - cr.floorZRel) < m_p.floorTolUm);
                if (isLand || isFloor) buf.push_back(pt);
            }
        }
        size_t total = 0;
        for (auto& b : threadPts) total += b.size();
        outCloud->points.reserve(total);
        for (auto& b : threadPts) outCloud->points.insert(outCloud->points.end(), b.begin(), b.end());
        out->setCloud(outCloud);
    }

    // ── Profile[] 출력 (기존 V2 스키마 유지) ──
    auto profL   = std::make_shared<Profile>();
    auto profR   = std::make_shared<Profile>();
    auto profC   = std::make_shared<Profile>();
    auto profZ   = std::make_shared<Profile>();
    auto profLZ  = std::make_shared<Profile>();
    auto profRZ  = std::make_shared<Profile>();
    profL->label  = "depth_left_um";
    profR->label  = "depth_right_um";
    profC->label  = "depth_combined_um";
    profZ->label  = "notch_floor_z_mm";
    profLZ->label = "land_left_z_mm";
    profRZ->label = "land_right_z_mm";
    profL->frameId = profR->frameId = profC->frameId
        = profZ->frameId = profLZ->frameId = profRZ->frameId = cloud.frameId;

    int validCount = 0;
    for (const auto& cr : chunks) {
        if (!cr.valid || !cr.hadFit) continue;
        ++validCount;

        const double floorZmm = poly::eval3(cr.coef, cr.floorCenterY) + cr.floorZRel / 1000.0;
        const double dL = std::isnan(cr.leftLandZ)     ? NAN_D : (cr.leftLandZ     - floorZmm) * 1000.0;
        const double dR = std::isnan(cr.rightLandZ)    ? NAN_D : (cr.rightLandZ    - floorZmm) * 1000.0;
        const double dC = std::isnan(cr.combinedLandZ) ? NAN_D : (cr.combinedLandZ - floorZmm) * 1000.0;

        profL->x.push_back(cr.centerXmm);  profL->y.push_back(0);  profL->s.push_back(cr.centerXmm);  profL->z.push_back(dL);
        profR->x.push_back(cr.centerXmm);  profR->y.push_back(0);  profR->s.push_back(cr.centerXmm);  profR->z.push_back(dR);
        profC->x.push_back(cr.centerXmm);  profC->y.push_back(0);  profC->s.push_back(cr.centerXmm);  profC->z.push_back(dC);
        profZ->x.push_back(cr.centerXmm);  profZ->y.push_back(0);  profZ->s.push_back(cr.centerXmm);  profZ->z.push_back(floorZmm);
        profLZ->x.push_back(cr.centerXmm); profLZ->y.push_back(0); profLZ->s.push_back(cr.centerXmm); profLZ->z.push_back(cr.leftLandZ);
        profRZ->x.push_back(cr.centerXmm); profRZ->y.push_back(0); profRZ->s.push_back(cr.centerXmm); profRZ->z.push_back(cr.rightLandZ);
    }
    out->profiles.push_back(profL);
    out->profiles.push_back(profR);
    out->profiles.push_back(profC);
    out->profiles.push_back(profZ);
    out->profiles.push_back(profLZ);
    out->profiles.push_back(profRZ);
    out->measurements.push_back({ "valid_count", (double)validCount, "", validCount > 0 });

    VISION_LOG_INFO("NotchMeasureV2: {} profiles → {} chunks ({} merged) → {} valid",
                    nProf, nChunks, chunkSize, validCount);
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
