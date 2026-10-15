#include "NotchMeasureTool.h"
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

// ──────────────────────────────────────────────
// Internal data structures
// ──────────────────────────────────────────────

struct EnvPt {
    double y;   // mm
    double z;   // mm (merged/aggregated per column)
};

// Result for one chunk (= avgProfiles consecutive scan profiles merged)
struct ChunkResult {
    bool   hadFit       = false;
    bool   valid        = false;
    bool   usedFallback = false;
    double coef[4]      = {};
    double notchLoY     = 0.0;   // mm
    double notchHiY     = 0.0;   // mm
    double floorCenterY = 0.0;  // mm
    double floorZRel    = 0.0;   // µm, negative = below land
    double floorWidth   = 0.0;  // µm
    double leftLandZ    = std::numeric_limits<double>::quiet_NaN();
    double rightLandZ   = std::numeric_limits<double>::quiet_NaN();
    double centerXmm    = 0.0;  // scan center position of this chunk
};

// ──────────────────────────────────────────────
// Utility
// ──────────────────────────────────────────────

static const double NAN_D = std::numeric_limits<double>::quiet_NaN();

// NaN-ignoring running median with 2*half+1 window
static std::vector<double> runningMedian(const std::vector<double>& vals, int half) {
    const int n = (int)vals.size();
    std::vector<double> out(n, NAN_D);
    for (int i = 0; i < n; ++i) {
        std::vector<double> win;
        win.reserve(2 * half + 1);
        for (int j = std::max(0, i - half); j <= std::min(n - 1, i + half); ++j)
            if (!std::isnan(vals[j])) win.push_back(vals[j]);
        if (!win.empty()) {
            size_t m = win.size() / 2;
            std::nth_element(win.begin(), win.begin() + (long)m, win.end());
            out[i] = win[m];
        }
    }
    return out;
}

// Least-squares slope
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

// Aggregate a vector of doubles (median or mean)
static double aggregate(std::vector<double>& v, const std::string& method) {
    if (v.empty()) return NAN_D;
    if (method == "mean") {
        double s = 0; for (double x : v) s += x;
        return s / v.size();
    }
    // median
    size_t m = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + (long)m, v.end());
    return v[m];
}

// ──────────────────────────────────────────────
// Floor detection — flat method
// ──────────────────────────────────────────────

static bool detectFloorFlat(const std::vector<EnvPt>& env,
                             const std::vector<double>& rel,
                             double loY, double hiY,
                             const NotchMeasureParams& p,
                             ChunkResult& cr) {
    const double halfWin = p.floorWinUm / 2000.0;
    const double stepMm  = p.lateralPitchMm;
    const double trigUm  = p.notchTrigUm;

    double bestSpread = std::numeric_limits<double>::max();
    double bestCenter = NAN_D;
    double bestZagg   = NAN_D;

    for (double c = loY - halfWin + halfWin; c <= hiY + halfWin - halfWin; c += stepMm) {
        std::vector<double> winRel;
        winRel.reserve(p.floorMinPts * 2);
        for (int k = 0; k < (int)env.size(); ++k) {
            if (env[k].y >= c - halfWin && env[k].y <= c + halfWin)
                winRel.push_back(rel[k]);
        }
        if ((int)winRel.size() < p.floorMinPts) continue;

        bool touchesLand = false;
        for (double r : winRel) if (r > trigUm) { touchesLand = true; break; }
        if (touchesLand) continue;

        std::sort(winRel.begin(), winRel.end());
        const int n = (int)winRel.size();
        double p05 = winRel[(int)std::floor(n * 0.05)];
        double p95 = winRel[(int)std::floor(n * 0.95)];
        double spread = p95 - p05;

        if (spread < bestSpread) {
            bestSpread = spread;
            bestCenter = c;
            // Aggregate floor rel according to floorAgg
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

// ──────────────────────────────────────────────
// Floor detection — corner method
// ──────────────────────────────────────────────

static bool detectFloorCorner(const std::vector<EnvPt>& env,
                               const std::vector<double>& rel,
                               double loY, double hiY,
                               const NotchMeasureParams& p,
                               ChunkResult& cr) {
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

    const double centerY  = (loY + hiY) / 2.0;
    const double searchMm = p.cornerSearchUm / 1000.0;
    const double stepMm   = p.lateralPitchMm;
    const double halfWinMm = stepMm * 3;
    const double offset50um = 0.050;

    auto slopeAt = [&](double c, bool goLeft) -> double {
        std::vector<double> ys, zs;
        for (int k = 0; k < ne; ++k) {
            if (env[k].y >= c - halfWinMm && env[k].y <= c + halfWinMm)
                { ys.push_back(env[k].y); zs.push_back(srel[k]); }
        }
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

    double cL = findCorner(true);
    double cR = findCorner(false);
    if (std::isnan(cL) || std::isnan(cR) || cL >= cR) return false;

    std::vector<double> floorRel;
    for (int k = 0; k < ne; ++k)
        if (env[k].y >= cL && env[k].y <= cR)
            floorRel.push_back(rel[k]);
    if (floorRel.empty()) return false;

    cr.floorCenterY = (cL + cR) / 2.0;
    cr.floorZRel    = aggregate(floorRel, p.floorAgg);
    cr.floorWidth   = (cR - cL) * 1000.0;
    return true;
}

// ──────────────────────────────────────────────
// Process one merged envelope → ChunkResult
// ──────────────────────────────────────────────

static void processChunkEnv(const std::vector<EnvPt>& env,
                             const NotchMeasureParams& p,
                             ChunkResult& cr) {
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

    // Find notch opening
    double notchLoY = 0, notchHiY = 0;
    {
        int bestLen = 0, bestStart = -1;
        int runLen = 0, runStart = -1;
        for (int k = 0; k < (int)env.size(); ++k) {
            if (rel[k] < p.notchTrigUm) {
                if (runLen == 0) {
                    runStart = k;
                } else {
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

    memcpy(cr.coef, coef, sizeof(coef));
    cr.hadFit   = true;
    cr.notchLoY = notchLoY;
    cr.notchHiY = notchHiY;

    bool floorOk = false;
    if (p.method == "corner") {
        floorOk = detectFloorCorner(env, rel, notchLoY, notchHiY, p, cr);
        if (!floorOk) { cr.usedFallback = true; floorOk = detectFloorFlat(env, rel, notchLoY, notchHiY, p, cr); }
    } else {
        floorOk = detectFloorFlat(env, rel, notchLoY, notchHiY, p, cr);
    }
    if (!floorOk) return;

    // Left / right land mean z
    double sumL = 0, cntL = 0, sumR = 0, cntR = 0;
    for (int k = 0; k < (int)env.size(); ++k) {
        if (std::abs(rel[k]) >= p.landTolUm) continue;
        if (env[k].y < notchLoY) { sumL += env[k].z; ++cntL; }
        else if (env[k].y > notchHiY) { sumR += env[k].z; ++cntR; }
    }
    if (cntL > 0) cr.leftLandZ  = sumL / cntL;
    if (cntR > 0) cr.rightLandZ = sumR / cntR;
    cr.valid = true;
}

// ──────────────────────────────────────────────
// Pass 1.5 — Circumferential stabilization (on chunks)
// ──────────────────────────────────────────────

static void stabilize(std::vector<ChunkResult>& chunks) {
    const int n    = (int)chunks.size();
    const int half = 25;

    std::vector<double> cy(n, NAN_D), zrel(n, NAN_D), loY(n, NAN_D), hiY(n, NAN_D);
    for (int i = 0; i < n; ++i) {
        if (chunks[i].valid) {
            cy[i]   = chunks[i].floorCenterY;
            zrel[i] = chunks[i].floorZRel;
            loY[i]  = chunks[i].notchLoY;
            hiY[i]  = chunks[i].notchHiY;
        }
    }

    auto medCY   = runningMedian(cy,   half);
    auto medZrel = runningMedian(zrel, half);
    auto medLoY  = runningMedian(loY,  half);
    auto medHiY  = runningMedian(hiY,  half);

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
                std::abs(cr.floorCenterY - medCY[i]) * 1000.0 > 50.0)
                cr.floorCenterY = medCY[i];
            if (!std::isnan(medZrel[i]) &&
                std::abs(cr.floorZRel - medZrel[i]) > 60.0)
                cr.floorZRel = medZrel[i];
        }
    }
}

} // anonymous namespace

// ──────────────────────────────────────────────
// NotchMeasureTool::execute
// ──────────────────────────────────────────────

ToolResult NotchMeasureTool::execute(VisionDataPtr input) {
    if (!input || !input->inCloud(0))
        return {ToolStatus::Fail, "NotchMeasure: PointCloud 입력 필요 (포트 0)"};
    const PointCloud3D& cloud = *input->inCloud(0);
    if (cloud.empty())
        return {ToolStatus::Fail, "NotchMeasure: 빈 PointCloud"};

    const int N = (int)cloud.points.size();
    const double tres   = m_p.transportResMm;
    const double lpitch = m_p.lateralPitchMm;

    // ── Assign profile indices + sort ──
    std::vector<int32_t> profIdxArr(N);
    for (int i = 0; i < N; ++i)
        profIdxArr[i] = (int32_t)std::lround((double)cloud.points[i].x / tres);

    std::vector<int> order(N);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(),
        [&](int a, int b){ return profIdxArr[a] < profIdxArr[b]; });

    struct Slice { int32_t pi; int start, end; };
    std::vector<Slice> slices;
    slices.reserve(N / 16);
    {
        int i = 0;
        while (i < N) {
            int32_t pi = profIdxArr[order[i]];
            int j = i;
            while (j < N && profIdxArr[order[j]] == pi) ++j;
            slices.push_back({pi, i, j});
            i = j;
        }
    }
    const int nProf = (int)slices.size();

    // ── Chunk slices into groups of avgProfiles ──
    const int chunkSize = std::max(1, m_p.avgProfiles);
    const int nChunks   = (nProf + chunkSize - 1) / chunkSize;

    // profToChunk[pi] = chunk index
    std::vector<int> profToChunk(nProf);
    for (int pi = 0; pi < nProf; ++pi)
        profToChunk[pi] = pi / chunkSize;

    // ── Pass 1: build merged envelope per chunk + measure (OpenMP) ──
    std::vector<ChunkResult> chunks(nChunks);

    // Pre-compute per-profile upper envelopes to avoid redundant work
    // Each profile's envelope: vector of {col, zmax}
    // We'll build them inside the chunk loop (OMP private per thread)

#pragma omp parallel for schedule(dynamic, 4)
    for (int ci = 0; ci < nChunks; ++ci) {
        const int piStart = ci * chunkSize;
        const int piEnd   = std::min(nProf, piStart + chunkSize);

        // Compute center x of chunk
        double xSum = 0; int xCnt = 0;
        for (int pi = piStart; pi < piEnd; ++pi) {
            xSum += (double)slices[pi].pi * tres; ++xCnt;
        }
        chunks[ci].centerXmm = xCnt > 0 ? xSum / xCnt : 0.0;

        // Per column: collect z-max from each profile in chunk, then aggregate
        std::unordered_map<int, std::vector<double>> colZvals;
        colZvals.reserve(512);

        for (int pi = piStart; pi < piEnd; ++pi) {
            const Slice& sl = slices[pi];
            std::unordered_map<int, double> colBest;
            colBest.reserve(sl.end - sl.start);
            for (int k = sl.start; k < sl.end; ++k) {
                const auto& pt = cloud.points[order[k]];
                int col = (int)std::lround((double)pt.y / lpitch);
                auto it = colBest.find(col);
                if (it == colBest.end() || (double)pt.z > it->second)
                    colBest[col] = (double)pt.z;
            }
            for (auto& [col, zmax] : colBest)
                colZvals[col].push_back(zmax);
        }

        // Build merged envelope
        std::vector<EnvPt> env;
        env.reserve(colZvals.size());
        for (auto& [col, zvals] : colZvals) {
            double yMm  = (double)col * lpitch;
            double zAgg = aggregate(zvals, m_p.avgMethod);
            env.push_back({yMm, zAgg});
        }
        std::sort(env.begin(), env.end(), [](const EnvPt& a, const EnvPt& b){ return a.y < b.y; });

        processChunkEnv(env, m_p, chunks[ci]);
    }

    // ── Pass 1.5: stabilization ──
    stabilize(chunks);

    // ── Pass 2: label all original points (OpenMP) ──
    auto outCloud = std::make_shared<PointCloud3D>();
    outCloud->frameId = cloud.frameId;

    std::vector<std::vector<Point3f>> threadPts;
#ifdef _OPENMP
    int nT = omp_get_max_threads();
#else
    int nT = 1;
#endif
    threadPts.resize(nT);

#pragma omp parallel for schedule(dynamic, 64)
    for (int pi = 0; pi < nProf; ++pi) {
        const Slice& sl    = slices[pi];
        const ChunkResult& cr = chunks[profToChunk[pi]];
        if (!cr.hadFit) continue;
        if (!cr.valid) continue;
        if (cr.notchHiY <= cr.notchLoY) continue;

#ifdef _OPENMP
        int tid = omp_get_thread_num();
#else
        int tid = 0;
#endif
        auto& buf = threadPts[tid];

        const double floorHalfMm = cr.floorWidth / 2000.0;
        const double marginMm    = 0.020;

        for (int k = sl.start; k < sl.end; ++k) {
            const Point3f& pt = cloud.points[order[k]];
            double y    = (double)pt.y;
            double z    = (double)pt.z;
            double relUm = (z - poly::eval3(cr.coef, y)) * 1000.0;

            bool inNotch = (y > cr.notchLoY - marginMm) && (y < cr.notchHiY + marginMm);
            bool isLand  = !inNotch && std::abs(relUm) < m_p.landTolUm;
            bool isFloor = (std::abs(y - cr.floorCenterY) < floorHalfMm) &&
                           (std::abs(relUm - cr.floorZRel) < m_p.floorTolUm);

            if (isLand || isFloor)
                buf.push_back(pt);
        }
    }

    size_t total = 0;
    for (auto& b : threadPts) total += b.size();
    outCloud->points.reserve(total);
    for (auto& b : threadPts)
        outCloud->points.insert(outCloud->points.end(), b.begin(), b.end());

    // ── Build output ──
    auto out = std::make_shared<VisionData>();
    out->sourceId = input->sourceId;
    out->clouds.push_back(outCloud);

    // Per-chunk depth profiles (one sample per valid chunk)
    int validChunks = 0;
    {
        auto profP = std::make_shared<Profile>();
        auto profL = std::make_shared<Profile>();
        auto profR = std::make_shared<Profile>();
        profP->label = "depth_fit_um";
        profL->label = "depth_left_land_um";
        profR->label = "depth_right_land_um";
        profP->frameId = profL->frameId = profR->frameId = cloud.frameId;

        for (int ci = 0; ci < nChunks; ++ci) {
            const ChunkResult& cr = chunks[ci];
            if (!cr.valid || !cr.hadFit) continue;
            ++validChunks;

            double xMm     = cr.centerXmm;
            double floorZmm = poly::eval3(cr.coef, cr.floorCenterY) + cr.floorZRel / 1000.0;
            double dP = -cr.floorZRel;
            double dL = std::isnan(cr.leftLandZ)  ? NAN_D : (cr.leftLandZ  - floorZmm) * 1000.0;
            double dR = std::isnan(cr.rightLandZ) ? NAN_D : (cr.rightLandZ - floorZmm) * 1000.0;

            profP->x.push_back(xMm); profP->y.push_back(0); profP->s.push_back(xMm); profP->z.push_back(dP);
            profL->x.push_back(xMm); profL->y.push_back(0); profL->s.push_back(xMm); profL->z.push_back(dL);
            profR->x.push_back(xMm); profR->y.push_back(0); profR->s.push_back(xMm); profR->z.push_back(dR);
        }

        out->profiles.push_back(profP);
        out->profiles.push_back(profL);
        out->profiles.push_back(profR);
    }

    out->measurements.push_back({"valid_chunks", (double)validChunks, "", validChunks > 0});

    VISION_LOG_INFO("NotchMeasure: {} profiles → {} chunks ({} merged), {} valid, {} output pts",
                    nProf, nChunks, chunkSize, validChunks, outCloud->points.size());

    return {ToolStatus::Ok, "", out};
}

} // namespace vision
