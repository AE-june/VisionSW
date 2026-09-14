#include "ExtractProfileTool.h"
#include "Logger.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <numeric>

namespace vision {

// ── line 모드 보간 헬퍼 ──────────────────────────────────────────────────

// nearest: float 산술 → double (axis 밴드 평균과 동일 연산, bit-identical 보장)
static double sampleNearest(const HeightMap& map, double px, double py, int ch) {
    const int col = static_cast<int>(std::round(px));
    const int row = static_cast<int>(std::round(py));
    if (!map.inBounds(col, row)) return std::numeric_limits<double>::quiet_NaN();
    const float raw = map.rawAt(col, row, ch);
    if (std::isnan(raw)) return std::numeric_limits<double>::quiet_NaN();
    return static_cast<double>((raw - map.zZeroCount) * map.zResMm);
}

// bilinear: 4 이웃 중 하나라도 OOB 또는 NaN이면 NaN 반환
static double sampleBilinear(const HeightMap& map, double px, double py, int ch) {
    const int x0 = static_cast<int>(std::floor(px)), y0 = static_cast<int>(std::floor(py));
    const int x1 = x0 + 1, y1 = y0 + 1;
    if (x0 < 0 || x1 >= map.width || y0 < 0 || y1 >= map.height)
        return std::numeric_limits<double>::quiet_NaN();
    const float f00 = map.rawAt(x0, y0, ch), f10 = map.rawAt(x1, y0, ch);
    const float f01 = map.rawAt(x0, y1, ch), f11 = map.rawAt(x1, y1, ch);
    if (std::isnan(f00) || std::isnan(f10) || std::isnan(f01) || std::isnan(f11))
        return std::numeric_limits<double>::quiet_NaN();
    const double tx = px - x0, ty = py - y0;
    auto toMm = [&](float r) {
        return (static_cast<double>(r) - map.zZeroCount) * map.zResMm;
    };
    return (1-tx)*(1-ty)*toMm(f00) + tx*(1-ty)*toMm(f10)
         + (1-tx)*ty   *toMm(f01) + tx*ty   *toMm(f11);
}

// ─────────────────────────────────────────────────────────────────────
//  extractLine — line 모드: p0→p1 직선을 count개 균등 샘플
//  샘플 위치: t = i/(count-1) (비율 계산, 누적 금지 §3.2)
//  bilinear: 이웃 4개 중 NaN/OOB이면 NaN
// ─────────────────────────────────────────────────────────────────────
static std::shared_ptr<Profile> extractLine(
    const HeightMap& map, const Region* rgn,
    double p0x, double p0y, double p1x, double p1y,
    const std::string& unit, int count,
    const std::string& interp, int channel)
{
    const bool unitMm = (unit != "px");

    // mm → 픽셀 좌표 변환
    double px0, py0, px1, py1;
    if (unitMm) {
        px0 = p0x / map.xResMm + map.originCol;
        py0 = p0y / map.yResMm + map.originRow;
        px1 = p1x / map.xResMm + map.originCol;
        py1 = p1y / map.yResMm + map.originRow;
    } else {
        px0 = p0x; py0 = p0y; px1 = p1x; py1 = p1y;
    }

    const double dpx = px1 - px0, dpy = py1 - py0;
    const double lenPx = std::sqrt(dpx*dpx + dpy*dpy);
    const double lenMm = std::sqrt((dpx * map.xResMm) * (dpx * map.xResMm)
                                 + (dpy * map.yResMm) * (dpy * map.yResMm));

    if (count <= 0)
        count = std::max(1, static_cast<int>(std::ceil(lenPx)) + 1);

    const bool bilinear = (interp != "nearest");

    auto prof = std::make_shared<Profile>();
    prof->frameId = map.frameId;
    prof->label   = "line";
    prof->s.resize(count);
    prof->x.resize(count);
    prof->y.resize(count);
    prof->z.resize(count, std::numeric_limits<double>::quiet_NaN());

    for (int i = 0; i < count; ++i) {
        // 비율로 계산 — 누적 덧셈 사용 금지 (§3.2)
        const double t  = (count > 1) ? static_cast<double>(i) / (count - 1) : 0.0;
        const double px = px0 + t * dpx;
        const double py = py0 + t * dpy;

        prof->x[i] = (px - map.originCol) * map.xResMm;
        prof->y[i] = (py - map.originRow) * map.yResMm;
        prof->s[i] = t * lenMm;

        // Region 검사: 가장 가까운 정수 픽셀로
        const int icol = static_cast<int>(std::round(px));
        const int irow = static_cast<int>(std::round(py));
        if (rgn && (!map.inBounds(icol, irow) || !rgn->contains(icol, irow)))
            continue;  // z = NaN 유지

        prof->z[i] = bilinear ? sampleBilinear(map, px, py, channel)
                               : sampleNearest(map, px, py, channel);
    }
    return prof;
}

ExtractProfileTool::ExtractProfileTool(ExtractProfileParams params)
    : m_params(std::move(params)) {}

// ─────────────────────────────────────────────────────────────────────
//  averageBand — axisX(row) 또는 axisY(col) 방향으로 [lo, hi) 라인 범위를
//  하나의 Profile로 평균. 유효 픽셀만 평균, NaN은 제외 (보간 없음).
//    axisX: lo..hi 는 행(row) 범위, 프로파일은 열(col) 방향 길이 W
//    axisY: lo..hi 는 열(col) 범위, 프로파일은 행(row) 방향 길이 H
//  midLine: 라벨용 대표 라인 인덱스(밴드 중앙).
// ─────────────────────────────────────────────────────────────────────
static std::shared_ptr<Profile> averageBand(
    const HeightMap& map, const Region* rgn,
    bool axisX, int lo, int hi, int midLine, int channel,
    const std::string& aggregation)
{
    const int W = map.width, H = map.height;
    const int len = axisX ? W : H;    // 프로파일 길이

    auto prof = std::make_shared<Profile>();
    prof->frameId = map.frameId;
    prof->label   = (axisX ? "row:" : "col:") + std::to_string(midLine);
    prof->s.resize(len);
    prof->x.resize(len);
    prof->y.resize(len);
    prof->z.resize(len, std::numeric_limits<double>::quiet_NaN());

    for (int i = 0; i < len; ++i) {
        // 물리 좌표 (대표 라인 기준)
        const int col = axisX ? i        : midLine;
        const int row = axisX ? midLine  : i;
        prof->x[i] = map.xMm(col);
        prof->y[i] = map.yMm(row);
        prof->s[i] = i * (axisX ? map.xResMm : map.yResMm);  // 호장(arc length)

        // Region 마스크 — 길이 유지, 밖은 NaN
        if (rgn && !rgn->contains(col, row)) continue;

        // 밴드 집계 (유효 픽셀만, NaN 제외)
        if (aggregation == "mean") {
            double sum = 0; int cnt = 0;
            for (int j = lo; j < hi; ++j) {
                const int c = axisX ? i : j;
                const int r = axisX ? j : i;
                if (!map.inBounds(c, r)) continue;
                const float raw = map.rawAt(c, r, channel);
                if (std::isnan(raw)) continue;
                sum += (raw - map.zZeroCount) * map.zResMm;
                ++cnt;
            }
            if (cnt > 0) prof->z[i] = sum / cnt;
        } else if (aggregation == "min") {
            double result = std::numeric_limits<double>::quiet_NaN();
            for (int j = lo; j < hi; ++j) {
                const int c = axisX ? i : j;
                const int r = axisX ? j : i;
                if (!map.inBounds(c, r)) continue;
                const float raw = map.rawAt(c, r, channel);
                if (std::isnan(raw)) continue;
                const double v = (raw - map.zZeroCount) * map.zResMm;
                if (std::isnan(result) || v < result) result = v;
            }
            prof->z[i] = result;
        } else if (aggregation == "max") {
            double result = std::numeric_limits<double>::quiet_NaN();
            for (int j = lo; j < hi; ++j) {
                const int c = axisX ? i : j;
                const int r = axisX ? j : i;
                if (!map.inBounds(c, r)) continue;
                const float raw = map.rawAt(c, r, channel);
                if (std::isnan(raw)) continue;
                const double v = (raw - map.zZeroCount) * map.zResMm;
                if (std::isnan(result) || v > result) result = v;
            }
            prof->z[i] = result;
        } else if (aggregation == "median") {
            std::vector<double> vals;
            for (int j = lo; j < hi; ++j) {
                const int c = axisX ? i : j;
                const int r = axisX ? j : i;
                if (!map.inBounds(c, r)) continue;
                const float raw = map.rawAt(c, r, channel);
                if (std::isnan(raw)) continue;
                vals.push_back((raw - map.zZeroCount) * map.zResMm);
            }
            if (!vals.empty()) {
                const size_t mid = vals.size() / 2;
                std::nth_element(vals.begin(), vals.begin() + mid, vals.end());
                prof->z[i] = vals[mid];
            }
        } else if (aggregation == "stddev") {
            // Welford's online algorithm
            double mean = 0.0, M2 = 0.0;
            int cnt = 0;
            for (int j = lo; j < hi; ++j) {
                const int c = axisX ? i : j;
                const int r = axisX ? j : i;
                if (!map.inBounds(c, r)) continue;
                const float raw = map.rawAt(c, r, channel);
                if (std::isnan(raw)) continue;
                const double v = (raw - map.zZeroCount) * map.zResMm;
                ++cnt;
                const double delta = v - mean;
                mean += delta / cnt;
                M2 += delta * (v - mean);
            }
            if (cnt > 1) prof->z[i] = std::sqrt(M2 / cnt);
            else if (cnt == 1) prof->z[i] = 0.0;
        } else {
            // 알 수 없는 집계 방식 → mean 폴백
            double sum = 0; int cnt = 0;
            for (int j = lo; j < hi; ++j) {
                const int c = axisX ? i : j;
                const int r = axisX ? j : i;
                if (!map.inBounds(c, r)) continue;
                const float raw = map.rawAt(c, r, channel);
                if (std::isnan(raw)) continue;
                sum += (raw - map.zZeroCount) * map.zResMm;
                ++cnt;
            }
            if (cnt > 0) prof->z[i] = sum / cnt;
        }
    }

    return prof;
}

// ─────────────────────────────────────────────────────────────────────
//  execute
// ─────────────────────────────────────────────────────────────────────
ToolResult ExtractProfileTool::execute(VisionDataPtr input) {
    if (!input || !input->inHeightMap(0))
        return { ToolStatus::Fail, "ExtractProfile: HeightMap(포트 0)이 없습니다." };

    const HeightMap& map = *input->inHeightMap(0);
    const Region*    rgn = input->inRegion(1) ? input->inRegion(1).get() : nullptr;

    // Region 프레임 불일치 검사
    if (rgn && !map.frameId.empty() && !rgn->frameId.empty()
            && rgn->frameId != map.frameId) {
        return { ToolStatus::Fail,
            "ExtractProfile: Region 프레임(" + rgn->frameId +
            ")이 HeightMap 프레임(" + map.frameId + ")과 다릅니다. TODO(T0-1 P3)" };
    }

    const std::string& mode = m_params.mode;

    if (mode == "axisX" || mode == "axisY") {
        const bool axisX = (mode == "axisX");

        auto out = std::make_shared<VisionData>();
        out->sourceId = input->sourceId;
        out->frames   = input->frames;

        // 타일링 대상 라인 범위 [lineStart, lineEnd)
        //   axisX: 행(row) 방향, axisY: 열(col) 방향
        const int total = axisX ? map.height : map.width;
        int lineStart = 0, lineEnd = total;
        if (rgn) {
            const Rect2D bb = rgn->boundingBox();
            if (bb.valid()) {
                if (axisX) { lineStart = bb.y; lineEnd = bb.bottom(); }
                else       { lineStart = bb.x; lineEnd = bb.right();  }
                lineStart = std::clamp(lineStart, 0, total);
                lineEnd   = std::clamp(lineEnd,   0, total);
            }
        }

        const int extent = std::max(0, lineEnd - lineStart);
        const int N      = std::max(1, m_params.span);   // 밴드당 라인수
        // 정수 나눗셈 floor. 0이면 최소 1개 밴드.
        int bands = extent / N;
        if (bands < 1) bands = 1;

        for (int b = 0; b < bands; ++b) {
            const int lo  = lineStart + b * N;
            int       hi  = lo + N;
            if (hi > lineEnd) hi = lineEnd;   // 마지막 밴드 클램프
            const int mid = std::clamp(lo + N / 2, 0, total - 1);
            auto prof = averageBand(map, rgn, axisX, lo, hi, mid, m_params.channel, m_params.aggregation);
            out->profiles.push_back(std::move(prof));
        }

        VISION_LOG_INFO("ExtractProfile: mode={} span={} lines=[{},{}) → {} profiles",
            mode, m_params.span, lineStart, lineEnd, out->profiles.size());
        return { ToolStatus::Ok, "", out };
    }

    if (mode == "line") {
        auto out = std::make_shared<VisionData>();
        out->sourceId = input->sourceId;
        out->frames   = input->frames;
        auto prof = extractLine(map, rgn,
            m_params.p0x, m_params.p0y, m_params.p1x, m_params.p1y,
            m_params.unit, m_params.count, m_params.interp, m_params.channel);
        out->profiles.push_back(std::move(prof));
        VISION_LOG_INFO("ExtractProfile: mode=line count={} interp={} → 1 profiles",
            m_params.count, m_params.interp);
        return { ToolStatus::Ok, "", out };
    }

    return { ToolStatus::Fail, "ExtractProfile: 알 수 없는 mode=" + mode };
}

} // namespace vision
