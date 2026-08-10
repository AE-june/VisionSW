#include "ExtractProfileTool.h"
#include "Logger.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <numeric>

namespace vision {

// ── line 모드 보간 헬퍼 ──────────────────────────────────────────────────

// nearest: float 산술 → double (extractAxis와 동일 연산, bit-identical 보장)
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
//  extractAxis — axisX(row) 또는 axisY(col) 단면 추출 (보간 없음)
//  span>1: 이웃 span줄의 유효 픽셀만 평균. NaN은 제외.
// ─────────────────────────────────────────────────────────────────────
static std::shared_ptr<Profile> extractAxis(
    const HeightMap& map, const Region* rgn,
    bool axisX,        // true=행(X축 프로파일), false=열(Y축 프로파일)
    int index, int span, int channel)
{
    const int W = map.width, H = map.height;
    const int len   = axisX ? W : H;    // 프로파일 길이
    const int total = axisX ? H : W;    // 수직 방향 크기

    // index 클램프
    index = std::clamp(index, 0, total - 1);
    const int half  = span / 2;
    const int r0    = std::max(0, index - half);
    const int r1    = std::min(total - 1, index + half);

    auto prof = std::make_shared<Profile>();
    prof->frameId = map.frameId;
    prof->label   = (axisX ? "row:" : "col:") + std::to_string(index);
    prof->s.resize(len);
    prof->x.resize(len);
    prof->y.resize(len);
    prof->z.resize(len, std::numeric_limits<double>::quiet_NaN());

    const float NaN = std::numeric_limits<float>::quiet_NaN();

    for (int i = 0; i < len; ++i) {
        // 물리 좌표
        const int col = axisX ? i      : index;
        const int row = axisX ? index  : i;
        prof->x[i] = map.xMm(col);
        prof->y[i] = map.yMm(row);
        prof->s[i] = i * (axisX ? map.xResMm : map.yResMm);  // 호장(arc length)

        // Region 마스크 — 길이 유지, 밖은 NaN
        if (rgn && !rgn->contains(col, row)) continue;

        // span 평균 (유효 픽셀만)
        double sum = 0; int cnt = 0;
        for (int j = r0; j <= r1; ++j) {
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
        const int repeat = std::max(1, m_params.repeat);  // D-3: 현재 1로 강제

        auto out = std::make_shared<VisionData>();
        out->sourceId = input->sourceId;
        out->frames   = input->frames;

        // repeat>1이면 index 오름차순으로 repeat개 추출.
        // D-3 결정 전까지 repeat을 1로 강제(파라미터는 정의, 값만 무시).
        const int total = axisX ? map.height : map.width;
        for (int k = 0; k < std::min(repeat, 1); ++k) {
            int idx = m_params.index + k;
            idx = std::clamp(idx, 0, total - 1);
            auto prof = extractAxis(map, rgn, axisX, idx, m_params.span, m_params.channel);
            out->profiles.push_back(std::move(prof));
        }

        VISION_LOG_INFO("ExtractProfile: mode={} index={} span={} → {} profiles",
            mode, m_params.index, m_params.span, out->profiles.size());
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
