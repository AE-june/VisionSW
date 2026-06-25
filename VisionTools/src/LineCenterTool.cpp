#include "LineCenterTool.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>

namespace vision {

ToolResult LineCenterTool::execute(VisionDataPtr input) {
    m_result = {};

    if (!input || !input->hasZMap())
        return { ToolStatus::Fail, "LineCenter: ZMap 입력이 없습니다" };

    const auto& map = *input->zmap;

    // 회전된 검색 ROI: 중심/반치수(픽셀) + 회전각
    const double cxRoi = (m_params.xPct + m_params.wPct / 2.0) * map.width;
    const double cyRoi = (m_params.yPct + m_params.hPct / 2.0) * map.height;
    const double hw = m_params.wPct * map.width  / 2.0;
    const double hh = m_params.hPct * map.height / 2.0;
    if (hw <= 0 || hh <= 0)
        return { ToolStatus::Fail, "LineCenter: ROI가 비어있습니다" };

    const double rad = m_params.angleDeg * 3.14159265358979323846 / 180.0;
    const double cosT = std::cos(rad), sinT = std::sin(rad);

    // 로컬(lx,ly) → 월드 픽셀 좌표
    auto toWorld = [&](double lx, double ly, double& wx, double& wy) {
        wx = cxRoi + lx * cosT - ly * sinT;
        wy = cyRoi + lx * sinT + ly * cosT;
    };
    // 로컬 좌표에서 최근접 픽셀 샘플 (유효하면 true)
    auto sampleLocal = [&](double lx, double ly, float& out) -> bool {
        double wx, wy; toWorld(lx, ly, wx, wy);
        int col = static_cast<int>(std::lround(wx));
        int row = static_cast<int>(std::lround(wy));
        if (!map.inBounds(col, row) || !map.valid(col, row)) return false;
        out = map.rawAt(col, row);
        return true;
    };

    // 스캔 축/방향 설정: scanX면 lx를 따라 스캔(각 ly마다), 아니면 ly를 따라 스캔(각 lx마다)
    const bool scanX = (m_params.scanDir == ScanDir::Lr || m_params.scanDir == ScanDir::Rl);
    const bool fwd   = (m_params.scanDir == ScanDir::Lr || m_params.scanDir == ScanDir::Tb);
    const double perpMin = scanX ? -hh : -hw;   // 스캔선들이 늘어선 축(수직축)
    const double perpMax = scanX ?  hh :  hw;
    const double scanLen = scanX ? 2.0 * hw : 2.0 * hh;
    const double scanMin = scanX ? -hw : -hh;
    const double scanMax = scanX ?  hw :  hh;
    const double thr = m_params.threshold;
    const bool d2l = (m_params.polarity == Polarity::DarkToLight);

    // 각 스캔선에서 첫 에지(임계값 교차)점을 수집 → 모멘트 누적
    double sx = 0, sy = 0;
    double sxx = 0, syy = 0, sxy = 0;
    long   n = 0;
    for (double p = perpMin; p <= perpMax; p += 1.0) {
        bool havePrev = false;
        float prevV = 0; double prevS = 0;
        for (double k = 0; k <= scanLen; k += 1.0) {
            double s = fwd ? (scanMin + k) : (scanMax - k);
            double lx = scanX ? s : p;
            double ly = scanX ? p : s;
            float v;
            if (!sampleLocal(lx, ly, v)) { havePrev = false; continue; }
            if (havePrev) {
                bool trans = d2l ? (prevV < thr && v >= thr)
                                 : (prevV >= thr && v < thr);
                if (trans) {
                    double se = (prevS + s) / 2.0;         // 에지 위치 = 두 샘플 중간
                    double elx = scanX ? se : p;
                    double ely = scanX ? p  : se;
                    double wx, wy; toWorld(elx, ely, wx, wy);
                    sx += wx; sy += wy; ++n;
                    sxx += wx * wx; syy += wy * wy; sxy += wx * wy;
                    break;
                }
            }
            prevV = v; prevS = s; havePrev = true;
        }
    }

    if (n < 2)
        return { ToolStatus::Fail, "LineCenter: 에지를 찾지 못했습니다 (임계값/극성/방향을 확인하세요)" };

    // 라인 중심 = 에지점들의 무게중심
    double cx = sx / n;
    double cy = sy / n;

    // 주축 각도 = 공분산 행렬의 주성분 방향
    double cxx = sxx / n - cx * cx;
    double cyy = syy / n - cy * cy;
    double cxy = sxy / n - cx * cy;
    double angleRad = 0.5 * std::atan2(2.0 * cxy, cxx - cyy);

    m_result.cx       = cx;
    m_result.cy       = cy;
    m_result.cxMm     = cx * map.xResMm;
    m_result.cyMm     = cy * map.yResMm;
    m_result.angleDeg = angleRad * 180.0 / 3.14159265358979323846;
    m_result.pointCount = static_cast<int>(n);
    m_result.valid    = true;

    VISION_LOG_INFO("LineCenter: center=({:.1f},{:.1f})px ({:.3f},{:.3f})mm, {} pts",
                    cx, cy, m_result.cxMm, m_result.cyMm, n);

    // 출력: ZMap 통과 + 기준점(point) stamp
    auto out = std::make_shared<VisionData>(*input);
    auto pt = std::make_shared<RefPoint>();
    pt->cx = cx;  pt->cy = cy;
    pt->cxMm = m_result.cxMm;  pt->cyMm = m_result.cyMm;
    pt->angleDeg = m_result.angleDeg;
    pt->valid = true;
    out->point = pt;

    return { ToolStatus::Ok, "", out };
}

} // namespace vision
