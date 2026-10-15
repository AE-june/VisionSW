#include "LineFitTool.h"
#include "Logger.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace vision {

namespace {
constexpr double kPi = 3.14159265358979323846;

// 스캔축을 따라 한 라인에서 특징점 위치(px, 서브픽셀 아님) 반환. 실패 시 false.
//   ridge/valley: 유효 픽셀 중 raw 최대/최소 위치
//   edge:         임계 교차(rising/falling) 첫 위치
bool findFeatureAlong(const HeightMap& map, const Region* rgn,
                      bool scanX, bool fwd, int fixed, int lo, int hi,
                      LineFeature feat, float thr, bool rising,
                      double& outPos)
{
    auto sample = [&](int s) -> std::pair<bool, float> {
        int col = scanX ? s : fixed;
        int row = scanX ? fixed : s;
        if (!map.inBounds(col, row) || !map.valid(col, row)) return {false, 0.f};
        if (rgn && !rgn->contains(col, row)) return {false, 0.f};
        return {true, map.rawAt(col, row)};
    };

    if (feat == LineFeature::Edge) {
        bool havePrev = false, prevAbove = false;
        for (int d = 0; d < (hi - lo); ++d) {
            int s = fwd ? (lo + d) : (hi - 1 - d);
            auto [ok, v] = sample(s);
            if (!ok) { havePrev = false; continue; }
            bool above = v >= thr;
            if (havePrev) {
                bool trans = rising ? (!prevAbove && above) : (prevAbove && !above);
                if (trans) { outPos = fwd ? (s - 0.5) : (s + 0.5); return true; }
            }
            prevAbove = above; havePrev = true;
        }
        return false;
    }

    // Ridge / Valley: 극값 위치
    bool have = false;
    float best = 0.f; int bestS = 0;
    for (int d = 0; d < (hi - lo); ++d) {
        int s = fwd ? (lo + d) : (hi - 1 - d);
        auto [ok, v] = sample(s);
        if (!ok) continue;
        bool better = !have
            || (feat == LineFeature::Ridge ? (v > best) : (v < best));
        if (better) { best = v; bestS = s; have = true; }
    }
    if (!have) return false;
    outPos = bestS;
    return true;
}
} // namespace

ToolResult LineFitTool::execute(VisionDataPtr input) {
    // HeightMap — 모든 포트 스캔 (연결 실수 허용)
    std::shared_ptr<HeightMap> hmPtr;
    for (size_t p = 0; p < input->inputs.size() && !hmPtr; ++p)
        hmPtr = input->inHeightMap(p);
    if (!hmPtr)
        return { ToolStatus::Fail, "LineFit: HeightMap이 없습니다." };
    const HeightMap& map = *hmPtr;

    // Region — 모든 포트 union
    std::vector<std::shared_ptr<Region>> allRegions;
    for (size_t p = 0; p < input->inputs.size(); ++p) {
        const auto& regs = input->inRegions(p);
        allRegions.insert(allRegions.end(), regs.begin(), regs.end());
    }
    std::shared_ptr<Region> unionRgn;
    if (allRegions.size() == 1) {
        unionRgn = allRegions[0];
    } else if (allRegions.size() > 1) {
        unionRgn = std::make_shared<Region>(Region::makeEmpty(map.width, map.height));
        for (const auto& r : allRegions) {
            if (!r) continue;
            for (size_t i = 0; i < unionRgn->mask.size(); ++i)
                unionRgn->mask[i] |= r->mask[i];
        }
    }
    const Region* rgn = unionRgn ? unionRgn.get() : nullptr;

    // 검색 영역: Region 바운딩박스 or 전체
    int x0 = 0, y0 = 0, x1 = map.width, y1 = map.height;
    if (rgn && !rgn->empty()) {
        auto bb = rgn->boundingBox();
        if (bb.w <= 0 || bb.h <= 0)
            return { ToolStatus::Fail, "LineFit: Region이 비어있습니다." };
        x0 = bb.x; y0 = bb.y; x1 = bb.x + bb.w; y1 = bb.y + bb.h;
    }

    const bool scanX = (m_params.scanDir == LineScanDir::Lr || m_params.scanDir == LineScanDir::Rl);
    const bool fwd   = (m_params.scanDir == LineScanDir::Lr || m_params.scanDir == LineScanDir::Tb);

    // 특징점 수집 (px). scanX면 각 행에서 특징 col, 아니면 각 열에서 특징 row.
    std::vector<double> px, py;
    if (scanX) {
        for (int row = y0; row < y1; ++row) {
            double pos;
            if (findFeatureAlong(map, rgn, true, fwd, row, x0, x1,
                                 m_params.feature, m_params.threshold, m_params.risingEdge, pos)) {
                px.push_back(pos); py.push_back(row);
            }
        }
    } else {
        for (int col = x0; col < x1; ++col) {
            double pos;
            if (findFeatureAlong(map, rgn, false, fwd, col, y0, y1,
                                 m_params.feature, m_params.threshold, m_params.risingEdge, pos)) {
                px.push_back(col); py.push_back(pos);
            }
        }
    }

    const long nTotal = static_cast<long>(px.size());
    if (nTotal < 2)
        return { ToolStatus::Fail, "LineFit: 특징점 부족 (검출 " + std::to_string(nTotal) + "개). 임계값/모드/방향 확인." };

    // 점 부분집합의 라인까지 수직거리(mm) — 축별 분해능 반영
    auto perpMm = [&](double ax, double ay, double ux, double uy, long k) {
        const double perpPx = (px[k] - ax) * (-uy) + (py[k] - ay) * ux;
        return std::hypot(perpPx * (-uy) * map.xResMm, perpPx * ux * map.yResMm);
    };

    // ── 인라이어 선택 (RANSAC) 또는 전체 ─────────────────────────────────
    std::vector<long> idx;
    if (m_params.fitMethod == LineFitMethod::Ransac && nTotal >= 2) {
        // 결정론적 PRNG(고정 시드) — 검사 반복성 보장
        uint32_t seed = 2654435761u;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return seed; };
        const double tol = m_params.ransacTolMm;
        std::vector<long> best;
        const int iters = std::max(1, m_params.ransacIters);
        for (int it = 0; it < iters; ++it) {
            const long i = rnd() % nTotal, j = rnd() % nTotal;
            if (i == j) continue;
            double ux = px[j] - px[i], uy = py[j] - py[i];
            const double L = std::hypot(ux, uy);
            if (L < 1e-9) continue;
            ux /= L; uy /= L;
            std::vector<long> inl;
            for (long k = 0; k < nTotal; ++k)
                if (perpMm(px[i], py[i], ux, uy, k) <= tol) inl.push_back(k);
            if (inl.size() > best.size()) best = std::move(inl);
        }
        if (best.size() >= 2) idx = std::move(best);
    }
    if (idx.size() < 2) { idx.resize(nTotal); for (long i = 0; i < nTotal; ++i) idx[i] = i; }

    const long n = static_cast<long>(idx.size());

    // ── PCA 직선 피팅 (선택된 점 집합) ──────────────────────────────────
    double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
    for (long ii = 0; ii < n; ++ii) {
        const long i = idx[ii];
        sx += px[i]; sy += py[i];
        sxx += px[i] * px[i]; syy += py[i] * py[i]; sxy += px[i] * py[i];
    }
    const double mx = sx / n, my = sy / n;
    const double cxx = sxx / n - mx * mx;
    const double cyy = syy / n - my * my;
    const double cxy = sxy / n - mx * my;
    const double angleRad = 0.5 * std::atan2(2.0 * cxy, cxx - cyy);
    const double dx = std::cos(angleRad), dy = std::sin(angleRad);

    // 라인 중심 = 검색영역 중앙을 피팅 라인에 투영
    const double cxRoi = (x0 + x1) * 0.5, cyRoi = (y0 + y1) * 0.5;
    const double t = (cxRoi - mx) * dx + (cyRoi - my) * dy;
    const double cx = mx + t * dx, cy = my + t * dy;
    const double angleDeg = angleRad * 180.0 / kPi;

    // 직진도: 선택 점의 라인까지 수직거리 mm RMS
    double ssPerp = 0;
    double tmin = 1e300, tmax = -1e300;
    for (long ii = 0; ii < n; ++ii) {
        const long i = idx[ii];
        const double d = perpMm(mx, my, dx, dy, i);
        ssPerp += d * d;
        const double ti = (px[i] - mx) * dx + (py[i] - my) * dy;
        if (ti < tmin) tmin = ti;
        if (ti > tmax) tmax = ti;
    }
    const double straightnessMm = std::sqrt(ssPerp / n);

    const double p0x = mx + tmin * dx, p0y = my + tmin * dy;   // px
    const double p1x = mx + tmax * dx, p1y = my + tmax * dy;
    const double p0xMm = (p0x - map.originCol) * map.xResMm;
    const double p0yMm = (p0y - map.originRow) * map.yResMm;
    const double p1xMm = (p1x - map.originCol) * map.xResMm;
    const double p1yMm = (p1y - map.originRow) * map.yResMm;
    const double lengthMm = std::hypot(p1xMm - p0xMm, p1yMm - p0yMm);

    const double cxMm = (cx - map.originCol) * map.xResMm;
    const double cyMm = (cy - map.originRow) * map.yResMm;

    // 출력
    auto out = std::make_shared<VisionData>();
    out->sourceId = input->sourceId;

    auto lm = std::make_shared<LineModel>();
    lm->cx = cx; lm->cy = cy; lm->cxMm = cxMm; lm->cyMm = cyMm;
    lm->angleDeg = angleDeg; lm->straightnessMm = straightnessMm;
    lm->p0x = p0x; lm->p0y = p0y; lm->p1x = p1x; lm->p1y = p1y;
    lm->p0xMm = p0xMm; lm->p0yMm = p0yMm; lm->p1xMm = p1xMm; lm->p1yMm = p1yMm;
    lm->lengthMm = lengthMm;
    lm->valid = true; lm->frameId = map.frameId;
    out->lines.push_back(lm);

    RefPoint rp;
    rp.cx = cx; rp.cy = cy; rp.cxMm = cxMm; rp.cyMm = cyMm;
    rp.angleDeg = angleDeg; rp.roiIndex = 0; rp.valid = true;
    out->points.push_back(rp);

    Overlay ov;
    ov.kind = Overlay::Kind::Lines;
    Overlay::LineData ld;
    ld.cx = cx; ld.cy = cy; ld.cxMm = cxMm; ld.cyMm = cyMm;
    ld.angleDeg = angleDeg; ld.roiIndex = 0; ld.pointCount = static_cast<int>(n);
    ld.p0x = p0x; ld.p0y = p0y; ld.p1x = p1x; ld.p1y = p1y;
    ov.lines.push_back(ld);
    out->overlays.push_back(std::move(ov));

    out->measurements = {
        {"angleDeg",      angleDeg,               "deg", true},
        {"cxMm",          cxMm,                   "mm",  true},
        {"cyMm",          cyMm,                   "mm",  true},
        {"lengthMm",      lengthMm,               "mm",  true},
        {"straightness",  straightnessMm,         "mm",  true},
        {"pointCount",    static_cast<double>(n), "pts", true},
    };

    VISION_LOG_INFO("LineFit: 라인 검출 cx={:.1f} cy={:.1f} angle={:.2f}deg pts={} straight={:.4f}mm",
                    cx, cy, angleDeg, n, straightnessMm);
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
