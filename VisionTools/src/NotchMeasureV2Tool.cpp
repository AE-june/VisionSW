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
    double coef[4]      = { 0, 0, 0, 0 };  // 전체 land 피팅 (notch 검출 + floor 기준)
    double leftCoef[4]  = { 0, 0, 0, 0 };  // 좌측 land 별도 피팅
    double rightCoef[4] = { 0, 0, 0, 0 };  // 우측 land 별도 피팅
    double notchLoY     = 0.0;
    double notchHiY     = 0.0;
    double floorCenterY = 0.0;
    double floorZRel    = 0.0;     // µm, 피팅 곡선 대비 상대값
    double floorWidth   = 0.0;     // µm
    double leftLandZ    = NAN_D;   // mm, 절대높이 (mean/median 집계)
    double rightLandZ   = NAN_D;   // mm
    double combinedLandZ = NAN_D;  // mm — 좌우 풀링(V2 확장, 3번째 깊이 출력용)
    double leftEdgeZ    = NAN_D;   // mm, 기울기 기반 land 시작점
    double leftEdgeY    = NAN_D;   // mm, lateral 위치
    double rightEdgeZ   = NAN_D;   // mm
    double rightEdgeY   = NAN_D;   // mm
    double centerXmm    = 0.0;
    std::vector<EnvPt> env;        // 시각화용 envelope 데이터 (y=lateral mm, z=absolute mm)
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

// notch 최장 연속구간 탐색 — 성공 시 true, loY/hiY 업데이트
static bool findNotchInterval(const std::vector<EnvPt>& env,
                               const std::vector<double>& rel,
                               const NotchMeasureV2Params& p,
                               double& loY, double& hiY) {
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
        } else { runLen = 0; }
    }
    if (bestLen < p.notchMinCols || bestStart < 0) return false;
    loY = env[bestStart].y;
    hiY = env[bestStart + bestLen - 1].y;
    return true;
}

// 한 chunk의 envelope 처리
static void processChunkEnv(const std::vector<EnvPt>& env, const NotchMeasureV2Params& p, ChunkResult& cr) {
    if ((int)env.size() < 9) return;
    const int nEnv = (int)env.size();

    std::vector<double> ys, zs;
    ys.reserve(nEnv); zs.reserve(nEnv);
    for (auto& e : env) { ys.push_back(e.y); zs.push_back(e.z); }

    // ── 1. 전체 robust 피팅 + 1차 노치 검출 ──────────────────────────────
    std::vector<bool> keep(nEnv, true);
    double coef[4] = {};
    if (!poly::robustPolyfit3(ys, zs, keep, coef, p.landFitIters)) return;

    auto makeRel = [&](const std::function<double(double)>& evalFn) {
        std::vector<double> r(nEnv);
        for (int k = 0; k < nEnv; ++k)
            r[k] = (env[k].z - evalFn(env[k].y)) * 1000.0;
        return r;
    };
    std::vector<double> rel = makeRel([&](double y){ return poly::eval3(coef, y); });

    double notchLoY = 0, notchHiY = 0;
    if (!findNotchInterval(env, rel, p, notchLoY, notchHiY)) return;

    std::memcpy(cr.coef, coef, sizeof(coef));
    cr.hadFit   = true;
    cr.notchLoY = notchLoY;
    cr.notchHiY = notchHiY;

    // ── 2. 좌/우 별도 피팅 (노치 내부 제외) ─────────────────────────────
    {
        std::vector<double> lys, lzs, rys, rzs;
        for (const auto& ep : env) {
            if (ep.y < notchLoY) { lys.push_back(ep.y); lzs.push_back(ep.z); }
            else if (ep.y > notchHiY) { rys.push_back(ep.y); rzs.push_back(ep.z); }
        }
        std::vector<bool> lk(lys.size(), true), rk(rys.size(), true);
        if (!poly::robustPolyfit3(lys, lzs, lk, cr.leftCoef,  p.landFitIters))
            std::memcpy(cr.leftCoef,  coef, sizeof(coef));
        if (!poly::robustPolyfit3(rys, rzs, rk, cr.rightCoef, p.landFitIters))
            std::memcpy(cr.rightCoef, coef, sizeof(coef));
    }

    // ── 3. 좌/우 피팅 기준 rel2 재계산 → 노치 재검출 ───────────────────
    // 노치 내부: leftCoef/rightCoef 선형 보간 → floor 기준점 일관성 유지
    const double notchW2 = (notchHiY > notchLoY) ? (notchHiY - notchLoY) : 1e-9;
    std::vector<double> rel2 = makeRel([&](double y) -> double {
        if (y <= notchLoY) return poly::eval3(cr.leftCoef, y);
        if (y >= notchHiY) return poly::eval3(cr.rightCoef, y);
        double t = (y - notchLoY) / notchW2;
        return poly::eval3(cr.leftCoef, y) * (1.0 - t) + poly::eval3(cr.rightCoef, y) * t;
    });
    {
        double lo2 = notchLoY, hi2 = notchHiY;
        if (findNotchInterval(env, rel2, p, lo2, hi2)) {
            cr.notchLoY = lo2; cr.notchHiY = hi2;
            notchLoY = lo2;   notchHiY = hi2;
        }
        // 재검출 실패 시 기존 경계 유지
    }

    // ── 4. 바닥 검출 (rel2, 정제된 경계 기준) ───────────────────────────
    bool floorOk = false;
    if (p.method == "corner") {
        floorOk = detectFloorCorner(env, rel2, notchLoY, notchHiY, p, cr);
        if (!floorOk) floorOk = detectFloorFlat(env, rel2, notchLoY, notchHiY, p, cr);
    } else {
        floorOk = detectFloorFlat(env, rel2, notchLoY, notchHiY, p, cr);
    }
    if (!floorOk) return;

    // ── 5. land 집계 (rel2 기준, landFlatFilter 적용) ───────────────────
    std::vector<double> leftVals, rightVals;
    for (int k = 0; k < nEnv; ++k) {
        if (p.landFlatFilter && std::abs(rel2[k]) >= p.landTolUm) continue;
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

    // ── 6. edge 탐색 — 노치 경계에서 바깥으로, 기울기 부호 반전 OR 평탄 구간 첫 점
    //    dev(k) = actualSlope(k,W) - sideCoef 도함수(y[k])
    //    signChange: prevDev * dev < 0  /  flat: |dev| < tol
    {
        const double tolMmPerMm = p.edgeSlopeTolUmPerMm / 1000.0;
        const int    W          = std::max(1, p.edgeSlopeWindowPts);

        // 좌측: notchLoY에서 왼쪽으로 (k 감소 = 노치에서 멀어짐)
        int startK = -1;
        for (int k = nEnv - 1; k >= 0; --k) {
            if (env[k].y < notchLoY) { startK = k; break; }
        }
        if (startK >= 1) {
            double prevDev = NAN_D;
            for (int k = startK; k >= 1; --k) {
                int w = std::min(W, k);
                double dy = env[k - w].y - env[k].y;
                double dz = env[k - w].z - env[k].z;
                if (std::abs(dy) < 1e-9) { prevDev = NAN_D; continue; }
                double dev = (dz / dy) - poly::deriv3(cr.leftCoef, env[k].y);
                if (!std::isnan(prevDev)) {
                    if (prevDev * dev < 0 || std::abs(dev) < tolMmPerMm) {
                        cr.leftEdgeZ = env[k].z; cr.leftEdgeY = env[k].y; break;
                    }
                }
                prevDev = dev;
            }
            if (std::isnan(cr.leftEdgeZ)) { cr.leftEdgeZ = env[0].z; cr.leftEdgeY = env[0].y; }
        } else {
            cr.leftEdgeZ = poly::eval3(cr.leftCoef, notchLoY);
            cr.leftEdgeY = notchLoY;
        }

        // 우측: notchHiY에서 오른쪽으로 (k 증가 = 노치에서 멀어짐)
        int startR = -1;
        for (int k = 0; k < nEnv; ++k) {
            if (env[k].y > notchHiY) { startR = k; break; }
        }
        if (startR >= 0 && startR < nEnv - 1) {
            double prevDev = NAN_D;
            for (int k = startR; k < nEnv - 1; ++k) {
                int w = std::min(W, nEnv - 1 - k);
                double dy = env[k + w].y - env[k].y;
                double dz = env[k + w].z - env[k].z;
                if (std::abs(dy) < 1e-9) { prevDev = NAN_D; continue; }
                double dev = (dz / dy) - poly::deriv3(cr.rightCoef, env[k].y);
                if (!std::isnan(prevDev)) {
                    if (prevDev * dev < 0 || std::abs(dev) < tolMmPerMm) {
                        cr.rightEdgeZ = env[k].z; cr.rightEdgeY = env[k].y; break;
                    }
                }
                prevDev = dev;
            }
            if (std::isnan(cr.rightEdgeZ)) { cr.rightEdgeZ = env.back().z; cr.rightEdgeY = env.back().y; }
        } else {
            cr.rightEdgeZ = poly::eval3(cr.rightCoef, notchHiY);
            cr.rightEdgeY = notchHiY;
        }
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

        chunks[ci].env = env;   // 시각화용 보존 (정렬 후)
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
    auto profLE  = std::make_shared<Profile>();  // depth_left_edge_um
    auto profRE  = std::make_shared<Profile>();  // depth_right_edge_um
    profL->label  = "depth_left_um";
    profR->label  = "depth_right_um";
    profC->label  = "depth_combined_um";
    profZ->label  = "notch_floor_z_mm";
    profLZ->label = "land_left_z_mm";
    profRZ->label = "land_right_z_mm";
    profLE->label = "depth_left_edge_um";
    profRE->label = "depth_right_edge_um";
    profL->frameId = profR->frameId = profC->frameId
        = profZ->frameId = profLZ->frameId = profRZ->frameId
        = profLE->frameId = profRE->frameId = cloud.frameId;

    int validCount = 0;
    for (const auto& cr : chunks) {
        if (!cr.valid || !cr.hadFit) continue;
        ++validCount;

        // floorZRel은 rel2(좌우 보간) 기준 → interpRef로 절대 z 복원
        double fT = (cr.notchHiY > cr.notchLoY)
            ? std::max(0.0, std::min(1.0, (cr.floorCenterY - cr.notchLoY) / (cr.notchHiY - cr.notchLoY)))
            : 0.5;
        double fInterpRef = poly::eval3(cr.leftCoef, cr.floorCenterY) * (1.0 - fT)
                          + poly::eval3(cr.rightCoef, cr.floorCenterY) * fT;
        const double floorZmm = fInterpRef + cr.floorZRel / 1000.0;
        const double dL  = std::isnan(cr.leftLandZ)    ? NAN_D : (cr.leftLandZ    - floorZmm) * 1000.0;
        const double dR  = std::isnan(cr.rightLandZ)   ? NAN_D : (cr.rightLandZ   - floorZmm) * 1000.0;
        const double dC  = std::isnan(cr.combinedLandZ)? NAN_D : (cr.combinedLandZ- floorZmm) * 1000.0;
        const double dLE = std::isnan(cr.leftEdgeZ)    ? NAN_D : (cr.leftEdgeZ    - floorZmm) * 1000.0;
        const double dRE = std::isnan(cr.rightEdgeZ)   ? NAN_D : (cr.rightEdgeZ   - floorZmm) * 1000.0;

        profL->x.push_back(cr.centerXmm);  profL->y.push_back(0);  profL->s.push_back(cr.centerXmm);  profL->z.push_back(dL);
        profR->x.push_back(cr.centerXmm);  profR->y.push_back(0);  profR->s.push_back(cr.centerXmm);  profR->z.push_back(dR);
        profC->x.push_back(cr.centerXmm);  profC->y.push_back(0);  profC->s.push_back(cr.centerXmm);  profC->z.push_back(dC);
        profZ->x.push_back(cr.centerXmm);  profZ->y.push_back(0);  profZ->s.push_back(cr.centerXmm);  profZ->z.push_back(floorZmm);
        profLZ->x.push_back(cr.centerXmm); profLZ->y.push_back(0); profLZ->s.push_back(cr.centerXmm); profLZ->z.push_back(cr.leftLandZ);
        profRZ->x.push_back(cr.centerXmm); profRZ->y.push_back(0); profRZ->s.push_back(cr.centerXmm); profRZ->z.push_back(cr.rightLandZ);
        profLE->x.push_back(cr.centerXmm); profLE->y.push_back(0); profLE->s.push_back(cr.centerXmm); profLE->z.push_back(dLE);
        profRE->x.push_back(cr.centerXmm); profRE->y.push_back(0); profRE->s.push_back(cr.centerXmm); profRE->z.push_back(dRE);
    }
    out->profiles.push_back(profL);
    out->profiles.push_back(profR);
    out->profiles.push_back(profC);
    out->profiles.push_back(profZ);
    out->profiles.push_back(profLZ);
    out->profiles.push_back(profRZ);
    out->profiles.push_back(profLE);
    out->profiles.push_back(profRE);
    out->measurements.push_back({ "valid_count", (double)validCount, "", validCount > 0 });

    // ── Chunk envelope profiles (시각화용 — main.cpp에서 별도 캐시로 분리) ──
    // label = "__notchenv_{ci}", x=lateral mm, z=abs z mm, y=fit curve mm,
    // s=[c0,c1,c2,c3, notchLoY,notchHiY,floorCenterY,floorZRel_um]
    for (int ci = 0; ci < nChunks; ++ci) {
        const ChunkResult& cr = chunks[ci];
        if (!cr.hadFit || cr.env.empty()) continue;
        auto profEnv = std::make_shared<Profile>();
        profEnv->label = "__notchenv_" + std::to_string(ci);
        profEnv->frameId = cloud.frameId;
        for (const auto& ep : cr.env) {
            profEnv->x.push_back(ep.y);
            profEnv->z.push_back(ep.z);
            const double* sideCoef = (ep.y <= cr.notchLoY) ? cr.leftCoef : cr.rightCoef;
            profEnv->y.push_back(poly::eval3(sideCoef, ep.y));
        }
        // s[0..3]: floor 기준 ref 전달. main.cpp는 c0+c1*y+c2*y²+c3*y³ 공식으로 floorZmm 계산.
        // rel2 기준(좌우 보간)과 일치시키려면 interpRef 하나만 s[0]에 넣고 s[1..3]=0.
        double floorT = (cr.notchHiY > cr.notchLoY)
            ? std::max(0.0, std::min(1.0, (cr.floorCenterY - cr.notchLoY) / (cr.notchHiY - cr.notchLoY)))
            : 0.5;
        double interpRef = poly::eval3(cr.leftCoef,  cr.floorCenterY) * (1.0 - floorT)
                         + poly::eval3(cr.rightCoef, cr.floorCenterY) * floorT;
        profEnv->s = { interpRef, 0.0, 0.0, 0.0,
                       cr.notchLoY, cr.notchHiY, cr.floorCenterY, cr.floorZRel,
                       cr.leftLandZ, cr.rightLandZ,
                       cr.leftEdgeZ, cr.rightEdgeZ,
                       cr.leftEdgeY, cr.rightEdgeY };
        out->profiles.push_back(profEnv);
    }

    VISION_LOG_INFO("NotchMeasureV2: {} profiles → {} chunks ({} merged) → {} valid",
                    nProf, nChunks, chunkSize, validCount);
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
