#include "LineCenterTool.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>

namespace vision {

// 단일 ROI에서 에지 캘리퍼로 라인 검색
bool LineCenterTool::findLine(const HeightMap& map, const LineCenterParams::ROI& roi,
                              LineCenterResult::Line& out) const {
    // 회전된 검색 ROI: 중심/반치수(픽셀) + 회전각
    const double cxRoi = (roi.xPct + roi.wPct / 2.0) * map.width;
    const double cyRoi = (roi.yPct + roi.hPct / 2.0) * map.height;
    const double hw = roi.wPct * map.width  / 2.0;
    const double hh = roi.hPct * map.height / 2.0;
    if (hw <= 0 || hh <= 0) return false;

    const double rad = roi.angleDeg * 3.14159265358979323846 / 180.0;
    const double cosT = std::cos(rad), sinT = std::sin(rad);

    auto toWorld = [&](double lx, double ly, double& wx, double& wy) {
        wx = cxRoi + lx * cosT - ly * sinT;
        wy = cyRoi + lx * sinT + ly * cosT;
    };
    auto sampleLocal = [&](double lx, double ly, float& v) -> bool {
        double wx, wy; toWorld(lx, ly, wx, wy);
        int col = static_cast<int>(std::lround(wx));
        int row = static_cast<int>(std::lround(wy));
        if (!map.inBounds(col, row) || !map.valid(col, row)) return false;
        v = map.rawAt(col, row);
        return true;
    };

    // 스캔 축/방향: scanX면 lx를 따라 스캔(각 ly마다), 아니면 ly를 따라 스캔
    const bool scanX = (m_params.scanDir == ScanDir::Lr || m_params.scanDir == ScanDir::Rl);
    const bool fwd   = (m_params.scanDir == ScanDir::Lr || m_params.scanDir == ScanDir::Tb);
    const double perpMin = scanX ? -hh : -hw;
    const double perpMax = scanX ?  hh :  hw;
    const double scanLen = scanX ? 2.0 * hw : 2.0 * hh;
    const double scanMin = scanX ? -hw : -hh;
    const double scanMax = scanX ?  hw :  hh;
    const double thr = m_params.threshold;
    const bool d2l = (roi.polarity == Polarity::DarkToLight);   // ROI별 극성

    double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
    long   n = 0;
    long   perpCount = 0;
    float  sampleMin = 1e9f, sampleMax = -1e9f;
    long   validCount = 0, aboveCount = 0;
    for (double p = perpMin; p <= perpMax; p += 1.0) {
        ++perpCount;
        bool havePrev = false;
        bool prevAbove = false; double prevS = 0;
        for (double k = 0; k <= scanLen; k += 1.0) {
            double s = fwd ? (scanMin + k) : (scanMax - k);
            double lx = scanX ? s : p;
            double ly = scanX ? p : s;
            float v;
            // 무효(NaN) 픽셀은 "어두움(below)"으로 취급 — 배경(NaN)↔타겟 경계도 에지로 검출
            bool valid = sampleLocal(lx, ly, v);
            bool above = valid && (v >= thr);
            if (valid) { ++validCount; if(v<sampleMin)sampleMin=v; if(v>sampleMax)sampleMax=v; }
            if (above) ++aboveCount;
            if (havePrev) {
                bool trans = d2l ? (!prevAbove && above)   // 흑→백
                                 : ( prevAbove && !above); // 백→흑
                if (trans) {
                    double se = (prevS + s) / 2.0;
                    double elx = scanX ? se : p;
                    double ely = scanX ? p  : se;
                    double wx, wy; toWorld(elx, ely, wx, wy);
                    sx += wx; sy += wy; ++n;
                    sxx += wx * wx; syy += wy * wy; sxy += wx * wy;
                    break;
                }
            }
            prevAbove = above; prevS = s; havePrev = true;
        }
    }

    VISION_LOG_INFO("LineCenter findLine: perpLines={} validPx={} abovePx={} edges={} thr={:.1f} rawRange=[{:.1f},{:.1f}] polarity={}",
        perpCount, validCount, aboveCount, n, thr,
        validCount>0?sampleMin:0.f, validCount>0?sampleMax:0.f,
        d2l ? "d2l" : "l2d");

    if (n < 2) return false;

    // 엣지포인트에 라인 피팅: centroid(mx,my) + 주축 방향(PCA/총최소제곱)
    double mx = sx / n, my = sy / n;
    double cxx = sxx / n - mx * mx;
    double cyy = syy / n - my * my;
    double cxy = sxy / n - mx * my;
    double angleRad = 0.5 * std::atan2(2.0 * cxy, cxx - cyy);
    const double dx = std::cos(angleRad), dy = std::sin(angleRad);   // 라인 방향(단위벡터)

    // 라인 중심 = ROI 중앙을 피팅 라인에 투영한 점 (엣지 검출 분포에 안 흔들림).
    // 피팅 라인은 centroid를 지나므로, ROI 중앙에서 라인에 내린 수선의 발.
    const double t = (cxRoi - mx) * dx + (cyRoi - my) * dy;
    const double cx = mx + t * dx, cy = my + t * dy;

    out.cx = cx;  out.cy = cy;
    out.cxMm = cx * map.xResMm;  out.cyMm = cy * map.yResMm;
    out.angleDeg = angleRad * 180.0 / 3.14159265358979323846;
    out.pointCount = static_cast<int>(n);
    return true;
}

ToolResult LineCenterTool::execute(VisionDataPtr input) {
    m_result = {};

    if (!input || !input->hasHeightMap())
        return { ToolStatus::Fail, "LineCenter: HeightMap 입력이 없습니다" };
    if (m_params.rois.empty())
        return { ToolStatus::Fail, "LineCenter: 검색 ROI가 없습니다" };

    const auto& map = *input->heightmap;

    // 타입화 출력: 검출 기준점(points)/원점(origin)만 전달. 이미지/heightmap 미포함.
    auto out = std::make_shared<VisionData>();
    out->sourceId = input->sourceId;
    auto pts = std::make_shared<std::vector<RefPoint>>();

    for (size_t i = 0; i < m_params.rois.size(); ++i) {
        LineCenterResult::Line line;
        if (!findLine(map, m_params.rois[i], line)) continue;
        line.roiIndex = static_cast<int>(i);
        m_result.lines.push_back(line);

        RefPoint rp;
        rp.cx = line.cx;  rp.cy = line.cy;
        rp.cxMm = line.cxMm;  rp.cyMm = line.cyMm;
        rp.angleDeg = line.angleDeg;
        rp.roiIndex = line.roiIndex;
        rp.valid = true;
        pts->push_back(rp);
    }

    if (m_result.lines.empty())
        return { ToolStatus::Fail, "LineCenter: 에지를 찾지 못했습니다 (임계값/극성/방향을 확인하세요)" };

    m_result.valid = true;
    out->points = pts;

    // 선택된 출력 좌표: X는 xRoi 라인의 x, Y는 yRoi 라인의 y
    auto origin = std::make_shared<OriginCoord>();
    for (const auto& rp : *pts) {
        if (rp.roiIndex == m_params.xRoi) { origin->xPx = rp.cx; origin->xMm = rp.cxMm; origin->hasX = true; }
        if (rp.roiIndex == m_params.yRoi) { origin->yPx = rp.cy; origin->yMm = rp.cyMm; origin->hasY = true; }
    }
    out->origin = origin;
    VISION_LOG_INFO("LineCenter: {} 라인 검색됨 ({} ROI 중)", m_result.lines.size(), m_params.rois.size());

    return { ToolStatus::Ok, "", out };
}

} // namespace vision
