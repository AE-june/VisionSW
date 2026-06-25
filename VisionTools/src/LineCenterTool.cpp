#include "LineCenterTool.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>

namespace vision {

// 단일 ROI에서 에지 캘리퍼로 라인 검색
bool LineCenterTool::findLine(const ZMap& map, const LineCenterParams::ROI& roi,
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
    for (double p = perpMin; p <= perpMax; p += 1.0) {
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

    if (n < 2) return false;

    double cx = sx / n, cy = sy / n;
    double cxx = sxx / n - cx * cx;
    double cyy = syy / n - cy * cy;
    double cxy = sxy / n - cx * cy;
    double angleRad = 0.5 * std::atan2(2.0 * cxy, cxx - cyy);

    out.cx = cx;  out.cy = cy;
    out.cxMm = cx * map.xResMm;  out.cyMm = cy * map.yResMm;
    out.angleDeg = angleRad * 180.0 / 3.14159265358979323846;
    out.pointCount = static_cast<int>(n);
    return true;
}

ToolResult LineCenterTool::execute(VisionDataPtr input) {
    m_result = {};

    if (!input || !input->hasZMap())
        return { ToolStatus::Fail, "LineCenter: ZMap 입력이 없습니다" };
    if (m_params.rois.empty())
        return { ToolStatus::Fail, "LineCenter: 검색 ROI가 없습니다" };

    const auto& map = *input->zmap;

    auto out = std::make_shared<VisionData>(*input);
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
